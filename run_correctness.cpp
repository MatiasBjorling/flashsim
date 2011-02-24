/* Copyright 2009, 2010 Brendan Tauras */

/* run_test.cpp is part of FlashSim. */

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

/* Test to see if drive write and read data correct.
 * Matias Bjørling 24/2-2011
 */

#include "ssd.h"
#include <stdio.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

using namespace ssd;

#define GARBAGEPATH "/home/silverwolf/workspace/randomgarbage"

int main()
{
	load_config();
	print_config(NULL);
	printf("\n");

	Ssd *ssd = new Ssd();

	// create memory mapping of file that we are going to check with
	int fd = open(GARBAGEPATH, O_RDONLY);
	struct stat st;
	stat(GARBAGEPATH, &st);

	void *test_data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

	if (test_data == MAP_FAILED)
		fprintf(stderr, "File not mapped.");

	/*
	 * Experiment setup
	 * 1. Do linear
	 * 2. Do semi-random linear
	 * 3. Do random
	 * 4. Do backward linear
	 */
	double result;
	unsigned int adr,i=0;
	printf("arg: %i\n", st.st_size);
	// Linear
	for (adr = 0; adr < st.st_size;adr += BLOCK_SIZE)
	{
		result = ssd -> event_arrive(WRITE, i, 1, (double) adr, (char*)test_data + adr);
		i++;

		if (i==1000)
			break;
	}



	delete ssd;
	return 0;
}
