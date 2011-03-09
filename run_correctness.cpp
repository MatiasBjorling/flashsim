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
#include <string.h>

using namespace ssd;

#define GARBAGEPATH "/home/silverwolf/workspace/randomgarbagesmall"

void do_seq(Ssd *ssd, event_type type, void *test, unsigned int file_size)
{
	unsigned int adr, i = 0;
	for (adr = 0; adr < file_size;adr += PAGE_SIZE)
	{
		ssd->event_arrive(type, i, 1, (double) adr, (char*)test + adr);
		if (type == READ)
		{
			if (memcmp(ssd->get_result_buffer(), (char*)test + adr, PAGE_SIZE) != 0)
				fprintf(stderr, "Err. Data does not compare. i: %i\n", i);
		}
		i++;
	}
}

void do_seq_backward(Ssd *ssd, event_type type, void *test, unsigned int file_size)
{
	unsigned int adr, i = BLOCK_SIZE-1, j=0;

	for (adr = file_size-PAGE_SIZE; adr > 0;adr -= PAGE_SIZE)
	{
		ssd->event_arrive(type, j+i--, 1, file_size-(double) adr, (char*)test + adr);
		if (type == READ)
		{
			if (memcmp(ssd->get_result_buffer(), (char*)test + adr, PAGE_SIZE) != 0)
				fprintf(stderr, "Err. Data does not compare. i: %i\n", i);
		}
		if (i == -1)
		{
			i = BLOCK_SIZE - 1;
			j += BLOCK_SIZE;
		}
	}
}

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
	 * 1. Do linear write and read.
	 * 2. Write linear again and  read.
	 * 2. Do semi-random linear
	 * 3. Do random
	 * 4. Do backward linear
	 */

	printf("Test 1. Write sequential test data.\n");
	do_seq(ssd, WRITE, test_data, st.st_size);

	printf("Test 2. Read sequential test data.\n");
	do_seq(ssd, READ, test_data, st.st_size);

	printf("Test 3. Write second write.\n");
	do_seq(ssd, WRITE, test_data, st.st_size);

	printf("Test 4. Write third write.\n");
	do_seq(ssd, WRITE, test_data, st.st_size);

	printf("Test 5. Read sequential test data.\n");
	do_seq(ssd, READ, test_data, st.st_size);

	printf("Test 6. Write backward sequential test data.\n");
	do_seq_backward(ssd, WRITE, test_data, st.st_size);

	printf("Test 7. Read backward sequential test data.\n");
	do_seq_backward(ssd, READ, test_data, st.st_size);

	printf("Test 8. Write backward sequential test data again.\n");
	do_seq_backward(ssd, WRITE, test_data, st.st_size);

	printf("Test 9. Read backward sequential test data.\n");
	do_seq_backward(ssd, READ, test_data, st.st_size);

	printf("Test 9. Read backward sequential test data.\n");
	do_seq_backward(ssd, READ, test_data, st.st_size);


	delete ssd;
	return 0;
}
