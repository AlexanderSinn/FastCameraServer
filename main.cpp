// xiSample.cpp : program that captures 10 images

#include "stdafx.h"

#ifdef WIN32
#include "xiApi.h"       // Windows
#else
#include <m3api/xiApi.h> // Linux, OSX
#endif

#ifdef _OPENMP
#include <omp.h>
#endif

#include "AllCameraParams.H"

#include <memory.h>
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <vector>
#include <map>
#include <iomanip>
#include <cmath>
#include <array>

#ifdef __NVCC__
#include <cub/cub.cuh>
#endif

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#define SAVECALL(call) \
    { \
        XI_RETURN res = call; \
        if (res != XI_OK) { \
            printf("XIMEA Error after %s (%d)\n", #call, res); \
            std::abort(); \
        } \
    }

#ifdef __NVCC__
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

template<class T>
std::ostream& operator<< (std::ostream& os, std::vector<T> const& vec) {
    os << '[';
    for (unsigned long long i = 0; i < vec.size(); ++i) {
        if (i != 0) {
            os << ", ";
        }
        os << vec[i];
    }
    os << ']';
    return os;
}

template<class T>
void print_percent(std::vector<T> vec) {
    T totsum = 0;
    for (auto& v : vec) {
        totsum += v;
    }

    std::cout << "fraction:";

    const std::array arr{0.5, 0.9, 0.99, 0.999};
    for (auto frac : arr) {
        T parsum = 0;
        for (int i = 0; i < vec.size(); ++i) {
            parsum += vec[i];
            if (parsum >= static_cast<T>(totsum * frac)) {
                std::cout << "\t" << i;
                break;
            }
        }
    }

    std::cout << std::endl;
}

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
__global__ void gpu_kernel(unsigned char * imdata, int im_width, int im_height, float * gloabl_agg)
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
    float aggw = BlockReduce(temp_storage).Sum(im_w);
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
              std::vector<long long>& px_h_hist, std::vector<long long>& px_w_hist) {
#ifdef __NVCC__
    gloabal_agg[0] = 0.F;
    gloabal_agg[1] = 0.F;
    gloabal_agg[2] = 0.F;
    dim3 blockDim(32, 16);
    dim3 gridDim;
    gridDim.x = ((im_width+3)/4 + blockDim.x - 1)/blockDim.x;
    gridDim.y = (im_height + blockDim.y - 1)/blockDim.y;
    gpu_kernel<<<gridDim, blockDim, 0, stream>>>(imdata, im_width/4, im_height, gloabal_agg);
    CUDA_SAVECALL(cudaStreamSynchronize(stream));

    px_w_hist[static_cast<int>(gloabal_agg[1] / gloabal_agg[0])] += 1;
    px_h_hist[static_cast<int>(gloabal_agg[2] / gloabal_agg[0])] += 1;
#endif
    (void)imdata, (void)im_width, (void)im_height;
}


inline
void print_image(XI_IMG& image) {
    std::cout << "size " << image.size << '\n';
    std::cout << "bp " << image.bp << '\n';
    std::cout << "bp_size " << image.bp_size << '\n';
    std::cout << "frm " << image.frm << '\n';
    std::cout << "width " << image.width << '\n';
    std::cout << "height " << image.height << '\n';
    std::cout << "nframe " << image.nframe << '\n';
    std::cout << "tsSec " << image.tsSec << '\n';
    std::cout << "tsUSec " << image.tsUSec << '\n';
    std::cout << "GPI_level " << image.GPI_level << '\n';
    std::cout << "black_level " << image.black_level << '\n';
    std::cout << "padding_x " << image.padding_x << '\n';
    std::cout << "AbsoluteOffsetX " << image.AbsoluteOffsetX << '\n';
    std::cout << "AbsoluteOffsetY " << image.AbsoluteOffsetY << '\n';
    std::cout << "transport_frm " << image.transport_frm << '\n';
    std::cout << "gain_db " << image.gain_db << '\n';


    float sum = 0.;

    for (int j = 0; j != image.height; ++j) {
        for (int i = 0; i != image.width; ++i) {
            sum += static_cast<float>(((unsigned char*)image.bp)[i + j * image.width]);
        }
    }

    sum /= image.height * image.width;

    for (int j = 0; j != image.height; ++j) {
        for (int i = 0; i != image.width; ++i) {
            std::cout << (static_cast<float>(((unsigned char*)image.bp)[i + j * image.width]) > sum ? '#' : '-');
        }
        std::cout << '\n';
    }
    std::cout << '\n';
}


