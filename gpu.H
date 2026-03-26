#pragma once

#include <cstddef>
#include <cstring>

#ifdef __NVCC__

#include <cub/cub.cuh>

#define CUDA_SAVECALL(call) \
    { \
        cudaError_t res = call; \
        if (res != cudaSuccess) { \
            printf("CUDA Error after %s (%s)\n", #call, cudaGetErrorString(res)); \
            std::abort(); \
        } \
    }

#else

#define CUDA_SAVECALL(call) call

#define cudaStream_t int

#define __device__
#define __forceinline__

#define cudaMalloc [](void**p, std::size_t s){ *p = std::malloc(s); }
#define cudaMallocHost [](void**p, std::size_t s){ *p = std::malloc(s); }

#define cudaFree std::free
#define cudaFreeHost std::free

#define cudaMemcpyAsync(dst, src, count, kind, stream) std:memcpy(dst, src, count)

#define cudaStreamSynchronize(stream) (void)0

#endif
