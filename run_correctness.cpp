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

#define GARBAGEPATH "/home/silverwolf/garbage"

double timings = 0.0;

double do_seq(Ssd *ssd, event_type type, void *test, unsigned int file_size)
{
	unsigned int adr, i = 0;
	double result = 0;
	for (adr = 0; adr < file_size;adr += PAGE_SIZE)
	{
		double iotime = ssd->event_arrive(type, i, 1, timings, (char*)test + adr);
		//printf("IO Execution time: %f\n", iotime);
		result += iotime;
		timings += iotime;
		if (type == READ)
		{
			if (ssd->get_result_buffer() == NULL)
				printf("Data has not been written\n");
			else if (memcmp(ssd->get_result_buffer(), (char*)test + adr, PAGE_SIZE) != 0)
				fprintf(stderr, "i: %i ", i);
		}
		i++;
	}
	fprintf(stderr,"\n");
	return result;
}

double do_seq_backward(Ssd *ssd, event_type type, void *test, unsigned int file_size)
{
	unsigned int adr, i = BLOCK_SIZE-1, j=0;
	double result = 0;
	for (adr = file_size; adr > 0;adr -= PAGE_SIZE)
	{
		double iotime = ssd->event_arrive(type, j+i, 1, timings, (char*)test + adr - PAGE_SIZE);

		if (type == READ && memcmp(ssd->get_result_buffer(), (char*)test + adr - PAGE_SIZE, PAGE_SIZE) != 0)
			fprintf(stderr, "Err. Data does not compare. i: %i\n", j+i);

		result += iotime;
		timings += iotime;

		i--;

		if (i == -1u)
		{
			i = BLOCK_SIZE - 1;
			j += BLOCK_SIZE;
		}
	}
	return result;
}

double do_random(Ssd *ssd, event_type type, void *test, unsigned int file_size)
{
	unsigned int adr, i = 0;
	double result = 0;
	for (adr = 0; adr < file_size;adr += PAGE_SIZE)
	{
		result += ssd->event_arrive(type, i, 1, (double) adr, (char*)test + adr);
		if (type == READ)
		{
			if (memcmp(ssd->get_result_buffer(), (char*)test + adr, PAGE_SIZE) != 0)
				fprintf(stderr, "Err. Data does not compare. i: %i\n", i);
		}
		i++;
	}
	return result;
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

	printf("Size of testfile: %iKB\n", (int)st.st_size/1024);

	/*
	 * Experiment setup
	 * 1. Do linear write and read.
	 * 2. Write linear again and  read.
	 * 2. Do semi-random linear
	 * 3. Do random
	 * 4. Do backward linear
	 */

	double result = 0;

	printf("Test 1. Write sequential test data.\n");
	result += do_seq(ssd, WRITE, test_data, st.st_size);

	printf("Test 1. Write sequential test data.\n");
	result += do_seq(ssd, WRITE, test_data, st.st_size);

	printf("Test 1. Write sequential test data.\n");
	result += do_seq(ssd, WRITE, test_data, st.st_size);
//
//	printf("Test 1. Trim data.\n");
//	result += do_seq(ssd, TRIM, test_data, st.st_size);

	printf("Test 1. Write sequential test data.\n");
		result += do_seq(ssd, WRITE, test_data, st.st_size);

	printf("Test 2. Read sequential test data.\n");
	result += do_seq(ssd, READ, test_data, st.st_size);

//	printf("Test 6. Write backward sequential test data.\n");
//	result += do_seq_backward(ssd, WRITE, test_data, st.st_size);
//
//	printf("Test 9. Read backward sequential test data.\n");
//	result += do_seq_backward(ssd, READ, test_data, st.st_size);

//	printf("Test 3. Write second write.\n");
//	result += do_seq(ssd, WRITE, test_data, st.st_size);
//
//	printf("Test 4. Write third write.\n");
//	result += do_seq_backward(ssd, WRITE, test_data, st.st_size);
//
//	printf("Test 5. Read sequential test data.\n");
//	result += do_seq_backward(ssd, READ, test_data, st.st_size);
//
//	printf("Test 6. Write backward sequential test data.\n");
//	result += do_seq_backward(ssd, WRITE, test_data, st.st_size);
//
//	printf("Test 7. Read backward sequential test data.\n");
//	result += do_seq_backward(ssd, READ, test_data, st.st_size);
//
//	printf("Test 8. Write backward sequential test data again.\n");
//	result += do_seq_backward(ssd, WRITE, test_data, st.st_size);
//
//	printf("Test 9. Read backward sequential test data.\n");
//	result += do_seq_backward(ssd, READ, test_data, st.st_size);

	printf("Write time: %.10lfs\n", result);

	ssd->print_statistics();
	delete ssd;
	return 0;
}
