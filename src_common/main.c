/*---------------------------------------------------------------------------*/
/*!
 * \file   main.c
 * \author Wayne Joubert
 * \date   Wed May 22 11:22:14 EDT 2013
 * \brief  Main driver for KBA sweep miniapp.
 * \note   Copyright (C) 2013 Oak Ridge National Laboratory, UT-Battelle, LLC.
 */
/*---------------------------------------------------------------------------*/

#include <stdio.h>

#include "arguments.h"
#include "env.h"
#include "definitions.h"
#include "dimensions.h"
#include "memory.h"
#include "pointer.h"
#include "quantities.h"
#include "array_operations.h"
#include "sweeper.h"

/*===========================================================================*/
/*Struct to hold run result data---*/

typedef struct
{
  P      normsq;
  P      normsqdiff;
  double flops;
  double floprate;
  Timer  time;
} Run_Data;

/*===========================================================================*/
/*---Perform run---*/

void run( Env* env, Arguments* args, Run_Data* run_data );

void run( Env* env, Arguments* args, Run_Data* run_data )
{
  if( ! Env_is_proc_active( env ) )
  {
    return;
  }

  /*---Declarations---*/

  Dimensions  dims_g;       /*---dims for entire problem---*/
  Dimensions  dims;         /*---dims for the part on this MPI proc---*/
  Quantities  quan;
  Sweeper     sweeper;

  Pointer vi = Pointer_null();
  Pointer vo = Pointer_null();

  run_data->normsq     = P_zero();
  run_data->normsqdiff = P_zero();

  int iteration   = 0;
  int niterations = 0;

  Timer t1             = 0;
  Timer t2             = 0;

  run_data->time       = 0;
  run_data->flops      = 0;
  run_data->floprate   = 0;
  run_data->normsq     = 0;
  run_data->normsqdiff = 0;

  /*---Define problem specs---*/

  dims_g.nx   = Arguments_consume_int_or_default( args, "--nx",  5 );
  dims_g.ny   = Arguments_consume_int_or_default( args, "--ny",  5 );
  dims_g.nz   = Arguments_consume_int_or_default( args, "--nz",  5 );
  dims_g.ne   = Arguments_consume_int_or_default( args, "--ne", 30 );
  dims_g.na   = Arguments_consume_int_or_default( args, "--na", 33 );
  niterations = Arguments_consume_int_or_default( args, "--niterations", 1 );
  dims_g.nm   = NM;

  Insist( dims_g.nx > 0 ? "Invalid nx supplied." : 0 );
  Insist( dims_g.ny > 0 ? "Invalid ny supplied." : 0 );
  Insist( dims_g.nz > 0 ? "Invalid nz supplied." : 0 );
  Insist( dims_g.ne > 0 ? "Invalid ne supplied." : 0 );
  Insist( dims_g.nm > 0 ? "Invalid nm supplied." : 0 );
  Insist( dims_g.na > 0 ? "Invalid na supplied." : 0 );
  Insist( niterations >= 0 ? "Invalid iteration count supplied." : 0 );

  /*---Initialize (local) dimensions - domain decomposition---*/

  dims = dims_g;

  dims.nx =
      ( ( Env_proc_x_this( env ) + 1 ) * dims_g.nx ) / Env_nproc_x( env )
    - ( ( Env_proc_x_this( env )     ) * dims_g.nx ) / Env_nproc_x( env );

  dims.ny =
      ( ( Env_proc_y_this( env ) + 1 ) * dims_g.ny ) / Env_nproc_y( env )
    - ( ( Env_proc_y_this( env )     ) * dims_g.ny ) / Env_nproc_y( env );

  /*---Initialize quantities---*/

  Quantities_ctor( &quan, dims, env );

  /*---Allocate arrays---*/

  Pointer_ctor( &vi, Dimensions_size_state( dims, NU ),
                                            Env_cuda_is_using_device( env ) );
  Pointer_set_pinned( &vi, Bool_true );
  Pointer_create( &vi );

  Pointer_ctor( &vo, Dimensions_size_state( dims, NU ),
                                            Env_cuda_is_using_device( env ) );
  Pointer_set_pinned( &vo, Bool_true );
  Pointer_create( &vo );

  /*---Initialize input state array---*/

  initialize_state( Pointer_h( &vi ), dims, NU, &quan );

  /*---Initialize output state array---*/
  /*---This is not strictly required for the output vector but might
       have a performance effect from pre-touching pages.
  ---*/

  initialize_state_zero( Pointer_h( &vo ), dims, NU );

  /*---Initialize sweeper---*/

  Sweeper_ctor( &sweeper, dims, &quan, env, args );

  /*---Check that all command line args used---*/

  Insist( Arguments_are_all_consumed( args )
                                          ? "Invalid argument detected." : 0 );

  /*---Call sweeper---*/

  t1 = Env_get_synced_time( env );

  for( iteration=0; iteration<niterations; ++iteration )
  {
    Sweeper_sweep( &sweeper,
                   iteration%2==0 ? &vo : &vi,
                   iteration%2==0 ? &vi : &vo,
                   &quan,
                   env );
  }

  t2 = Env_get_synced_time( env );
  run_data->time = t2 - t1;

  /*---Compute flops used---*/

  run_data->flops = Env_sum_d( niterations *
         ( Dimensions_size_state( dims, NU ) * NOCTANT * 2. * dims.na
         + Dimensions_size_state_angles( dims, NU )
                                        * Quantities_flops_per_solve( dims )
         + Dimensions_size_state( dims, NU ) * NOCTANT * 2. * dims.na ), env );

  run_data->floprate = run_data->time <= (Timer)0 ?
                                   0 : run_data->flops / run_data->time / 1e9;

  /*---Compute, print norm squared of result---*/

  get_state_norms( Pointer_h( &vi ), Pointer_h( &vo ),
                     dims, NU, &run_data->normsq, &run_data->normsqdiff, env );

  /*---Deallocations---*/

  Pointer_dtor( &vi );
  Pointer_dtor( &vo );

  Sweeper_dtor( &sweeper, env );
  Quantities_dtor( &quan );
}

