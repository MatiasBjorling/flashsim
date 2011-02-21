/* Copyright 2011 Matias Bjørling */

/* bast_ftl.cpp  */

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

/* Implementation of the BAST FTL described in the Paper
 * "A SPACE-EFFICIENT FLASH TRANSLATION LAYER FOR COMPACTFLASH SYSTEMS by Kim et. al.
 *
 * Notice: Startup procedures are not implemented as the drive is empty every time
 * the simulator is executed. i.e. OOB's is not filled with logical page address
 * at write and it is not read on startup to recreate mapping tables.
 */


#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../ssd.h"

using namespace ssd;

LogPageBlock::LogPageBlock():
		state(FREE)
{
	pages = new int[BLOCK_SIZE];
	for (uint i=0;i<BLOCK_SIZE;i++)
	{
		pages[i] = -1;
	}

}

void LogPageBlock::Reset()
{
	for (uint i=0;i<BLOCK_SIZE;i++)
	{
		pages[i] = -1;
	}
	state = FREE;
	address = 0;
	numValidPages = 0;
}

LogPageBlock::~LogPageBlock()
{
	delete [] pages;
}

Ftl::Ftl(Controller &controller):
	controller(controller),
	garbage(*this),
	wear(*this)
{
	currentPage = 0;

	// Detect required number of bits for logical address size
	for (int size = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * 4; size > 0; addressSize++) size /= 2;

	// Find required number of bits for block size
	for (int size = BLOCK_SIZE/2;size > 0; addressShift++) size /= 2;

	printf("Total required bits for representation: %i (Address: %i Block: %i) \n", addressSize + addressShift, addressSize, addressShift);

	// Trivial assumption checks
	if (sizeof(uint) != 4) assert("uint is not 4 bytes");

	// Initialise block mapping table.
	uint numBlocks = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE;
	data_list = new long[numBlocks];
	free_list = new long[numBlocks];
	log_list = new LogPageBlock[numBlocks];
	invalid_list = new long[numBlocks];

	for (int i=0;i<numBlocks;i++)
	{
		data_list[i] = -1;
		free_list[i] = -1;
		invalid_list[i] = -1;
	}


	printf("Total mapping table size: %iKB\n", numBlocks * sizeof(uint) / 1024);

	return;
}

Ftl::~Ftl(void)
{
	delete map;
	return;
}

enum status Ftl::read(Event &event)
{
	// Find block
	uint lookupBlock = (event.get_logical_address() >> addressShift);
	Address eventAddress = resolve_logical_address(event.get_logical_address());

	LogPageBlock *logBlock = &log_list[lookupBlock];

	if (data_list[lookupBlock] == -1 && logBlock->pages[eventAddress.page] == -1)
	{
		event.set_address(resolve_logical_address(0));
		fprintf(stderr, "Page not written! Logical Address: %i\n", event.get_logical_address());
	}

	// If page is in the log block
	if (logBlock->pages[eventAddress.page] != -1)
	{
		Address returnAddress = resolve_logical_address(logBlock->address);
		returnAddress.page = logBlock->pages[eventAddress.page];
		event.set_address(returnAddress);
	} else {
		// If page is in the data block
		Address returnAddress = resolve_logical_address(data_list[lookupBlock]);
		event.set_address(returnAddress);
	}

	controller.issue(event);


	return SUCCESS;
}

enum status Ftl::write(Event &event)
{
	uint lookupBlock = (event.get_logical_address() >> addressShift);

	Address eventAddress = resolve_logical_address(event.get_logical_address());

	LogPageBlock *logBlock = &log_list[lookupBlock];

	if (logBlock->state == INVALID)
		return FAILURE;

	// Get a new block and promote it as the block for the request.
	if (logBlock->state == FREE)
	{
		Address newLogBlock;
		if (get_free_block(newLogBlock) == FAILURE)
			return FAILURE;

		// Maintain page-level block structure.
		logBlock->state = ACTIVE;
		logBlock->pages[eventAddress.page] = logBlock->numValidPages;
		logBlock->address = currentPage;
		newLogBlock.page = logBlock->numValidPages;

		logBlock->numValidPages++;

		event.set_address(newLogBlock);
		controller.issue(event);
		return SUCCESS;
	}

	// Can it fit inside the exising log block. Issue the request.
	if (logBlock->numValidPages < BLOCK_SIZE)
	{
		printf("iamyourfather %i\n", logBlock->numValidPages);
		logBlock->pages[eventAddress.page] = logBlock->numValidPages;
		Address logBlockAddress = resolve_logical_address(logBlock->address);
		logBlockAddress.page = logBlock->numValidPages;
		event.set_address(logBlockAddress);
		controller.issue(event);

		logBlock->numValidPages++;

		return SUCCESS;
	}

	// No space. Merging required.
	/* 1. Log block merge
	 * 2. Log block switch
	 */

	printf("Must do merging\n");
	// Is block switch possible? i.e. log block switch
	bool isSequential = true;
	for (uint i=0;i<BLOCK_SIZE;i++)
	{
		if (logBlock->pages[i] != i)
		{
			isSequential = false;
			break;
		}
	}

	if (isSequential)
	{
		// Add to empty list i.e. switch without erasing the datablock.
		if (data_list[lookupBlock] != -1)
			invalid_list[data_list[lookupBlock]] = 1; // Cleaned at next run.

		data_list[lookupBlock] = logBlock->address;
		logBlock->Reset();
		printf("Wrote sequential\n");
		// TODO: Update mapping with IO.
		return SUCCESS;
	}





	return FAILURE;
}

inline Address Ftl::resolve_logical_address(uint logicalAddress)
{
	uint numCells = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE;

	Address phyAddress;
	phyAddress.package = floor(logicalAddress / (numCells / SSD_SIZE));
	phyAddress.die = floor(logicalAddress / (numCells / SSD_SIZE / PACKAGE_SIZE));
	phyAddress.plane = floor(logicalAddress / (numCells / SSD_SIZE / PACKAGE_SIZE / DIE_SIZE));
	phyAddress.block = floor(logicalAddress / (numCells / SSD_SIZE / PACKAGE_SIZE / DIE_SIZE / PLANE_SIZE));
	phyAddress.page = logicalAddress % BLOCK_SIZE;
	phyAddress.valid = PAGE;

	//fprintf(stderr, "numCells: %i package: %i die: %i plane: %i block: %i page: %i\n", numCells, phyAddress.package, phyAddress.die, phyAddress.plane, phyAddress.block, phyAddress.page);

	return phyAddress;
}

enum status Ftl::erase(Event &event)
{
	return SUCCESS;
}

enum status Ftl::merge(Event &event)
{
	return SUCCESS;
}

void Ftl::garbage_collect(Event &event)
{
	(void) garbage.collect(event);
}

ssd::ulong Ftl::get_erases_remaining(const Address &address) const
{
	return controller.get_erases_remaining(address);
}

void Ftl::get_least_worn(Address &address) const
{
	controller.get_least_worn(address);
	return;
}

enum page_state Ftl::get_state(const Address &address) const
{
	return controller.get_state(address);
}

enum status Ftl::get_free_block(Address &address)
{
	currentPage += BLOCK_SIZE;
	address = resolve_logical_address(currentPage);

	if (controller.get_block_state(address) == FREE)
	{
		return SUCCESS;
	}

	fprintf(stderr, "No free pages left for FTL.\n");
	return FAILURE;
}
