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
			printf("Error after %s (%d)\n", #call, res); \
			std::abort(); \
		} \
	} \


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

	HANDLE xiH = NULL;

	// Retrieving a handle to the camera device 
	printf("Opening first camera...\n");
	SAVECALL(xiOpenDevice(0, &xiH);)


		for (auto& [pname, tname] : all_params) {
			char result[512] = {};
			XI_RETURN res = xiGetParamString(xiH, pname, result, 512);
			std::cout << std::setw(5) << res << " " << std::setw(40) << tname << ": " << result << std::endl;
		}


	// Setting "exposure" parameter (10ms=10000us)
	SAVECALL(xiSetParamInt(xiH, XI_PRM_EXPOSURE, 10));


	const int im_width = 608;
	const int im_height = 608;
	//const int im_width = 384;
	//const int im_height = 384;

	SAVECALL(xiSetParamInt(xiH, XI_PRM_WIDTH, im_width));
	SAVECALL(xiSetParamInt(xiH, XI_PRM_HEIGHT, im_height));

	SAVECALL(xiSetParamInt(xiH, XI_PRM_GAIN, 1000000));

	// set software trigger:
	SAVECALL(xiSetParamInt(xiH, XI_PRM_TRG_SOURCE, XI_TRG_SOFTWARE));


	// Note:
	// The default parameters of each camera might be different in different API versions
	// In order to ensure that your application will have camera in expected state,
	// please set all parameters expected by your application to required value.

	printf("Starting acquisition...\n");
	SAVECALL(xiStartAcquisition(xiH));

	SAVECALL(xiSetParamInt(xiH, XI_PRM_TRG_SOFTWARE, 1));
	SAVECALL(xiGetImage(xiH, 5000, &image));

	auto last = std::chrono::steady_clock::now();

	constexpr int expected_images = 100000;

	long long last_image_num = 0;
	long long dropped_frames = 0;

	std::vector<long long> time_hist(1000, 0);
	std::vector<long long> px_h_hist(image.height, 0);
	std::vector<long long> px_w_hist(image.width, 0);

	constexpr bool do_moments = true;

	for (int images = 0; images < expected_images; images++)
	{
		SAVECALL(xiSetParamInt(xiH, XI_PRM_TRG_SOFTWARE, 1));
		SAVECALL(xiGetImage(xiH, 5000, &image));

		if constexpr (do_moments) {
			float px_sum = 0;
			float px_h_sum = 0;
			float px_w_sum = 0;

#pragma omp parallel for reduction(+:px_sum,px_w_sum,px_h_sum)
			for (int j = 0; j < im_height; ++j) {
				const float fj = static_cast<float>(j);
				const auto ptr = (unsigned char*)image.bp + j * im_width;
				float fi = 0;
				for (int i = 0; i < im_width; ++i) {
					float value = static_cast<float>(ptr[i]);
					px_sum += value;
					px_w_sum += value * fi;
					px_h_sum += value * fj;

					fi += 1;
				}
			}

			px_h_hist[static_cast<int>(px_h_sum / px_sum)] += 1;
			px_w_hist[static_cast<int>(px_w_sum / px_sum)] += 1;

			//printf("h %lld, w %lld\n", px_h_sum / px_sum, px_w_sum / px_sum);
		}

		auto current_time = std::chrono::steady_clock::now();
		auto delta = std::chrono::duration<double, std::micro>(current_time - last);
		last = current_time;

		time_hist[std::max(std::min(static_cast<int>(delta.count()), static_cast<int>(time_hist.size() - 1)), 0)] += 1;

		if (images > 0) {
			dropped_frames += std::max<long long>((image.nframe - last_image_num - 1), 0);
		}
		last_image_num = image.nframe;

		//printf("%f us, ptr %p nframe %d, tsSec %d, tsUSec %d  \n", delta.count(), image.bp, image.nframe, image.tsSec, image.tsUSec);
	}

	std::cout << "time_hist = " << time_hist << std::endl;
	if constexpr (do_moments) {
		std::cout << "px_h_hist = " << px_h_hist << std::endl;
		std::cout << "px_w_hist = " << px_w_hist << std::endl;
	}
	std::cout << "dropped_frames = " << dropped_frames << std::endl;

	printf("Stopping acquisition...\n");
	SAVECALL(xiStopAcquisition(xiH));
	SAVECALL(xiCloseDevice(xiH));
	printf("Done\n");

	return 0;
}