/*===========================================================================*/
/*---Perform two runs, compare results---*/

Bool_t compare_runs( Env* env, char* argstring1, char* argstring2 );
Bool_t compare_runs( Env* env, char* argstring1, char* argstring2 )
{
  Arguments args1;
  Arguments args2;
  Run_Data  rd1;
  Run_Data  rd2;

  Arguments_ctor_string( &args1, argstring1 );
  Env_set_values( env, &args1 );

  if( Env_do_output( env ) )
  {
    printf("%s // ", argstring1);
  }
  run( env, &args1, &rd1 );

  Arguments_ctor_string( &args2, argstring2 );
  Env_set_values( env, &args2 );

  if( Env_do_output( env ) )
  {
    printf("%s // ", argstring2);
  }
  run( env, &args2, &rd2 );

  Bool_t pass = Env_do_output( env ) ?
                rd1.normsqdiff == P_zero() &&
                rd2.normsqdiff == P_zero() &&
                rd1.normsq == rd2.normsq : Bool_false;

  if( Env_do_output( env ) )
  {
    printf("%e %e %e %e // %i %i %i // %s\n",
      rd1.normsqdiff, rd2.normsqdiff, rd1.normsq, rd2.normsq,
      rd1.normsq == rd2.normsq,
      rd1.normsqdiff == P_zero(), rd2.normsqdiff == P_zero(),
      pass ? "PASS" : "FAIL" );
  }

  return pass;
}

/*===========================================================================*/
/*---Tester---*/

void test( Env* env );

void test( Env* env )
{
  Bool_t pass = Bool_false;
  pass = compare_runs( env, 
     "--nx 3 --ny 5 --nz 6 --ne 2 --na 5 --nblock_z 2 --nproc_x 1",
     "--nx 3 --ny 5 --nz 6 --ne 2 --na 5 --nblock_z 2 --nproc_x 1" );
/*
     "--nx 3 --ny 5 --nz 6 --ne 2 --na 5 --nblock_z 2 --nproc_x 2 --is_using_device 1" );
*/
  if( Env_do_output( env ) )
  {
    printf( "%i\n", pass );
  }
}

/*===========================================================================*/
/*---Main---*/

int main( int argc, char** argv )
{
  /*---Declarations---*/
  Env env;

  /*---Initialize for execution---*/

  Env_initialize( &env, argc, argv );

  if( argc == 1 )
  {
    test( &env );
  }
  else
  {
    Arguments args;
    Run_Data  run_data;

    Arguments_ctor( &args, argc, argv );
    Env_set_values( &env, &args );

    /*---Perform run---*/

    run( &env, &args, &run_data );

    if( Env_do_output( &env ) )
    {
      printf( "Normsq result: %.8e  diff: %.3e  %s  time: %.3f  GF/s: %.3f\n",
              (double)run_data.normsq, (double)run_data.normsqdiff,
              run_data.normsqdiff==P_zero() ? "PASS" : "FAIL",
              (double)run_data.time, run_data.floprate );
    }

    /*---Deallocations---*/

    Arguments_dtor( &args );

  }

  /*---Finalize execution---*/

  Env_finalize( &env );

} /*---main---*/

/*---------------------------------------------------------------------------*/
