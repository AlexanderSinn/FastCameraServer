#include "laser_params.H"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>

struct MaxLoc2D {
    float max_val = -300;
    int loc_x_lo = -1;
    int loc_x_hi = -1;
    int loc_y_lo = -1;
    int loc_y_hi = -1;
};

struct MaxLoc2DOp {
    __device__ __forceinline__
    MaxLoc2D operator() (const MaxLoc2D& a, const MaxLoc2D& b) {
        if (a.max_val > b.max_val) {
            return a;
        } else if (a.max_val < b.max_val) {
            return b;
        } else {
            return {
                a.max_val,
                std::min(a.loc_x_lo, b.loc_x_lo),
                std::max(a.loc_x_hi, b.loc_x_hi),
                std::min(a.loc_y_lo, b.loc_y_lo),
                std::max(a.loc_y_hi, b.loc_y_hi)
            };
        }
    }
};

struct SumMoments {
    float sum = 0;
    float sum_clip = 0;

    float moment_x = 0;
    float moment_y = 0;

    float moment_xx = 0;
    float moment_yy = 0;

    float clip_area = 0;

    float sum_sq_clip = 0;

    float clip_area_low = 0;
    float clip_area_hi = 0;
};


struct SumMomentsOp {
    __device__ __forceinline__
    SumMoments operator() (const SumMoments& a, const SumMoments& b) {
        return {
            a.sum + b.sum,
            a.sum_clip + b.sum_clip,
            a.moment_x + b.moment_x,
            a.moment_y + b.moment_y,
            a.moment_xx + b.moment_xx,
            a.moment_yy + b.moment_yy,
            a.clip_area + b.clip_area,
            a.sum_sq_clip + b.sum_sq_clip,
            a.clip_area_low + b.clip_area_low,
            a.clip_area_hi + b.clip_area_hi
        };
    }
};





template <class T, class U, class F>
void Reduce2D (int nx, int ny, T* out, F f, U u, void*&p, std::size_t& sz, cudaStream_t stream) {

#ifdef __NVCC__
    thrust::counting_iterator<int> index_sequence_begin(0);

    auto call_f = [=] __device__ (int ii) -> T {
        int j = ii / nx;
        int i = ii - j * nx;
        return f(i, j);
    };

    if (p == nullptr) {
        cub::DeviceReduce::TransformReduce(
            p,
            sz,
            index_sequence_begin,
            out,
            nx * ny,
            u,
            call_f,
            T{},
            stream);

        cudaMalloc(&p, sz);
    }

    cub::DeviceReduce::TransformReduce(
        p,
        sz,
        index_sequence_begin,
        out,
        nx * ny,
        u,
        call_f,
        T{},
        stream);

#else
    T ret{};
    for (int j=0; j<ny; ++j) {
        for (int i=0; i<nx; ++i) {
            ret = u(f(i, j), ret);
        }
    }
    *out = ret;
#endif
}

#ifdef __NVCC__
__launch_bounds__(256)
__global__ void histogramm_kernel (
    int im_width, int im_height,
    unsigned char * imdata,
    unsigned char * bgdata,
    unsigned int * hist
)
{
    constexpr unsigned char low_clip = 10;
    __shared__ unsigned int shared_hist[256];

    shared_hist[threadIdx.x] = 0;

    __syncthreads();

    const int griddim = gridDim.x * 256;

    for (int i=threadIdx.x + 256 * blockIdx.x; i < im_width*im_height; i += griddim) {
        auto im_val = imdata[i];
        auto bg_val = bgdata[i];

        if (im_val >= bg_val + low_clip) {
            atomicAdd(shared_hist + (im_val - bg_val), 1);
        }
    }

    __syncthreads();

    if (shared_hist[threadIdx.x] != 0) {
        atomicAdd(hist + threadIdx.x, shared_hist[threadIdx.x]);
    }
}

void launch_hist (int im_width, int im_height,
    unsigned char * imdata,
    unsigned char * bgdata,
    unsigned int * hist,
    cudaStream_t stream
) {
    int griddim = std::min(256,
        (im_width * im_height + 255)/256
    );
    histogramm_kernel<<<griddim, 256, 0, stream>>>(im_width, im_height, imdata, bgdata, hist);
}

#else

void launch_hist (int im_width, int im_height,
    unsigned char * imdata,
    unsigned char * bgdata,
    unsigned int * hist,
    cudaStream_t stream
) {}

#endif

float fwhm_kernel (const unsigned int * hist)
{
    const float minimal_max_value = 10.F;

    unsigned int max_value = 0.F;
    unsigned int max_value_half = 0.F;
    int max_pos = 0;
    int half_pos_lo = 0;
    int half_pos_hi = 255;

    int i = 255;

    while (i>=0) {
        if (hist[i] >= minimal_max_value) {
            max_value = hist[i];
            max_value_half = 0.5F * max_value;
            max_pos = i;
            break;
        }
        --i;
    }

    while (i>=0) {
        if (hist[i] > max_value) {
            max_value = hist[i];
            max_value_half = 0.5F * max_value;
            max_pos = i;
        } else if (hist[i] <= max_value_half) {
            half_pos_lo = i;
            break;
        }
        --i;
    }

    i = max_pos;
    while (i<256) {
        if (hist[i] <= max_value_half) {
            half_pos_hi = i;
            break;
        }
        ++i;
    }

    return half_pos_hi - half_pos_lo;
}