int _tmain(int argc, _TCHAR* argv[])
{
    // initialize omp
#ifdef _OPENMP
    omp_set_num_threads(8);
#pragma omp parallel
    {
#pragma omp critical
        printf("Initialized omp thread %d\n", omp_get_thread_num());
    }
#endif

    //std::cout << "Press any key to start..." << std::endl;
    //char temp_char = 0;
    //std::cin >> temp_char;

    // image buffer
    XI_IMG image;
    memset(&image, 0, sizeof(image));
    image.size = SIZE_XI_IMG_V2;
    image.size = sizeof(image);

    float * cuda_mem_ptr = nullptr;
#ifdef __NVCC__
    CUDA_SAVECALL(cudaMallocManaged(&cuda_mem_ptr, sizeof(float) * 3));
    int priority_low = 0;
    int priority_high = 0;
    CUDA_SAVECALL(cudaDeviceGetStreamPriorityRange(&priority_low, &priority_high));
    cudaStream_t stream;
    CUDA_SAVECALL(cudaStreamCreateWithPriority(&stream, cudaStreamNonBlocking, priority_high));
#else
    cudaStream_t stream = 0;
#endif

    HANDLE xiH = NULL;

    // Retrieving a handle to the camera device
    printf("Opening first camera...\n");
    SAVECALL(xiOpenDevice(0, &xiH);)


    //for (auto& [pname, tname] : all_params) {
    //    char result[512] = {};
    //    XI_RETURN res = xiGetParamString(xiH, pname, result, 512);
    //    std::cout << std::setw(5) << res << " " << std::setw(40) << tname << ": " << result << std::endl;
    //}

    //const int im_width = 608;
    //const int im_height = 608;
    //const int im_width = 384;
    //const int im_height = 384;
    const int im_width = 608;
    const int im_height = 608;

    SAVECALL(xiSetParamInt(xiH, XI_PRM_DOWNSAMPLING, XI_DWN_2x2));
    SAVECALL(xiSetParamInt(xiH, XI_PRM_DOWNSAMPLING_TYPE, XI_BINNING));

    SAVECALL(xiSetParamInt(xiH, XI_PRM_EXPOSURE, 10));
    SAVECALL(xiSetParamInt(xiH, XI_PRM_WIDTH, im_width));
    SAVECALL(xiSetParamInt(xiH, XI_PRM_HEIGHT, im_height));
    SAVECALL(xiSetParamFloat(xiH, XI_PRM_GAIN, 2.3F));
    SAVECALL(xiSetParamInt(xiH, XI_PRM_TRG_SOURCE, XI_TRG_SOFTWARE));
    //SAVECALL(xiSetParamInt(xiH, XI_PRM_BUFFER_POLICY, XI_BP_SAFE));

    SAVECALL(xiSetParamInt(xiH, XI_PRM_BUFFER_POLICY, XI_BP_UNSAFE));
    SAVECALL(xiSetParamInt(xiH, XI_PRM_IMAGE_DATA_FORMAT, XI_FRM_TRANSPORT_DATA));
    SAVECALL(xiSetParamInt(xiH, XI_PRM_OUTPUT_DATA_BIT_DEPTH, 8));
    SAVECALL(xiSetParamInt(xiH, XI_PRM_TRANSPORT_DATA_TARGET, XI_TRANSPORT_DATA_TARGET_ZEROCOPY));
    int image_size = 0;
    SAVECALL(xiGetParamInt(xiH, XI_PRM_IMAGE_PAYLOAD_SIZE, &image_size));
    SAVECALL(xiSetParamInt(xiH, XI_PRM_ACQ_BUFFER_SIZE, 10 * image_size));

    printf("Starting acquisition...\n");
    SAVECALL(xiStartAcquisition(xiH));

    SAVECALL(xiSetParamInt(xiH, XI_PRM_TRG_SOFTWARE, 1));
    while (xiGetImage(xiH, 1000, &image) == XI_OK) {}
    //SAVECALL(xiGetImage(xiH, 5000, &image));

    //print_image(image);

    int ntiles_h = 20;
    int ntiles_w = 20;

    constexpr int expected_images = 100000;

    long long last_image_num = 0;
    long long dropped_frames = 0;
    long long duplicate_frames = 0;

    std::vector<long long> time_hist1(2000, 0);
    std::vector<long long> time_hist2(2000, 0);

    std::vector<long long> px_h_hist(image.height, 0);
    std::vector<long long> px_w_hist(image.width, 0);

    auto tile_start_h = make_tile_start(ntiles_h, image.height);
    auto tile_start_w = make_tile_start(ntiles_w, image.width);

    std::vector<float> tile_sum_h(ntiles_h * ntiles_w, 0.F);
    std::vector<float> tile_sum_w(ntiles_h * ntiles_w, 0.F);

    constexpr bool do_moments = false;
    constexpr bool do_tiles = false;
    constexpr bool do_gpu_moments = true;

    if constexpr (do_gpu_moments) {
        for (int i=0; i<1000; ++i) {
            // warm up
            SAVECALL(xiSetParamInt(xiH, XI_PRM_TRG_SOFTWARE, 1));
            SAVECALL(xiGetImage(xiH, 5000, &image));
            calc_gpu((unsigned char*)image.bp, im_width, im_height, stream, cuda_mem_ptr,
                    px_h_hist, px_w_hist);
        }
    }

    px_h_hist.assign(px_h_hist.size(), 0.F);
    px_w_hist.assign(px_w_hist.size(), 0.F);

    for (int images = 0; images < expected_images; images++)
    {
        auto t1 = std::chrono::steady_clock::now();

        //nvtxRangePush("xiTrigSoftware");
        SAVECALL(xiSetParamInt(xiH, XI_PRM_TRG_SOFTWARE, 1));
        //nvtxRangePop();
        //nvtxRangePush("xiGetImage");
        SAVECALL(xiGetImage(xiH, 5000, &image));
        //nvtxRangePop();

        auto t2 = std::chrono::steady_clock::now();

        if constexpr (do_moments) {
            calc_moments(im_height, im_width, (unsigned char*)image.bp, px_h_hist, px_w_hist);
        }

        if constexpr (do_tiles) {
            calc_tile(ntiles_h, ntiles_w, (unsigned char*)image.bp, tile_start_h, tile_start_w,
                      tile_sum_w, tile_sum_h, im_width);
        }

        if constexpr (do_gpu_moments) {
            //nvtxRangePush("calc_gpu");
            calc_gpu((unsigned char*)image.bp, im_width, im_height, stream, cuda_mem_ptr,
                     px_h_hist, px_w_hist);
            //nvtxRangePop();
        }

        auto t3 = std::chrono::steady_clock::now();

        {
            auto delta = std::chrono::duration<double, std::micro>(t2 - t1);
            time_hist1[std::max(std::min(static_cast<int>(delta.count()), static_cast<int>(time_hist1.size() - 1)), 0)] += 1;
        }

        {
            auto delta = std::chrono::duration<double, std::micro>(t3 - t1);
            time_hist2[std::max(std::min(static_cast<int>(delta.count()), static_cast<int>(time_hist2.size() - 1)), 0)] += 1;
        }

        if (images > 0) {
            dropped_frames += std::max<long long>((image.nframe - last_image_num - 1), 0);
            duplicate_frames += std::max<long long>(-(image.nframe - last_image_num - 1), 0);
        }
        last_image_num = image.nframe;

    }

    std::cout << "time_hist1 = " << time_hist1 << std::endl;
    print_percent(time_hist1);
    std::cout << "time_hist2 = " << time_hist2 << std::endl;
    print_percent(time_hist2);

    if constexpr (do_moments || do_gpu_moments) {
        std::cout << "px_h_hist = " << px_h_hist << std::endl;
        std::cout << "px_w_hist = " << px_w_hist << std::endl;
    }

    if constexpr (do_tiles) {
        std::cout << "tile_sum_h = " << tile_sum_h << std::endl;
        std::cout << "tile_sum_w = " << tile_sum_w << std::endl;
    }

    std::cout << "dropped_frames = " << dropped_frames << std::endl;
    std::cout << "duplicate_frames = " << duplicate_frames << std::endl;

    printf("Stopping acquisition...\n");
    SAVECALL(xiStopAcquisition(xiH));
    SAVECALL(xiCloseDevice(xiH));
    printf("Done\n");

#ifdef __NVCC__
    CUDA_SAVECALL(cudaStreamDestroy(stream));
    CUDA_SAVECALL(cudaFree(cuda_mem_ptr));
#endif

    return 0;
}
