#pragma once

#include <cstddef>

#include "gpu.H"

struct LaserCharacterizingParameters {

    float max_energy = -1;
    float max_energy_loc_x = -1;
    float max_energy_loc_y = -1;

    float main_clip_level = -1; // INPUT
    float background_clip_level = -1; // INPUT

    float full_energy = -1;
    float clip_level_energy = -1;
    float fractional_energy = -1;

    float centroid_x = -1;
    float centroid_y = -1;

    float width_sigma_x = -1;
    float width_sigma_y = -1;

    float ellipticity = -1;

    float cross_section_area = -1;

    float clip_level_irradiation_area = -1;

    float clip_level_average_power = -1;

    float flatness_factor = -1;

    float beam_uniformity = -1;

    float plateau_uniformity = -1;

    float edge_clip_lo = 0.1; // INPUT
    float edge_clip_hi = 0.9; // INPUT
    float edge_steepness = -1;

};

struct TempMemory {
    unsigned char * image_ptr = nullptr;
    unsigned char * background_ptr = nullptr;

    void * h_maxlocout = nullptr;

    void * h_sumout = nullptr;

    void * preduction1 = nullptr;
    std::size_t sreduction1 = 0;

    void * preduction2 = nullptr;
    std::size_t sreduction2 = 0;

    unsigned int * h_hist = nullptr;
};


LaserCharacterizingParameters characterize_laser (
    int im_height, int im_width, TempMemory& mem, cudaStream_t stream
);


void free_temp_memory (TempMemory& mem);

void print_params (const LaserCharacterizingParameters& params);

