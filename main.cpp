// xiSample.cpp : program that captures 10 images

#include "stdafx.h"

#ifdef WIN32
#include "xiApi.h"       // Windows
#else
#include <m3api/xiApi.h> // Linux, OSX
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


int _tmain(int argc, _TCHAR* argv[])
{
	// image buffer
	XI_IMG image;
	memset(&image, 0, sizeof(image));
	image.size = sizeof(XI_IMG);

	HANDLE xiH = NULL;

	// Retrieving a handle to the camera device 
	printf("Opening first camera...\n");
    SAVECALL(xiOpenDevice(0, &xiH);)
    
	char device_sn[256] = {};
	SAVECALL(xiGetDeviceInfoString(0, XI_PRM_DEVICE_SN, device_sn, 256));
	char device_ip[512] = {};
	SAVECALL(xiGetDeviceInfoString(0, XI_PRM_DEVICE_INSTANCE_PATH, device_ip, 512));
	char device_t[256] = {};
	SAVECALL(xiGetDeviceInfoString(0, XI_PRM_DEVICE_TYPE, device_t, 256));
	char device_n[256] = {};
	SAVECALL(xiGetDeviceInfoString(0, XI_PRM_DEVICE_NAME, device_n, 256));

	std::cout
		<< "XI_PRM_DEVICE_SN " << device_sn << '\n'
		<< "XI_PRM_DEVICE_INSTANCE_PATH " << device_ip << '\n'
		<< "XI_PRM_DEVICE_TYPE " << device_t << '\n'
		<< "XI_PRM_DEVICE_NAME " << device_n << '\n'
		<< std::endl;
    
	for (auto& [pname, tname] : all_params) {
		char result[512] = {};
		XI_RETURN res = xiGetParamString(xiH, pname, result, 512);
		std::cout << std::setw(5) << res << " " << std::setw(40) << tname << ": " << result << std::endl;
	}


	// Setting "exposure" parameter (10ms=10000us)
	SAVECALL(xiSetParamInt(xiH, XI_PRM_EXPOSURE, 50));


	const int im_width = 256;
	const int im_height = 256;

	SAVECALL(xiSetParamInt(xiH, XI_PRM_WIDTH, im_width));
	SAVECALL(xiSetParamInt(xiH, XI_PRM_HEIGHT, im_height));

	SAVECALL(xiSetParamInt(xiH, XI_PRM_GAIN, 1000000));

	// Note:
	// The default parameters of each camera might be different in different API versions
	// In order to ensure that your application will have camera in expected state,
	// please set all parameters expected by your application to required value.

	printf("Starting acquisition...\n");
	SAVECALL(xiStartAcquisition(xiH));

	SAVECALL(xiGetImage(xiH, 5000, &image));

	auto start_time = std::chrono::steady_clock::now();

	auto last = start_time;


	constexpr int expected_images = 1000000000;

	//std::vector<double> delta_time_vec(100, 0.);

	for (int images = 0; images < expected_images; images++)
	{
		// getting image from camera
		SAVECALL(xiGetImage(xiH, 5000, &image));
		//unsigned char pixel = *(unsigned char*)image.bp;

		auto current_time = std::chrono::steady_clock::now();

		auto duration = std::chrono::duration<double, std::micro>(current_time - start_time);
		auto delta = std::chrono::duration<double, std::micro>(current_time - last);

		last = current_time;

		unsigned char min_px = -1;
		unsigned char max_px = 1;

		for (int j = 0; j < (im_width * im_height); ++j) {
			min_px = std::min(min_px, ((unsigned char*)image.bp)[j]);
			max_px = std::max(max_px, ((unsigned char*)image.bp)[j]);
		}

		//std::cout << delta.count() << " us\n";
		if (images % 1 == 0) {
			printf("%f us, min %d, max %d\n", delta.count(), int{ min_px }, int{ max_px });
		}

		//delta_time_vec[images] = delta.count();

		//printf("Image %d (%dx%d) received from camera. First pixel value: %d; time: %f us delta: %f us\n",
		//	images, (int)image.width, (int)image.height, pixel, duration.count(), delta.count());
	}

	//for (int images = 0; images < expected_images; images++) {

	//	std::cout << delta_time_vec[images] << " us"  << std::endl;
	//}

	printf("Stopping acquisition...\n");
	SAVECALL(xiStopAcquisition(xiH));
	SAVECALL(xiCloseDevice(xiH));
	printf("Done\n");

	return 0;
}

