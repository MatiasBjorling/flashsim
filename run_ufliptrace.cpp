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

	char line[80];

	double read_time = 0;
	double write_time = 0;
	double read_total = 0;
	double write_total = 0;
	unsigned long num_reads = 0;
	unsigned long num_writes = 0;

	load_config();
	print_config(NULL);

	Ssd ssd;

	printf("INITIALIZING SSD\n");

//	for (int i=0; i<2048576;i++)
//	{
//		write_time += ssd.event_arrive(WRITE, i, 1, i*500);
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

	long cnt=0;

	double start_time = 1024288000;
	for (int i=0; i<files.size();i++)
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
		/* first go through and write to all read addresses to prepare the SSD */
		while(fgets(line, 80, trace) != NULL){
			sscanf(line, "%c; %c; %li; %u; %lf, %lf", &ioPatternType, &ioType, &vaddr, &queryTime, &arrive_time, start_time+arrive_time);

			//printf("%li %c %c %li %u %lf %lf %li\n", ++cnt, ioPatternType, ioType, vaddr, queryTime, arrive_time, start_time+arrive_time);

			if (ioType == 'R')
			{
				read_time += ssd.event_arrive(READ, vaddr, 1, start_time+arrive_time);
				num_reads++;
			}
			else if(ioType == 'W')
			{
				write_time += ssd.event_arrive(WRITE, vaddr, 1, start_time+arrive_time);
				num_writes++;
			}
		}

		fclose(trace);
	}

	closedir(working_directory);

	printf("Num reads : %lu\n", num_reads);
	printf("Num writes: %lu\n", num_writes);
	printf("Avg read time : %.20lf\n", read_time / num_reads);
	printf("Avg write time: %.20lf\n", write_time / num_writes);
	return 0;
}
