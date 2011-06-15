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
//	printf("Press ENTER to continue...");
//	getchar();
//	printf("\n");

	Ssd ssd;

	FILE *trace = NULL;
	if((trace = fopen(argv[1], "r")) == NULL){
		printf("Please provide trace file name\n");
		exit(-1);
	}

	printf("INITIALIZING SSD\n");

	/* first go through and write to all read addresses to prepare the SSD */
	while(fgets(line, 80, trace) != NULL){
		sscanf(line, "%c; %c; %li; %u; %lf", &ioPatternType, &ioType, &vaddr, &queryTime, &arrive_time);

		if(ioType == 'W')
			(void) ssd.event_arrive(WRITE, vaddr, 1, arrive_time);

		printf(line);
		printf("%c %c %li %u %lf\n", ioPatternType, ioType, vaddr, queryTime, arrive_time);
	}

//	printf("STARTING TRACE\n");
//
//	/* now rewind file and run trace */
//	fseek(trace, 0, SEEK_SET);
//	while(fgets(line, 80, trace) != NULL){
//		sscanf(line, "%c; %c; %li; %u; %ld", &ioPatternType, &ioType, &vaddr, &queryTime, arrive_time);
//		vaddr %= 65536;
//		if(op == 0){
//			write_time = ssd.event_arrive(WRITE, vaddr, size, arrive_time);
//			if(write_time != 0){
//				write_total += write_time;
//				num_writes++;
//			}
//		} else if(op == 1){
//			read_time = ssd.event_arrive(READ, vaddr, size, arrive_time);
//			if(read_time != 0){
//				read_total += read_time;
//				num_reads++;
//			}
//		} else
//			fprintf(stderr, "Bad operation in trace\n");
//	}
	printf("Num reads : %lu\n", num_reads);
	printf("Num writes: %lu\n", num_reads);
	printf("Avg read time : %.20lf\n", read_time / num_reads);
	printf("Avg write time: %.20lf\n", write_time / num_writes);
	return 0;
}
