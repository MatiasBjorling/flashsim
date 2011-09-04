/*
 * SSDSim.cpp
 *
 *  Created on: Feb 21, 2011
 *      Author: silverwolf
 */

#include "SSDSim.h"
#include <stdio.h>

#ifdef __cplusplus
#include "ssd.h"
#include <sys/time.h>
using namespace ssd;
#endif

void SSD_Initialize()
{
	load_config();
	print_config(NULL);

	ssdImpl = new Ssd();

	gettimeofday(&ssd_boot_time, NULL);

	printf("Booted the SSD Simulator.\n");
}

void SSD_Cleanup()
{
	printf("SSD Simulator killed.\n");
	delete ssdImpl;
}

void SSD_Write(unsigned long long address, int size, void *buf)
{
	gettimeofday(&ssd_request_time, NULL);

	double time = ((ssd_request_time.tv_sec - ssd_boot_time.tv_sec) * 1000 + (ssd_request_time.tv_usec - ssd_boot_time.tv_usec) / 1000.0) + 0.5;

	for (int i=0;i<size;i += PAGE_SIZE)
	{
		double result = ssdImpl->event_arrive(WRITE, address, 1, time, NULL);
		printf("Write time address %llu (%i): %.20lf at %.3f\n", address, size, result, time);
	}
}

void SSD_Read(unsigned long long address, int size, void *buf)
{
	gettimeofday(&ssd_request_time, NULL);

	double time = ((ssd_request_time.tv_sec - ssd_boot_time.tv_sec) * 1000 + (ssd_request_time.tv_usec - ssd_boot_time.tv_usec) / 1000.0) + 0.5;

	for (int i=0;i<size;i += PAGE_SIZE)
	{
		double result = ssdImpl->event_arrive(READ, address, 1, time, NULL);
		printf("Read time %llu (%i): %.20lf at %.3f\n", address, size, result, time);
	}
}

