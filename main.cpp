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
#include "gpio.H"
#include "time.H"
#include "reductions.H"

#include <memory.h>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <map>
#include <iomanip>
#include <cmath>
#include <array>
#include <thread>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#define XIMEA_SAVECALL(call) \
    { \
        XI_RETURN res = call; \
        if (res != XI_OK) { \
            printf("XIMEA Error after %s (%d)\n", #call, res); \
            std::abort(); \
        } \
    }


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

    gpio_innit();

    //std::atomic<int> thread_stop = 0;
    //std::thread constant_trigger{trigger_thread, std::ref(thread_stop)};

    HANDLE xiH = NULL;

    // Retrieving a handle to the camera device
    printf("Opening first camera...\n");
    XIMEA_SAVECALL(xiOpenDevice(0, &xiH);)

    //for (auto& [pname, tname] : all_params) {
    //    char result[512] = {};
    //    XI_RETURN res = xiGetParamString(xiH, pname, result, 512);
    //    std::cout << std::setw(5) << res << " " << std::setw(40) << tname << ": " << result << std::endl;
    //}

    const int im_width = 608;
    const int im_height = 608;
    constexpr bool hw_trigger = false;

    XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_DOWNSAMPLING, XI_DWN_2x2));
    XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_DOWNSAMPLING_TYPE, XI_BINNING));

    XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_EXPOSURE, 10));
    XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_WIDTH, im_width));
    XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_HEIGHT, im_height));
    XIMEA_SAVECALL(xiSetParamFloat(xiH, XI_PRM_GAIN, 2.3F));
    if constexpr (!hw_trigger) {
        XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_TRG_SOURCE, XI_TRG_SOFTWARE));
        //XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_GPO_SELECTOR, XI_GPO_PORT3));
        //XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_GPO_MODE, XI_GPO_EXPOSURE_ACTIVE));
    } else {
        XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_GPI_SELECTOR, XI_GPI_PORT4));
        XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_GPI_MODE, XI_GPI_TRIGGER));
        XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_TRG_SOURCE, XI_TRG_EDGE_RISING));
        XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_TRG_OVERLAP, XI_TRG_OVERLAP_OFF));

        XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_DEBOUNCE_T0, 1));
        XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_DEBOUNCE_T1, 1));
    }

    XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_BUFFER_POLICY, XI_BP_UNSAFE));
    XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_IMAGE_DATA_FORMAT, XI_FRM_TRANSPORT_DATA));
    XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_OUTPUT_DATA_BIT_DEPTH, 8));
    XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_TRANSPORT_DATA_TARGET, XI_TRANSPORT_DATA_TARGET_ZEROCOPY));
    int image_size = 0;
    XIMEA_SAVECALL(xiGetParamInt(xiH, XI_PRM_IMAGE_PAYLOAD_SIZE, &image_size));
    XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_ACQ_BUFFER_SIZE, 10 * image_size));

    printf("Starting acquisition...\n");
    XIMEA_SAVECALL(xiStartAcquisition(xiH));

    XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_TRG_SOFTWARE, 1));
    while (xiGetImage(xiH, 1000, &image) == XI_OK) {}
    //XIMEA_SAVECALL(xiGetImage(xiH, 5000, &image));

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
            if constexpr (!hw_trigger) {
                XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_TRG_SOFTWARE, 1));
            }
            XIMEA_SAVECALL(xiGetImage(xiH, 5000, &image));
            calc_gpu((unsigned char*)image.bp, im_width, im_height, stream, cuda_mem_ptr,
                    px_h_hist, px_w_hist);
        }
    }

    px_h_hist.assign(px_h_hist.size(), 0.F);
    px_w_hist.assign(px_w_hist.size(), 0.F);

    for (int images = 0; images < expected_images; images++)
    {
        auto t1 = get_time();

        //nvtxRangePush("xiTrigSoftware");
        if constexpr (!hw_trigger) {
            XIMEA_SAVECALL(xiSetParamInt(xiH, XI_PRM_TRG_SOFTWARE, 1));
        }
        //nvtxRangePop();
        //nvtxRangePush("xiGetImage");
        XIMEA_SAVECALL(xiGetImage(xiH, 5000, &image));
        //nvtxRangePop();

        auto t2 = get_time();

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

        if constexpr (hw_trigger) {
            gpio_t2();
        }

        auto t3 = get_time();

        time_hist1[std::max(std::min(static_cast<int>(time_diff_us(t1, t2)), static_cast<int>(time_hist1.size() - 1)), 0)] += 1;

        time_hist2[std::max(std::min(static_cast<int>(time_diff_us(t1, t3)), static_cast<int>(time_hist2.size() - 1)), 0)] += 1;

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
    XIMEA_SAVECALL(xiStopAcquisition(xiH));
    XIMEA_SAVECALL(xiCloseDevice(xiH));
    printf("Done\n");

    //thread_stop.store(1);
    //constant_trigger.join();

    gpio_exit();

#ifdef __NVCC__
    CUDA_SAVECALL(cudaStreamDestroy(stream));
    CUDA_SAVECALL(cudaFree(cuda_mem_ptr));
#endif

    return 0;
}
