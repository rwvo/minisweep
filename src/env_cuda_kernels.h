/*---------------------------------------------------------------------------*/
/*!
 * \file   env_cuda_kernels.h
 * \author Wayne Joubert
 * \date   Tue Apr 22 17:03:08 EDT 2014
 * \brief  Environment settings for cuda.  Code for device kernels.
 * \note   Copyright (C) 2014 Oak Ridge National Laboratory, UT-Battelle, LLC.
 */
/*---------------------------------------------------------------------------*/

#ifndef _env_cuda_kernels_h_
#define _env_cuda_kernels_h_

#ifdef USE_CUDA
#include "cuda.h"
#endif

#include "types_kernels.h"

#ifndef Assert
#define Assert(v)
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/*===========================================================================*/
/*---Enums---*/

#ifndef __MIC__
enum{ VEC_LEN = 32 };
#endif

/*===========================================================================*/
/*---Pointer to device shared memory---*/

#ifdef __CUDA_ARCH__
__shared__ extern char cuda_shared_memory[];
#endif

TARGET_HD static char* Env_cuda_shared_memory()
{
#ifdef __CUDA_ARCH__
  return cuda_shared_memory;
#else
  return (char*) NULL;
#endif
}

/*===========================================================================*/
/*---Device thread management---*/

TARGET_HD static int Env_cuda_threadblock( int axis )
{
  Assert( axis >= 0 && axis < 2 );

#ifdef __CUDA_ARCH__
  return axis==0 ? blockIdx.x :
         axis==1 ? blockIdx.y :
                   blockIdx.z;
#else
  return 0;
#endif
}

/*---------------------------------------------------------------------------*/

TARGET_HD static int Env_cuda_thread_in_threadblock( int axis )
{
  Assert( axis >= 0 && axis < 2 );

#ifdef __CUDA_ARCH__
  return axis==0 ? threadIdx.x :
         axis==1 ? threadIdx.y :
                   threadIdx.z;
#else
  return 0;
#endif
}

/*---------------------------------------------------------------------------*/

TARGET_HD static void Env_cuda_sync_threadblock()
{
#ifdef __CUDA_ARCH__
  __syncthreads();
#endif
}

/*===========================================================================*/

#ifdef __cplusplus
} /*---extern "C"---*/
#endif

#endif /*---_env_cuda_kernels_h_---*/

/*---------------------------------------------------------------------------*/