LaserCharacterizingParameters characterize_laser (
    int im_height, int im_width, TempMemory& mem, cudaStream_t stream
)
{
    if (mem.background_ptr == nullptr) {
        CUDA_SAVECALL(cudaMalloc(
            reinterpret_cast<void**>(&mem.background_ptr), im_height * im_width));

        CUDA_SAVECALL(cudaMemcpyAsync(
            mem.background_ptr,
            mem.image_ptr,
            im_height * im_width,
            cudaMemcpyDeviceToDevice,
            stream
        ));
    }

    if (mem.h_maxlocout == nullptr) {
        CUDA_SAVECALL(cudaMallocHost(&mem.h_maxlocout, sizeof(MaxLoc2D)));
    }

    if (mem.h_hist == nullptr) {
        CUDA_SAVECALL(cudaMallocHost(
            reinterpret_cast<void**>(&mem.h_hist), 256 * sizeof(unsigned int)));
    }

    for (int i=0; i<256; ++i) {
        mem.h_hist[i] = 0;
    }

    auto image_ptr = mem.image_ptr;
    auto background_ptr = mem.background_ptr;
    auto d_maxlocout = reinterpret_cast<MaxLoc2D*>(mem.h_maxlocout);

    Reduce2D(im_width, im_height, d_maxlocout,
        [=] __device__ (int w, int h) -> MaxLoc2D {
            float im_val = static_cast<float>(image_ptr[w + im_width * h]);
            float bg_val = static_cast<float>(background_ptr[w + im_width * h]);

            return MaxLoc2D {
                im_val - bg_val,
                w, w, h, h
            };
        }, MaxLoc2DOp{}, mem.preduction1, mem.sreduction1, stream);

    if (mem.h_sumout == nullptr) {
        CUDA_SAVECALL(cudaMallocHost(&mem.h_sumout, sizeof(SumMoments)));
    }

    float background_clip_level = 0.01;
    float main_clip_level = 0.3;
    float edge_clip_lo = 0.1;
    float edge_clip_hi = 0.9;
    auto d_sumout = reinterpret_cast<SumMoments*>(mem.h_sumout);

    Reduce2D(im_width, im_height, d_sumout,
        [=] __device__ (int w, int h) -> SumMoments {
            float im_val = static_cast<float>(image_ptr[w + im_width * h]);
            float bg_val = static_cast<float>(background_ptr[w + im_width * h]);

            im_val -= bg_val;

            float max_imval = d_maxlocout->max_val;

            float im_tot = im_val >= max_imval * background_clip_level ? im_val : 0.F;
            float xp = static_cast<float>(w);
            float yp = static_cast<float>(h);

            return SumMoments{
                im_tot,
                im_val >= max_imval * main_clip_level ? im_val : 0.F,
                im_tot * xp,
                im_tot * yp,
                im_tot * xp * xp,
                im_tot * yp * yp,
                im_val >= max_imval * main_clip_level ? 1.F : 0.F,
                im_val >= max_imval * main_clip_level ? im_tot * im_tot : 0.F,
                im_val >= max_imval * edge_clip_lo ? 1.F : 0.F,
                im_val >= max_imval * edge_clip_hi ? 1.F : 0.F
            };
        }, SumMomentsOp{}, mem.preduction2, mem.sreduction2, stream);

    launch_hist(im_width, im_height, image_ptr, background_ptr,
        mem.h_hist, stream);

    CUDA_SAVECALL(cudaStreamSynchronize(stream));

    LaserCharacterizingParameters ret{};

    ret.background_clip_level = background_clip_level;
    ret.main_clip_level = main_clip_level;
    ret.edge_clip_lo = edge_clip_lo;
    ret.edge_clip_hi = edge_clip_hi;

    const MaxLoc2D maxloc = *reinterpret_cast<MaxLoc2D*>(mem.h_maxlocout);
    const SumMoments summom = *reinterpret_cast<SumMoments*>(mem.h_sumout);
    const float fwhm = fwhm_kernel(mem.h_hist);

    ret.max_energy = maxloc.max_val;
    ret.max_energy_loc_x = (maxloc.loc_x_lo + maxloc.loc_x_hi) * 0.5F;
    ret.max_energy_loc_y = (maxloc.loc_y_lo + maxloc.loc_y_hi) * 0.5F;

    ret.full_energy = summom.sum;
    ret.clip_level_energy = summom.sum_clip;
    float full_energy_inv = ret.full_energy > 0.F ? 1.F / ret.full_energy : 0.F;
    ret.fractional_energy = ret.clip_level_energy * full_energy_inv;

    ret.centroid_x = summom.moment_x * full_energy_inv;
    ret.centroid_y = summom.moment_y * full_energy_inv;

    ret.width_sigma_x = 4.F * std::sqrt( std::max(0.F, summom.moment_xx * full_energy_inv -
        (summom.moment_x * full_energy_inv) * (summom.moment_x * full_energy_inv)));

    ret.width_sigma_y = 4.F * std::sqrt( std::max(0.F, summom.moment_yy * full_energy_inv -
        (summom.moment_y * full_energy_inv) * (summom.moment_y * full_energy_inv)));

    ret.ellipticity = std::max(ret.width_sigma_x, ret.width_sigma_y) > 0.F ?
        std::min(ret.width_sigma_x, ret.width_sigma_y) /
            std::max(ret.width_sigma_x, ret.width_sigma_y) : 0.F;

    ret.cross_section_area = (3.14159265358979323846F / 4.F) *
        ret.width_sigma_x * ret.width_sigma_y;

    ret.clip_level_irradiation_area = summom.clip_area;

    ret.clip_level_average_power = ret.clip_level_irradiation_area > 0.F ?
        ret.clip_level_energy / ret.clip_level_irradiation_area : 0.F;

    ret.flatness_factor = ret.max_energy > 0.F ?
        ret.clip_level_average_power / ret.max_energy : 0.F;

    ret.beam_uniformity =
        (summom.clip_area > 0.F && ret.clip_level_average_power > 0.F) ?
        std::sqrt(std::max( 0.F, summom.sum_sq_clip / summom.clip_area -
            ret.clip_level_average_power * ret.clip_level_average_power)
            ) / ret.clip_level_average_power
        : 0.F;

    ret.plateau_uniformity = fwhm;

    ret.edge_steepness = summom.clip_area_low > 0.F ?
        (summom.clip_area_low - summom.clip_area_hi) / summom.clip_area_low :
        1.F;

    return ret;
}

