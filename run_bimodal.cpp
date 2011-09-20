/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>. */

/****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <math.h>
#include "ssd.h"

using namespace ssd;

/**
 * Benchmark plan
 * 1. The number of writes should be the size of the device simulated.
 * 2. Trim a designated area. E.g. 256MB, measure the time it takes to perform the trim with the implemented FTLs.
 * 3. Measure the time required to overwrite the area and compare with the other FTLs.
 * 4. Create a report with shows the differences in response time using CDF's.
 *
 * Test assumes a 6GB SSD, with Block-size 64 and Page size 2048 bytes.
 */

int main(int argc, char **argv){

	long vaddr;

	double arrive_time;

	load_config();
	print_config(NULL);

	Ssd ssd;

	printf("INITIALIZING SSD Bimodal\n");

	srandom(1);
	int preIO = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE;

	if (FTL_IMPLEMENTATION == 0) // PAGE
		preIO -= 16*BLOCK_SIZE;

	if (FTL_IMPLEMENTATION == 1) // BAST
		preIO -= (BAST_LOG_PAGE_LIMIT*BLOCK_SIZE)*2;

	if (FTL_IMPLEMENTATION == 2) // FAST
		preIO -= (FAST_LOG_PAGE_LIMIT*BLOCK_SIZE)*1.1;

	if (FTL_IMPLEMENTATION > 2) // DFTL BIFTL
		preIO -= 512;

	int deviceSize = 3145216;

	if (preIO > deviceSize)
		preIO = deviceSize;

	printf("Writes %i pages for startup out of %i total pages.\n", preIO, SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE);

	double start_time = 0;
	double timeMultiplier = 10000;

	double read_time = 0;
	double write_time = 0;
	double trim_time = 0;

	unsigned long num_reads = 0;
	unsigned long num_writes = 0;
	unsigned long num_trims = 0;

	std::vector<double> avgsTrim;
	std::vector<double> avgsWrite1;
	std::vector<double> avgsRead1;
	std::vector<double> avgsRead2;
	std::vector<double> avgsRead3;
	std::vector<double> avgsTrim2;
	std::vector<double> avgsWrite2;
	std::vector<double> avgsRead4;
	std::vector<double> avgsWrite3;


	avgsTrim.reserve(1024*64);
	avgsWrite1.reserve(1024*64);
	avgsRead1.reserve(1024*64);
	avgsRead2.reserve(1024*64);
	avgsRead3.reserve(1024*64);
	avgsTrim2.reserve(1024*64);
	avgsWrite2.reserve(1024*64);
	avgsRead4.reserve(1024*64);
	avgsWrite3.reserve(1024*64);

	// Reset statistics
	ssd.reset_statistics();

	// 1. Write random to the size of the device
	srand(1);
	double afterFormatStartTime = 0;
	//for (int i=0; i<preIO/3*2;i++)
	for (int i=0; i<preIO*1.1;i++)
	//for (int i=0; i<700000;i++)
	{
		long int r = random()%preIO;
		double d = ssd.event_arrive(WRITE, r, 1, afterFormatStartTime);
		afterFormatStartTime += d;

		if (i % 10000 == 0)
			printf("Wrote %i %f\n", i,d );
	}

	start_time = afterFormatStartTime;
	// Reset statistics
	ssd.reset_statistics();

	// 2. Trim an area. ( We let in be 512MB (offset 131072 pages or 2048 blocks) into the address space, and then 256MB (1024 blocks or 65536 pages) )

	int startTrim = 2048*64; //131072
	int endTrim = 3072*64; //196608

	/* Test 1 */
	for (int i=startTrim; i<endTrim;i++)
	{

		trim_time = ssd.event_arrive(TRIM, i, 1, ((start_time+arrive_time)*timeMultiplier));
		avgsTrim.push_back(trim_time);
		num_trims++;

		arrive_time += trim_time;

		if (i % 1000 == 0)
			printf("Trim: %i %f\n", i, trim_time);

	}

	for (int i=startTrim; i<endTrim;i++)
	{

		trim_time = ssd.event_arrive(READ, i, 1, ((start_time+arrive_time)*timeMultiplier));
		avgsRead1.push_back(trim_time);
		num_trims++;

		arrive_time += trim_time;

		if (i % 1000 == 0)
			printf("Read: %i %f\n", i, trim_time);

	}

	for (int i=startTrim; i<endTrim;i++)
	{

		trim_time = ssd.event_arrive(READ, i, 1, ((start_time+arrive_time)*timeMultiplier));
		avgsRead2.push_back(trim_time);
		num_trims++;

		arrive_time += trim_time;

		if (i % 1000 == 0)
			printf("Read: %i %f\n", i, trim_time);

	}

	for (int i=startTrim; i<endTrim;i++)
	{

		trim_time = ssd.event_arrive(WRITE, i, 1, ((start_time+arrive_time)*timeMultiplier));
		avgsWrite1.push_back(trim_time);
		num_trims++;

		arrive_time += trim_time;

		if (i % 1000 == 0)
			printf("Write: %i %f\n", i, trim_time);

	}

	for (int i=startTrim; i<endTrim;i++)
	{

		trim_time = ssd.event_arrive(READ, i, 1, ((start_time+arrive_time)*timeMultiplier));
		avgsRead3.push_back(trim_time);
		num_trims++;

		arrive_time += trim_time;

		if (i % 1000 == 0)
			printf("Read: %i %f\n", i, trim_time);

	}

	/* Test 1 */
	for (int i=startTrim; i<endTrim;i++)
	{

		trim_time = ssd.event_arrive(TRIM, i, 1, ((start_time+arrive_time)*timeMultiplier));
		avgsTrim2.push_back(trim_time);
		num_trims++;

		arrive_time += trim_time;

		if (trim_time > 400)
			printf("Trim: %i %f\n", i, trim_time);

	}

	for (int i=startTrim; i<endTrim;i++)
	{

		trim_time = ssd.event_arrive(WRITE, i, 1, ((start_time+arrive_time)*timeMultiplier));
		avgsWrite2.push_back(trim_time);
		num_trims++;

		arrive_time += trim_time;

		if (i % 1000 == 0)
			printf("Write: %i %f\n", i, trim_time);

	}

	for (int i=startTrim; i<endTrim;i++)
	{

		trim_time = ssd.event_arrive(READ, i, 1, ((start_time+arrive_time)*timeMultiplier));
		avgsRead4.push_back(trim_time);
		num_trims++;

		arrive_time += trim_time;

		if (i % 1000 == 0)
			printf("Read: %i %f\n", i, trim_time);

	}

//	// 1. Write random to the size of the device
//	for (int i=0; i<700000;i++)
//	{
//		long int r = (random()%preIO-200000)+200000;
//		double d = ssd.event_arrive(WRITE, r, 1, afterFormatStartTime);
//		afterFormatStartTime += d;
//
//		if (i % 10000 == 0)
//			printf("Wrote %i %f\n", i,d );
//	}
//
//	for (int i=startTrim; i<endTrim;i++)
//	{
//
//		trim_time = ssd.event_arrive(WRITE, i, 1, ((start_time+arrive_time)*timeMultiplier));
//		avgsWrite3.push_back(trim_time);
//		num_trims++;
//
//		arrive_time += trim_time;
//
//		if (i % 1000 == 0)
//			printf("Write: %i %f\n", i, trim_time);
//
//	}


	ssd.print_ftl_statistics();

	FILE *logFile = NULL;
	if ((logFile = fopen("output.log", "w")) == NULL)
	{
		printf("Output file cannot be written to.\n");
		exit(-1);
	}

	fprintf(logFile, "Trim;Read1;Read2;Write1;Read3;Trim2;Write2;Read4;Write3\n");

	for (size_t i=0;i<avgsTrim.size();i++)
	{
		fprintf(logFile, "%f;%f;%f;%f;%f;%f;%f;%f\n", avgsTrim[i],avgsRead1[i], avgsRead2[i], avgsWrite1[i], avgsRead3[i], avgsTrim2[i], avgsWrite2[i], avgsRead4[i]);
	}

	fclose(logFile);

	ssd.print_ftl_statistics();
	ssd.print_statistics();

	printf("Finished.\n");
	return 0;
}


