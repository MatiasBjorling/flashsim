/*
 * SSDSim.cpp
 *
 *  Created on: Feb 21, 2011
 *      Author: silverwolf
 */

#include "SSDSim.h"
#include "ssd.h"
#include <stdio.h>

#ifdef __cplusplus
#include "ssd.h"
using namespace ssd;
#endif

void SSDInitialize()
{
	ssdImpl = new Ssd();
	printf("Booted the SSD Simulator.\n");
}
void SSDCleanup()
{
	printf("SSD Simulator killed.\n");
	delete ssdImpl;
}
void SSDWrite(unsigned long long address, int size)
{
	double result = ssdImpl->event_arrive(WRITE, address, 1, (double) 0);
	printf("Write time address %llu (%i): %.20lf\n", address, size, result);
}
void SSDRead(unsigned long long address, int size)
{
	double result = ssdImpl->event_arrive(READ, address, 1, (double) 0);
	printf("Read time %llu (%i): %.20lf\n", address, size, result);
}

