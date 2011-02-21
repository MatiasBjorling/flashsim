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

extern "C" {
#endif

void SSDInitialize();
void SSDCleanup();
void SSDWrite(unsigned long long address, int size);
void SSDRead(unsigned long long address, int size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* SSDSIM_H_ */