void free_temp_memory (TempMemory& mem)
{
    if (mem.background_ptr != nullptr) {
        CUDA_SAVECALL(cudaFree(mem.background_ptr));
    }

    if (mem.h_maxlocout != nullptr) {
        CUDA_SAVECALL(cudaFreeHost(mem.h_maxlocout));
    }

    if (mem.h_sumout != nullptr) {
        CUDA_SAVECALL(cudaFreeHost(mem.h_sumout));
    }

    if (mem.preduction1 != nullptr) {
        CUDA_SAVECALL(cudaFree(mem.preduction1));
    }

    if (mem.preduction2 != nullptr) {
        CUDA_SAVECALL(cudaFree(mem.preduction2));
    }

    if (mem.h_hist != nullptr) {
        CUDA_SAVECALL(cudaFreeHost(mem.h_hist));
    }
}

void print_params (const LaserCharacterizingParameters& params)
{
    return;
    std::stringstream ss{};
    ss << std::setw(40) << "max_energy    " << params.max_energy << '\n';
    ss << std::setw(40) << "max_energy_loc_x    " << params.max_energy_loc_x << '\n';
    ss << std::setw(40) << "max_energy_loc_y    " << params.max_energy_loc_y << '\n';
    ss << std::setw(40) << "main_clip_level    " << params.main_clip_level << '\n';
    ss << std::setw(40) << "background_clip_level    " << params.background_clip_level << '\n';
    ss << std::setw(40) << "full_energy    " << params.full_energy << '\n';
    ss << std::setw(40) << "clip_level_energy    " << params.clip_level_energy << '\n';
    ss << std::setw(40) << "fractional_energy    " << params.fractional_energy << '\n';
    ss << std::setw(40) << "centroid_x    " << params.centroid_x << '\n';
    ss << std::setw(40) << "centroid_y    " << params.centroid_y << '\n';
    ss << std::setw(40) << "width_sigma_x    " << params.width_sigma_x << '\n';
    ss << std::setw(40) << "width_sigma_y    " << params.width_sigma_y << '\n';
    ss << std::setw(40) << "ellipticity    " << params.ellipticity << '\n';
    ss << std::setw(40) << "cross_section_area    " << params.cross_section_area << '\n';
    ss << std::setw(40) << "clip_level_irradiation_area    " << params.clip_level_irradiation_area << '\n';
    ss << std::setw(40) << "clip_level_average_power    " << params.clip_level_average_power << '\n';
    ss << std::setw(40) << "flatness_factor    " << params.flatness_factor << '\n';
    ss << std::setw(40) << "beam_uniformity    " << params.beam_uniformity << '\n';
    ss << std::setw(40) << "plateau_uniformity    " << params.plateau_uniformity << '\n';
    ss << std::setw(40) << "edge_clip_lo    " << params.edge_clip_lo << '\n';
    ss << std::setw(40) << "edge_clip_hi    " << params.edge_clip_hi << '\n';
    ss << std::setw(40) << "edge_steepness    " << params.edge_steepness << '\n';
    std::cout << ss.str() << std::endl;
}