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

	double read_time = 0;
	double write_time = 0;

	unsigned long num_reads = 0;
	unsigned long num_writes = 0;

	double afterFormatStartTime = 0;

	load_config();
	print_config(NULL);

	Ssd ssd;

	printf("INITIALIZING SSD\n");

//	srandom(112);
//
//	for (int i=0; i<150000;i++)
//	{
//		long int wee = random()%2000000;
//
//		ssd.event_arrive(WRITE, wee, 1, i*1000);
//		afterFormatStartTime += 1000;
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

	FILE *logFile = NULL;
	if ((logFile = fopen("output.log", "w")) == NULL)
	{
		printf("Output file cannot be written to.\n");
		exit(-1);
	}

	fprintf(logFile, "File;NumIOReads;ReadIOTime;NumIOWrites;WriteIOTime;NumIOTotal;IOTime;");
	ssd.write_header(logFile);

	double timeMultiplier = 10000;
	int deviceSize = 2000000;

	long cnt=0;

	double start_time = afterFormatStartTime;
	for (int i=0; i<files.size();i++)
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

		std::string fileName = files[i].c_str();
		//DET_1_1_00_0023_P64.0_2.csv
		int testnr = atoi(fileName.substr(4,1).c_str());

		char pattern = fileName.substr(4,1).c_str()[0];

		int addressDivisor = 1;
		float multiplier = 1;

		std::string multiplerStr = fileName.substr(fileName.find('P',0)+1, fileName.find_last_of('_', std::string::npos)-fileName.find('P',0)-1);

		switch (pattern)
		{
		case '5':
			multiplier = atof(multiplerStr.c_str());
			break;

		}


		printf("testnr %i %i\n", testnr, ioSize);

		/* first go through and write to all read addresses to prepare the SSD */
		while(fgets(line, 80, trace) != NULL){
			sscanf(line, "%c; %c; %li; %u; %i; %lf", &ioPatternType, &ioType, &vaddr, &queryTime, &ioSize, &arrive_time);

			//printf("%li %c %c %li %u %lf %lf %li\n", ++cnt, ioPatternType, ioType, vaddr, queryTime, arrive_time, start_time+arrive_time);

			double local_loop_time = 0;

			if (ioType == 'R')
			{
				for (int i=0;i<ioSize;i++)
				{
					local_loop_time += ssd.event_arrive(READ, ((vaddr+(i*(int)multiplier))/addressDivisor)%deviceSize, 1, (start_time+arrive_time+local_loop_time)*timeMultiplier);
					num_reads++;
				}

				read_time += local_loop_time;
			}
			else if(ioType == 'W')
			{
				for (int i=0;i<ioSize;i++)
				{
					local_loop_time += ssd.event_arrive(WRITE, ((vaddr+(i*(int)multiplier))/addressDivisor)%deviceSize, 1, (start_time+arrive_time+local_loop_time)*timeMultiplier);

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
