/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_config.cpp is part of FlashSim. */

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

/* Configuration loader
 * Brendan Tauras 2009-11-02
 * Matias Bjørling 2011-03
 *
 * Functions below provide basic configuration file parsing.  Config file
 * support includes skipping blank lines, comment lines (begin with a #).
 * Parsed lines consist of the variable name, a space, then the value
 * (e.g. SSD_SIZE 4 ).  Default config values (if config file is missing
 * an entry to set the value) are defined in the variable declarations below.
 *
 * A function is also provided for printing the current configuration. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* using namespace ssd; */
namespace ssd {

/* Define typedefs and error macros from ssd.h here instead of including
 * header file because we want to declare the global configuration variables
 * and set them in this file.  ssd.h declares the global configuration
 * variables as extern const, which would conflict this file's definitions.
 * This is not the best solution, but it is easy and it works. */

/* some obvious typedefs for laziness */
typedef unsigned int uint;
typedef unsigned long ulong;

/* define exit codes for errors */
#define MEM_ERR -1
#define FILE_ERR -2

/* Simulator configuration
 * All configuration variables are set by reading ssd.conf and referenced with
 * 	as "extern const" in ssd.h
 * Configuration variables are described below and are assigned default values
 * 	in case of config file error.  The values defined below are overwritten
 * 	when defined in the config file.
 * We do not want a class here because we want to use the configuration
 * 	variables in the same was as macros. */

/* Ram class:
 * 	delay to read from and write to the RAM for 1 page of data */
double RAM_READ_DELAY = 0.00000001;
double RAM_WRITE_DELAY = 0.00000001;

/* Bus class:
 * 	delay to communicate over bus
 * 	max number of connected devices allowed
 * 	number of time entries bus has to keep track of future schedule usage
 * 	value used as a flag to indicate channel is free
 * 		(use a value not used as a delay value - e.g. -1.0)
 * 	number of simultaneous communication channels - defined by SSD_SIZE */
double BUS_CTRL_DELAY = 0.000000005;
double BUS_DATA_DELAY = 0.00000001;
uint BUS_MAX_CONNECT = 8;
uint BUS_TABLE_SIZE = 64;
double BUS_CHANNEL_FREE_FLAG = -1.0;
/* uint BUS_CHANNELS = 4; same as # of Packages, defined by SSD_SIZE */

/* Ssd class:
 * 	number of Packages per Ssd (size) */
uint SSD_SIZE = 4;

/* Package class:
 * 	number of Dies per Package (size) */
uint PACKAGE_SIZE = 8;

/* Die class:
 * 	number of Planes per Die (size) */
uint DIE_SIZE = 2;

/* Plane class:
 * 	number of Blocks per Plane (size)
 * 	delay for reading from plane register
 * 	delay for writing to plane register
 * 	delay for merging is based on read, write, reg_read, reg_write 
 * 		and does not need to be explicitly defined */
uint PLANE_SIZE = 64;
double PLANE_REG_READ_DELAY = 0.0000000001;
double PLANE_REG_WRITE_DELAY = 0.0000000001;

/* Block class:
 * 	number of Pages per Block (size)
 * 	number of erases in lifetime of block
 * 	delay for erasing block */
uint BLOCK_SIZE = 16;
uint BLOCK_ERASES = 1048675;
double BLOCK_ERASE_DELAY = 0.001;

/* Page class:
 * 	delay for Page reads
 * 	delay for Page writes */
double PAGE_READ_DELAY = 0.000001;
double PAGE_WRITE_DELAY = 0.00001;

/* Page data memory allocation
 *
 */
uint PAGE_SIZE = 4096;
bool PAGE_ENABLE_DATA = true;

/*
 * Memory area to support pages with data.
 */
void *page_data;

/*
 * Number of blocks to reserve for mappings. e.g. map directory in BAST.
 */
uint MAP_DIRECTORY_SIZE = 0;

/*
 * Implementation to use (0 -> Page, 1 -> BAST, 2 -> FAST, 3 -> DFTL, 4 -> BiModal
 */
uint FTL_IMPLEMENTATION = 0;

/*
 * Limit of LOG pages (for use in BAST)
 */
uint BAST_LOG_PAGE_LIMIT = 100;


/*
 * Limit of LOG pages (for use in FAST)
 */
uint FAST_LOG_PAGE_LIMIT = 4;

/*
 * Number of pages allowed to be in DFTL Cached Mapping Table.
 * (Size equals CACHE_BLOCK_LIMIT * block size * page size)
 *
 */
uint CACHE_DFTL_LIMIT = 8;

/*
 * Parallelism mode.
 * 0 -> Normal
 * 1 -> Striping
 * 2 -> Logical Address Space Parallelism (LASP)
 */
uint PARALLELISM_MODE = 0;

/* Virtual block size (as a multiple of the physical block size) */
uint VIRTUAL_BLOCK_SIZE = 1;

/* Virtual page size (as a multiple of the physical page size) */
uint VIRTUAL_PAGE_SIZE = 1;

uint NUMBER_OF_ADDRESSABLE_BLOCKS = 0;

/* RAISSDs: Number of physical SSDs */
uint RAID_NUMBER_OF_PHYSICAL_SSDS = 0;

void load_entry(char *name, double value, uint line_number) {
	/* cheap implementation - go through all possibilities and match entry */
	if (!strcmp(name, "RAM_READ_DELAY"))
		RAM_READ_DELAY = value;
	else if (!strcmp(name, "RAM_WRITE_DELAY"))
		RAM_WRITE_DELAY = value;
	else if (!strcmp(name, "BUS_CTRL_DELAY"))
		BUS_CTRL_DELAY = value;
	else if (!strcmp(name, "BUS_DATA_DELAY"))
		BUS_DATA_DELAY = value;
	else if (!strcmp(name, "BUS_MAX_CONNECT"))
		BUS_MAX_CONNECT = (uint) value;
	else if (!strcmp(name, "BUS_TABLE_SIZE"))
		BUS_TABLE_SIZE = (uint) value;
	else if (!strcmp(name, "SSD_SIZE"))
		SSD_SIZE = (uint) value;
	else if (!strcmp(name, "PACKAGE_SIZE"))
		PACKAGE_SIZE = (uint) value;
	else if (!strcmp(name, "DIE_SIZE"))
		DIE_SIZE = (uint) value;
	else if (!strcmp(name, "PLANE_SIZE"))
		PLANE_SIZE = (uint) value;
	else if (!strcmp(name, "PLANE_REG_READ_DELAY"))
		PLANE_REG_READ_DELAY = value;
	else if (!strcmp(name, "PLANE_REG_WRITE_DELAY"))
		PLANE_REG_WRITE_DELAY = value;
	else if (!strcmp(name, "BLOCK_SIZE"))
		BLOCK_SIZE = (uint) value;
	else if (!strcmp(name, "BLOCK_ERASES"))
		BLOCK_ERASES = (uint) value;
	else if (!strcmp(name, "BLOCK_ERASE_DELAY"))
		BLOCK_ERASE_DELAY = value;
	else if (!strcmp(name, "PAGE_READ_DELAY"))
		PAGE_READ_DELAY = value;
	else if (!strcmp(name, "PAGE_WRITE_DELAY"))
		PAGE_WRITE_DELAY = value;
	else if (!strcmp(name, "PAGE_SIZE"))
		PAGE_SIZE = value;
	else if (!strcmp(name, "FTL_IMPLEMENTATION"))
		FTL_IMPLEMENTATION = value;
	else if (!strcmp(name, "PAGE_ENABLE_DATA"))
		PAGE_ENABLE_DATA = (value == 1);
	else if (!strcmp(name, "MAP_DIRECTORY_SIZE"))
		MAP_DIRECTORY_SIZE = value;
	else if (!strcmp(name, "FTL_IMPLEMENTATION"))
		FTL_IMPLEMENTATION = value;
	else if (!strcmp(name, "BAST_LOG_PAGE_LIMIT"))
		BAST_LOG_PAGE_LIMIT = value;
	else if (!strcmp(name, "FAST_LOG_PAGE_LIMIT"))
		FAST_LOG_PAGE_LIMIT = value;
	else if (!strcmp(name, "CACHE_DFTL_LIMIT"))
		CACHE_DFTL_LIMIT = value;
	else if (!strcmp(name, "PARALLELISM_MODE"))
		PARALLELISM_MODE = value;
	else if (!strcmp(name, "VIRTUAL_BLOCK_SIZE"))
		VIRTUAL_BLOCK_SIZE = value;
	else if (!strcmp(name, "VIRTUAL_PAGE_SIZE"))
		VIRTUAL_PAGE_SIZE = value;
	else if (!strcmp(name, "RAID_NUMBER_OF_PHYSICAL_SSDS"))
		RAID_NUMBER_OF_PHYSICAL_SSDS = value;
	else
		fprintf(stderr, "Config file parsing error on line %u\n", line_number);
	return;
}

void load_config(void) {
	const char * const config_name = "ssd.conf";
	FILE *config_file = NULL;

	/* update sscanf line below with max name length (%s) if changing sizes */
	uint line_size = 128;
	char line[line_size];
	uint line_number;

	char name[line_size];
	double value;

	if ((config_file = fopen(config_name, "r")) == NULL) {
		fprintf(stderr, "Config file %s not found.  Exiting.\n", config_name);
		exit(FILE_ERR);
	}

	for (line_number = 1; fgets(line, line_size, config_file) != NULL; line_number++) {
		line[line_size - 1] = '\0';

		/* ignore comments and blank lines */
		if (line[0] == '#' || line[0] == '\n')
			continue;

		/* read lines with entries (name value) */
		if (sscanf(line, "%127s %lf", name, &value) == 2) {
			name[line_size - 1] = '\0';
			load_entry(name, value, line_number);
		} else
			fprintf(stderr, "Config file parsing error on line %u\n",
					line_number);
	}
	fclose(config_file);

	NUMBER_OF_ADDRESSABLE_BLOCKS = (SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE) / VIRTUAL_PAGE_SIZE;

	return;
}

void print_config(FILE *stream) {
	if (stream == NULL)
		stream = stdout;
	fprintf(stream, "RAM_READ_DELAY: %.16lf\n", RAM_READ_DELAY);
	fprintf(stream, "RAM_WRITE_DELAY: %.16lf\n", RAM_WRITE_DELAY);
	fprintf(stream, "BUS_CTRL_DELAY: %.16lf\n", BUS_CTRL_DELAY);
	fprintf(stream, "BUS_DATA_DELAY: %.16lf\n", BUS_DATA_DELAY);
	fprintf(stream, "BUS_MAX_CONNECT: %u\n", BUS_MAX_CONNECT);
	fprintf(stream, "BUS_TABLE_SIZE: %u\n", BUS_TABLE_SIZE);
	fprintf(stream, "SSD_SIZE: %u\n", SSD_SIZE);
	fprintf(stream, "PACKAGE_SIZE: %u\n", PACKAGE_SIZE);
	fprintf(stream, "DIE_SIZE: %u\n", DIE_SIZE);
	fprintf(stream, "PLANE_SIZE: %u\n", PLANE_SIZE);
	fprintf(stream, "PLANE_REG_READ_DELAY: %.16lf\n", PLANE_REG_READ_DELAY);
	fprintf(stream, "PLANE_REG_WRITE_DELAY: %.16lf\n", PLANE_REG_WRITE_DELAY);
	fprintf(stream, "BLOCK_SIZE: %u\n", BLOCK_SIZE);
	fprintf(stream, "BLOCK_ERASES: %u\n", BLOCK_ERASES);
	fprintf(stream, "BLOCK_ERASE_DELAY: %.16lf\n", BLOCK_ERASE_DELAY);
	fprintf(stream, "PAGE_READ_DELAY: %.16lf\n", PAGE_READ_DELAY);
	fprintf(stream, "PAGE_WRITE_DELAY: %.16lf\n", PAGE_WRITE_DELAY);
	fprintf(stream, "PAGE_SIZE: %u\n", PAGE_SIZE);
	fprintf(stream, "PAGE_ENABLE_DATA: %i\n", PAGE_ENABLE_DATA);
	fprintf(stream, "MAP_DIRECTORY_SIZE: %i\n", MAP_DIRECTORY_SIZE);
	fprintf(stream, "FTL_IMPLEMENTATION: %i\n", FTL_IMPLEMENTATION);
	fprintf(stream, "PARALLELISM_MODE: %i\n", PARALLELISM_MODE);
	fprintf(stream, "RAID_NUMBER_OF_PHYSICAL_SSDS: %i\n", RAID_NUMBER_OF_PHYSICAL_SSDS);

	return;
}

}
