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
#include "ssd.h"

using namespace ssd;

int main(int argc, char **argv){

	long vaddr;
	ssd::uint queryTime;
	char ioPatternType; // (S)equential or (R)andom
	char ioType; // (R)ead or (W)rite
	double arrive_time;
	int ioSize;

	char line[80];

	double afterFormatStartTime = 0;

	load_config();
	print_config(NULL);

	Ssd ssd;

	printf("INITIALIZING SSD\n");

	int preIO = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE;

	if (FTL_IMPLEMENTATION == 0) // PAGE
		preIO -= 16*BLOCK_SIZE;

	if (FTL_IMPLEMENTATION == 1) // BAST
		preIO -= (BAST_LOG_PAGE_LIMIT*BLOCK_SIZE)*1.2;

	if (FTL_IMPLEMENTATION == 2) // FAST
		preIO -= (FAST_LOG_PAGE_LIMIT*BLOCK_SIZE)*1.1;

	if (FTL_IMPLEMENTATION > 2) // DFTL BIFTL
		preIO -= 1000;

	//int deviceSize = 2827059;
	int deviceSize = 2097024;

	if (preIO > deviceSize)
		preIO = deviceSize;

	printf("Writes %i pages for startup out of %i total pages.\n", preIO, SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE);

//	srand(1);
//	for (int i=0; i<preIO*3;i++)
//	{
//		long int r = random()%deviceSize;
//		double d = ssd.event_arrive(WRITE, r, 1, (double)i*1000.0);
//		//double d = ssd.event_arrive(WRITE, i, 1, i*1000);
//		afterFormatStartTime += 1000;
//
//		if (i % 1000 == 0)
//			printf("Wrote %i %f\n", i,d );
//	}

	DIR *working_directory = NULL;
	if ((working_directory = opendir(argv[1])) == NULL)
	{
		printf("Please provide trace file directory.\n");
		exit(-1);
	}

	std::vector<std::string> files;
	struct dirent *dirp;
	while ((dirp = readdir(working_directory)) != NULL)
	{
		if (dirp->d_type == DT_REG)
			files.push_back(dirp->d_name);
	}

	std::sort(files.begin(), files.end());

	double start_time = afterFormatStartTime;
	double timeMultiplier = 10000;


	long writeEvent = 0;
	long readEvent = 0;
	for (unsigned int i=0; i<files.size();i++)
	{
		char *filename = NULL;
		asprintf(&filename, "%s%s", argv[1], files[i].c_str());

		FILE *trace = NULL;
		if((trace = fopen(filename, "r")) == NULL){
			printf("File was moved or access was denied.\n");
			exit(-1);
		}

		printf("-__- %s -__-\n", files[i].c_str());

		start_time = start_time + arrive_time;


		int addressDivisor = 1;
		float multiplier = 1;

		std::string fileName = files[i].c_str();
		std::string multiplerStr = fileName.substr(fileName.find('P',0)+1, fileName.find_last_of('_', std::string::npos)-fileName.find('P',0)-1);

		char pattern = fileName.substr(4,1).c_str()[0];
		switch (pattern)
		{
		case '5':
			multiplier = atof(multiplerStr.c_str());
			break;

		}

		/* first go through and write to all read addresses to prepare the SSD */
		while(fgets(line, 80, trace) != NULL){
			sscanf(line, "%c; %c; %li; %u; %i; %lf", &ioPatternType, &ioType, &vaddr, &queryTime, &ioSize, &arrive_time);

			//printf("%li %c %c %li %u %lf %lf %li\n", ++cnt, ioPatternType, ioType, vaddr, queryTime, arrive_time, start_time+arrive_time);

			double local_loop_time = 0;

			if (ioType == 'R')
			{
				for (int i=0;i<ioSize;i++)
				{
					local_loop_time += ssd.event_arrive(READ, ((vaddr+(i*(int)multiplier))/addressDivisor)%deviceSize, 1, ((start_time+arrive_time)*timeMultiplier)+local_loop_time);
					readEvent++;
				}


			}
			else if(ioType == 'W')
			{
				for (int i=0;i<ioSize;i++)
				{
					local_loop_time += ssd.event_arrive(WRITE, ((vaddr+(i*(int)multiplier))/addressDivisor)%deviceSize, 1, ((start_time+arrive_time)*timeMultiplier)+local_loop_time);
					writeEvent++;
				}


			}

			arrive_time += local_loop_time;
		}

		fclose(trace);
	}

	printf("Pre write done------------------------------\n");
	ssd.print_ftl_statistics();
	printf("Num read %li write %li\n", readEvent, writeEvent);
	getchar();


	FILE *logFile = NULL;
	if ((logFile = fopen("output.log", "w")) == NULL)
	{
		printf("Output file cannot be written to.\n");
		exit(-1);
	}

	fprintf(logFile, "File;NumIOReads;ReadIOTime;NumIOWrites;WriteIOTime;NumIOTotal;IOTime;");
	ssd.write_header(logFile);

	double read_time = 0;
	double write_time = 0;

	unsigned long num_reads = 0;
	unsigned long num_writes = 0;

	for (unsigned int i=0; i<files.size();i++)
	{
		char *filename = NULL;
		asprintf(&filename, "%s%s", argv[1], files[i].c_str());

		FILE *trace = NULL;
		if((trace = fopen(filename, "r")) == NULL){
			printf("File was moved or access was denied.\n");
			exit(-1);
		}

		fprintf(logFile, "%s;", files[i].c_str());

		printf("-__- %s -__-\n", files[i].c_str());

		start_time = start_time + arrive_time;

		// Reset statistics
		ssd.reset_statistics();

		num_reads = 0;
		read_time = 0;

		num_writes = 0;
		write_time = 0;

		int addressDivisor = 1;
		float multiplier = 1;

		std::string fileName = files[i].c_str();
		std::string multiplerStr = fileName.substr(fileName.find('P',0)+1, fileName.find_last_of('_', std::string::npos)-fileName.find('P',0)-1);

		char pattern = fileName.substr(4,1).c_str()[0];
		switch (pattern)
		{
		case '5':
			multiplier = atof(multiplerStr.c_str());
			break;

		}

		/* first go through and write to all read addresses to prepare the SSD */
		while(fgets(line, 80, trace) != NULL){
			sscanf(line, "%c; %c; %li; %u; %i; %lf", &ioPatternType, &ioType, &vaddr, &queryTime, &ioSize, &arrive_time);

			//printf("%li %c %c %li %u %lf %lf %li\n", ++cnt, ioPatternType, ioType, vaddr, queryTime, arrive_time, start_time+arrive_time);

			double local_loop_time = 0;

			if (ioType == 'R')
			{
				for (int i=0;i<ioSize;i++)
				{
					local_loop_time += ssd.event_arrive(READ, ((vaddr+(i*(int)multiplier))/addressDivisor)%deviceSize, 1, ((start_time+arrive_time)*timeMultiplier)+local_loop_time);
					num_reads++;
				}

				read_time += local_loop_time;
			}
			else if(ioType == 'W')
			{
				for (int i=0;i<ioSize;i++)
				{
					local_loop_time += ssd.event_arrive(WRITE, ((vaddr+(i*(int)multiplier))/addressDivisor)%deviceSize, 1, ((start_time+arrive_time)*timeMultiplier)+local_loop_time);

					num_writes++;
				}
				write_time += local_loop_time;

			}

			arrive_time += local_loop_time;
		}

		// Write all statistics
		fprintf(logFile, "%lu;%f;%lu;%f;%lu;%f;", num_reads, read_time, num_writes, write_time, num_reads+num_writes, read_time+write_time);
		ssd.write_statistics(logFile);

		fclose(trace);


	}

	fclose(logFile);

	closedir(working_directory);

	printf("Finished.\n");
	return 0;
}


