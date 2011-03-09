/*
 * SSDSim.h
 *
 *  Created on: Feb 21, 2011
 *      Author: Matias Bjørling
 */

#ifndef SSDSIM_H_
#define SSDSIM_H_


#ifdef __cplusplus
#include "ssd.h"

using namespace ssd;

Ssd *ssdImpl;
struct timeval ssd_boot_time, ssd_request_time;

extern "C" {
#endif

void SSD_Initialize();
void SSD_Cleanup();
void SSD_Write(unsigned long long address, int size, void *buf);
void SSD_Read(unsigned long long address, int size, void *buf);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* SSDSIM_H_ */
