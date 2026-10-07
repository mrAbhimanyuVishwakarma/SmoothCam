// Minimal CUDA driver API declarations - just the types the NVIDIA Optical Flow
// headers reference and the handful of functions SmoothCam loads at runtime from
// nvcuda.dll (part of every NVIDIA display driver). This lets us build without
// the CUDA Toolkit and ship without any NVIDIA binaries.
//
// Types and signatures follow the public CUDA driver API (stable since CUDA 4).

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int CUresult;
typedef int CUdevice;
typedef unsigned long long CUdeviceptr;
typedef struct CUctx_st* CUcontext;
typedef struct CUarray_st* CUarray;
typedef struct CUstream_st* CUstream;
typedef struct CUgraphicsResource_st* CUgraphicsResource;

#define CUDA_SUCCESS 0

typedef enum CUmemorytype_enum
{
    CU_MEMORYTYPE_HOST = 1,
    CU_MEMORYTYPE_DEVICE = 2,
    CU_MEMORYTYPE_ARRAY = 3,
    CU_MEMORYTYPE_UNIFIED = 4
} CUmemorytype;

typedef struct CUDA_MEMCPY2D_st
{
    size_t srcXInBytes;
    size_t srcY;
    CUmemorytype srcMemoryType;
    const void* srcHost;
    CUdeviceptr srcDevice;
    CUarray srcArray;
    size_t srcPitch;

    size_t dstXInBytes;
    size_t dstY;
    CUmemorytype dstMemoryType;
    void* dstHost;
    CUdeviceptr dstDevice;
    CUarray dstArray;
    size_t dstPitch;

    size_t WidthInBytes;
    size_t Height;
} CUDA_MEMCPY2D;

#ifdef __cplusplus
}
#endif
