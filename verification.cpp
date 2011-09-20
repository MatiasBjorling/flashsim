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

int main(int argc, char **argv){

	long vaddr;

	double arrive_time;

	double afterFormatStartTime = 0;

	load_config();
	print_config(NULL);

	Ssd ssd;

	printf("INITIALIZING SSD\n");

	srandom(1);
	int preIO = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE;

	if (FTL_IMPLEMENTATION == 0) // PAGE
		preIO -= 16*BLOCK_SIZE;

	if (FTL_IMPLEMENTATION == 1) // BAST
		preIO -= (BAST_LOG_PAGE_LIMIT*BLOCK_SIZE)*1.3;

	if (FTL_IMPLEMENTATION == 2) // FAST
		preIO -= (FAST_LOG_PAGE_LIMIT*BLOCK_SIZE)*1.1;

	if (FTL_IMPLEMENTATION > 2) // DFTL BIFTL
		preIO -= 1024;

	int deviceSize = 3145216;

	if (preIO > deviceSize)
		preIO = deviceSize;

	printf("Writes %i pages for startup out of %i total pages.\n", preIO, SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE);

	double start_time = afterFormatStartTime;
	double timeMultiplier = 10000;

	FILE *logFile = NULL;
	if ((logFile = fopen("output.log", "w")) == NULL)
	{
		printf("Output file cannot be written to.\n");
		exit(-1);
	}

	fprintf(logFile, "NumIOReads;ReadIOTime;NumIOWrites;WriteIOTime;NumIOTotal;IOTime;");
	ssd.write_header(logFile);

	double read_time = 0;
	double write_time = 0;

	unsigned long num_reads = 0;
	unsigned long num_writes = 0;

	std::vector<double> avgs;

	// Reset statistics
	ssd.reset_statistics();

	num_reads = 0;
	read_time = 0;

	num_writes = 0;
	write_time = 0;

	int addressDivisor = 1;
	float multiplier = 1;

	//ssd.reset_statistics();

	avgs.reserve(preIO);



//	/* Test 1 */
//	for (int i=0; i<preIO;i++)
//	{
//
//		write_time = ssd.event_arrive(WRITE, i, 1, ((start_time+arrive_time)*timeMultiplier));
//		avgs.push_back(write_time);
//		num_writes++;
//
//		arrive_time += write_time;
//
//		if (i % 100000 == 0)
//			printf("%i\n", i);
//
////		// Write all statistics
////		printf("%lu;%f;%lu;%f;%lu;%f;\n", num_reads, read_time, num_writes, write_time, num_reads+num_writes, read_time+write_time);
//
//	}

//	ssd.print_ftl_statistics();
//
//	srandom(1);
//	int seqSize = 128*64;
//
//	/* Test 2 */
//	for (int i=preIO/2-seqSize/2; i<preIO/2+seqSize;i++)
//	{
//		write_time = ssd.event_arrive(WRITE, i, 1, ((start_time+arrive_time)*timeMultiplier));
//		avgs.push_back(write_time);
//		num_writes++;
//
//		arrive_time += write_time;
//		printf("%i\n", i);
//	}
//
//	/* Test 2 */
//	for (int i=0; i<(preIO-seqSize)*1.3;i++)
//	{
//		int r = 0;
//
//		do
//		{
//			r = random()%deviceSize;
//		} while (r > preIO/2-seqSize/2 && preIO/2+seqSize/2 > r);
//
//		write_time = ssd.event_arrive(WRITE, r, 1, ((start_time+arrive_time)*timeMultiplier));
//		avgs.push_back(write_time);
//		num_writes++;
//
//		arrive_time += write_time;
//
//		if (i % 100000 == 0)
//			printf("%i\n", i);
//	}

	ssd.print_ftl_statistics();

	/* Test 3 Start -------------------------------------------------------------------------- */
	srandom(1);
	int seqSize = 128*64;

	for (int i=0; i<preIO*1.3;i++)
	{


		int	r = random()%deviceSize;


		write_time = ssd.event_arrive(WRITE, r, 1, ((start_time+arrive_time)*timeMultiplier));
		avgs.push_back(write_time);
		num_writes++;

		arrive_time += write_time;

		if (i % 100000 == 0)
			printf("%i\n", i);
	}

	for (int i=preIO/2-seqSize/2; i<preIO/2+seqSize;i++)
	{
		write_time = ssd.event_arrive(TRIM, i, 1, ((start_time+arrive_time)*timeMultiplier));
		avgs.push_back(write_time);
		num_writes++;

		arrive_time += write_time;
		printf("%i\n", i);
	}

	/* Test 3 Stop -------------------------------------------------------------------------- */

	double mean = 0.0;
	for (size_t i=0;i<avgs.size();i++)
	{
		mean += avgs[i];
	}

	mean = mean / (double)avgs.size();

	printf("Mean: %f\n", mean);

	double var = 0.0;
	for (size_t i=0;i<avgs.size();i++)
	{
		var += pow(avgs[i]-mean,2);
	}

	var /= avgs.size();

	printf("Var: %f\n", sqrt(var));

	ssd.print_ftl_statistics();
	ssd.print_statistics();


	fclose(logFile);

	printf("Finished.\n");
	return 0;
}


