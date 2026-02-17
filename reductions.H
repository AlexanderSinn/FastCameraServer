#pragma once

#include <vector>

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
#define cudaStream_t int
#endif


static
void calc_moments(int im_height, int im_width, unsigned char * imager_ptr,
                  std::vector<long long>& px_h_hist, std::vector<long long>& px_w_hist) {
    float px_sum = 0;
    float px_h_sum = 0;
    float px_w_sum = 0;

#ifdef _OPENMP
#pragma omp parallel for reduction(+:px_sum,px_w_sum,px_h_sum)
#endif
    for (int j = 0; j < im_height; ++j) {
        const float fj = static_cast<float>(j);
        const auto ptr = imager_ptr + j * im_width;
        float fi = 0;
        for (int i = 0; i < im_width; ++i) {
            float value = static_cast<float>(ptr[i]);
            px_sum += value;
            px_w_sum += value * fi;
            px_h_sum += value * fj;

            fi += 1.F;
        }
    }

    px_h_hist[static_cast<int>(px_h_sum / px_sum)] += 1;
    px_w_hist[static_cast<int>(px_w_sum / px_sum)] += 1;

    //printf("CPU %f %f %f\n", (double)px_sum, (double)px_h_sum, (double)px_w_sum);
}

static
std::vector<int> make_tile_start(int nt, int imsize)
{
    std::vector<int> ret(nt + 1, 0);
    for (int i = 0; i < nt + 1; ++i) {
        ret[i] = (i * imsize) / nt;
    }
    return ret;
}

static
void calc_tile(int nt_h, int nt_w, unsigned char* imager_ptr,
    std::vector<int> const& tile_start_h, std::vector<int> const& tile_start_w,
    std::vector<float>& tile_sum_h, std::vector<float>& tile_sum_w, int im_width)
{
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (int it = 0; it < nt_w*nt_h; ++it) {
        int it_h = it / nt_w;
        int it_w = it - nt_w * it_h;

        float px_sum = 0;
        float px_h_sum = 0;
        float px_w_sum = 0;

        int tbegin_h = tile_start_h[it_h];
        int tbegin_w = tile_start_w[it_w];
        int tend_h = tile_start_h[it_h + 1];
        int tend_w = tile_start_w[it_w + 1];

        for (int j = tbegin_h; j < tend_h; ++j) {
            const float fj = static_cast<float>(j);
            const auto ptr = imager_ptr + j * im_width;
            float fi = static_cast<float>(tbegin_w);
            for (int i = tbegin_w; i < tend_w; ++i) {
                float value = static_cast<float>(ptr[i]);
                px_sum += value;
                px_w_sum += value * fi;
                px_h_sum += value * fj;

                fi += 1;
            }
        }

        tile_sum_w[it] += px_w_sum / px_sum;
        tile_sum_h[it] += px_h_sum / px_sum;
    }
}

#ifdef __NVCC__
__launch_bounds__(512)
__global__ void gpu_kernel(unsigned char * imdata, int im_width, int im_height, float * gloabl_agg,
                           float cutoff_value)
{
    using BlockReduce = cub::BlockReduce<float, 32, cub::BLOCK_REDUCE_WARP_REDUCTIONS, 16>;

    __shared__ typename BlockReduce::TempStorage temp_storage;

    int tidw = threadIdx.x + blockIdx.x * blockDim.x;
    int tidh = threadIdx.y + blockIdx.y * blockDim.y;

    float im_value[4] = {0.F, 0.F, 0.F, 0.F};

    if (tidw < im_width && tidh < im_height) {
        auto pack_im = reinterpret_cast<uchar4*>(imdata);
        uchar4 packed_data = pack_im[tidw + tidh * im_width];
        im_value[0] = static_cast<float>(packed_data.x);
        im_value[1] = static_cast<float>(packed_data.y);
        im_value[2] = static_cast<float>(packed_data.z);
        im_value[3] = static_cast<float>(packed_data.w);
    }

    im_value[0] = im_value[0] >= cutoff_value ? im_value[0] : 0.F;
    im_value[1] = im_value[1] >= cutoff_value ? im_value[1] : 0.F;
    im_value[2] = im_value[2] >= cutoff_value ? im_value[2] : 0.F;
    im_value[3] = im_value[3] >= cutoff_value ? im_value[3] : 0.F;

    float f_tidw = static_cast<float>(4*tidw);
    float im_w = (
        im_value[0] * f_tidw +
        im_value[1] * (f_tidw + 1.F) +
        im_value[2] * (f_tidw + 2.F) +
        im_value[3] * (f_tidw + 3.F)
    );
    float im_h = static_cast<float>(tidh) * (
        im_value[0] +
        im_value[1] +
        im_value[2] +
        im_value[3]
    );

    float aggs = BlockReduce(temp_storage).Sum(im_value);
    __syncthreads();
    float aggw = BlockReduce(temp_storage).Sum(im_w);
    __syncthreads();
    float aggh = BlockReduce(temp_storage).Sum(im_h);

    if (threadIdx.x == 0 && threadIdx.y == 0) {
        atomicAdd(gloabl_agg + 0, aggs);
        atomicAdd(gloabl_agg + 1, aggw);
        atomicAdd(gloabl_agg + 2, aggh);
    }
}
#endif

void calc_gpu(unsigned char * imdata, int im_width, int im_height,
              cudaStream_t& stream, float * gloabal_agg,
              std::vector<long long>& px_h_hist, std::vector<long long>& px_w_hist,
              float cutoff_value) {
#ifdef __NVCC__
    gloabal_agg[0] = 0.F;
    gloabal_agg[1] = 0.F;
    gloabal_agg[2] = 0.F;
    dim3 blockDim(32, 16);
    dim3 gridDim;
    gridDim.x = ((im_width+3)/4 + blockDim.x - 1)/blockDim.x;
    gridDim.y = (im_height + blockDim.y - 1)/blockDim.y;
    gpu_kernel<<<gridDim, blockDim, 0, stream>>>(imdata, im_width/4, im_height, gloabal_agg,
                                                 cutoff_value);
    CUDA_SAVECALL(cudaStreamSynchronize(stream));

    if (gloabal_agg[0] > 0.F) {
        px_w_hist[static_cast<int>(gloabal_agg[1] / gloabal_agg[0])] += 1;
        px_h_hist[static_cast<int>(gloabal_agg[2] / gloabal_agg[0])] += 1;
    }

    //printf("GPU %f %f %f\n", (double)gloabal_agg[0], (double)gloabal_agg[2], (double)gloabal_agg[1]);
#endif
    (void)imdata, (void)im_width, (void)im_height;
}
