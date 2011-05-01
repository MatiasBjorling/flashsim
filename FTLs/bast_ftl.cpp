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
 * "A SPACE-EFFICIENT FLASH TRANSLATION LAYER FOR COMPACTFLASH SYSTEMS by Kim et. al."
 *
 * Notice: Startup procedures are not implemented as the drive is empty every time
 * the simulator is executed. i.e. OOB's is not filled with logical page address
 * at write and it is not read on startup to recreate mapping tables.
 *
 * Mapping table are implemented using simulation. A simulated read is performed
 * every time a page read is out a cache log page. A cache log page usually hold approx.
 * 1000 mappings.
 *
 * Second notice. Victim mappings still need to be implemented.
 */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <queue>
#include "../ssd.h"

using namespace ssd;

LogPageBlock::LogPageBlock()
{
	pages = new int[BLOCK_SIZE];
	aPages = new ulong[BLOCK_SIZE];

	for (uint i=0;i<BLOCK_SIZE;i++)
	{
		pages[i] = -1;
		aPages[i] = -1;
	}

	numPages = 0;

	next = NULL;
}


LogPageBlock::~LogPageBlock()
{
	delete [] pages;
	delete [] aPages;
}

/* Comparison class for use by FTL to sort the LogPageBlock compared to the number of pages written. */
bool LogPageBlock::operator() (const LogPageBlock& lhs, const LogPageBlock& rhs) const
{
	return lhs.numPages < rhs.numPages;
}

FtlImpl_Bast::FtlImpl_Bast(Controller &controller):
	FtlParent(controller)
{
	addressShift = 0;
	addressSize = 0;

	// Detect required number of bits for logical address size
	for (int size = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * 4; size > 0; addressSize++) size /= 2;

	// Find required number of bits for block size
	for (int size = BLOCK_SIZE/2;size > 0; addressShift++) size /= 2;

	printf("Total required bits for representation: %i (Address: %i Block: %i) \n", addressSize + addressShift, addressSize, addressShift);

	// Trivial assumption checks
	if (sizeof(int) != 4) assert("integer is not 4 bytes");

	// Initialise block mapping table.
	uint numBlocks = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE;
	data_list = new long[numBlocks];

	for (uint i=0;i<numBlocks;i++)
	{
		data_list[i] = -1;
	}

	printf("Total mapping table size: %luKB\n", numBlocks * sizeof(uint) / 1024);
	printf("Using BAST FTL.\n");
	return;
}

FtlImpl_Bast::~FtlImpl_Bast(void)
{
	delete data_list;

	return;
}

enum status FtlImpl_Bast::read(Event &event)
{
	// Find block
	long lookupBlock = (event.get_logical_address() >> addressShift);
	Address eventAddress;
	eventAddress.set_linear_address(event.get_logical_address());

	LogPageBlock *logBlock = NULL;
	if (log_map.find(lookupBlock) != log_map.end())
		logBlock = log_map[lookupBlock];

	if (data_list[lookupBlock] == -1 && logBlock != NULL && logBlock->pages[eventAddress.page] == -1)
	{
		event.set_address(new Address(0, PAGE));
		fprintf(stderr, "Page read not written. Logical Address: %li\n", event.get_logical_address());
		return FAILURE;
	}

	// If page is in the log block
	if (logBlock != NULL && logBlock->pages[eventAddress.page] != -1)
	{
		Address returnAddress = new Address(logBlock->address.get_linear_address()+logBlock->pages[eventAddress.page], PAGE);
		event.set_address(returnAddress);

		manager.simulate_map_read(event);

		return controller.issue(event);
	} else {
		// If page is in the data block
		Address returnAddress = new Address(data_list[lookupBlock]+ event.get_logical_address() % BLOCK_SIZE , PAGE);
		event.set_address(returnAddress);

		manager.simulate_map_read(event);

		return controller.issue(event);
	}

	return FAILURE;
}


void FtlImpl_Bast::allocate_new_logblock(LogPageBlock *logBlock, long logicalBlockAddress, Event &event)
{
	if (log_map.size() >= PAGE_MAX_LOG)
	{
		long exLogicalBlock = (*log_map.begin()).first;
		LogPageBlock *exLogBlock = (*log_map.begin()).second;

		//printf("killing %li with address: %lu\n", exLogicalBlock, exLogBlock->address.get_linear_address());

		if (!is_sequential(exLogBlock, exLogicalBlock, event))
			random_merge(exLogBlock, exLogicalBlock, event);
	}

	logBlock = new LogPageBlock();
	logBlock->address = manager.get_free_block(LOG);

	//printf("Using new log block with address: %lu Block: %u at logical address: %li\n", logBlock->address.get_linear_address(), logBlock->address.block, logicalBlockAddress);
	log_map[logicalBlockAddress] = logBlock;
}

void FtlImpl_Bast::dispose_logblock(LogPageBlock *logBlock, long logicalBlockAddress)
{
	log_map.erase(logicalBlockAddress);
	delete logBlock;
}

bool FtlImpl_Bast::is_sequential(LogPageBlock* logBlock, long logicalBlockAddress, Event &event)
{
	// No page space. Merging required.
	/* 1. Log block merge
	 * 2. Log block switch
	 */

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
		manager.promote_block(DATA);

		// Add to empty list i.e. switch without erasing the datablock.
		Address a;
		a.set_linear_address(data_list[logicalBlockAddress], BLOCK);
		if (data_list[logicalBlockAddress] != -1)
			manager.invalidate(a, DATA);

		data_list[logicalBlockAddress] = logBlock->address.get_linear_address();

		dispose_logblock(logBlock, logicalBlockAddress);

		manager.simulate_map_write(event);

		//printf("Wrote sequential\n");
	}

	return isSequential;
}

bool FtlImpl_Bast::random_merge(LogPageBlock *logBlock, long logicalBlockAddress, Event &event)
{
	// Do merge (n reads, n writes and 2 erases (gc'ed))
	/* 1. Write page to new data block
	 * 1a Promote new log block.
	 * 2. Create BLOCK_SIZE reads
	 * 3. Create BLOCK_SIZE writes
	 * 4. Invalidate data block
	 * 5. promote new block as data block
	 * 6. put data and log block into the invalidate list.
	 */

	Address eventAddress;
	eventAddress.set_linear_address(event.get_logical_address());

	Address newDataBlock = manager.get_free_block(DATA);
	printf("Using new data block with address: %lu Block: %u\n", newDataBlock.get_linear_address(), newDataBlock.block);

	Event *eventOps = event.get_last_event(event);
	Event *newEvent = NULL;
	for (uint i=0;i<BLOCK_SIZE;i++)
	{
		// Lookup page table and see if page exist in log page
		Address readAddress;
		if (logBlock->pages[eventAddress.page] != -1)
		{
			readAddress.set_linear_address(logBlock->address.real_address + logBlock->pages[i], PAGE);
		}
		else if (data_list[logicalBlockAddress] != -1)
		{
			readAddress.set_linear_address(data_list[logicalBlockAddress] + i, PAGE);
		}
		else
		{
			printf("Empty page.\n");
			continue;
		}

		if((newEvent = new Event(READ, event.get_logical_address(), 1, event.get_start_time())) == NULL)
		{
			fprintf(stderr, "Ssd error: %s: could not allocate Event\n", __func__);	exit(MEM_ERR);
		}

		newEvent->set_address(readAddress);

		eventOps->set_next(*newEvent);
		eventOps = newEvent;

		if((newEvent = new Event(WRITE, event.get_logical_address(), 1, event.get_start_time())) == NULL)
		{
			fprintf(stderr, "Ssd error: %s: could not allocate Event\n", __func__); exit(MEM_ERR);
		}

		Address dataBlockAddress = new Address(newDataBlock.get_linear_address() + i, PAGE);

		newEvent->set_payload((char*)page_data + readAddress.get_linear_address() * PAGE_SIZE);

		newEvent->set_address(dataBlockAddress);

		eventOps->set_next(*newEvent);
		eventOps = newEvent;
	}

	event.get_last_event(event);

	// Invalidate inactive pages
	manager.invalidate(logBlock->address, LOG);
	if (data_list[logicalBlockAddress] != -1)
	{
		Address dBlock = new Address(data_list[logicalBlockAddress], BLOCK);
		manager.invalidate(dBlock, DATA);
	}

	// Update mapping
	data_list[logicalBlockAddress] = newDataBlock.get_linear_address();

	// Add erase events if necessary.
	manager.insert_events(event);

	// Add write events if necessary.
	manager.simulate_map_write(event);

	dispose_logblock(logBlock, logicalBlockAddress);

	return true;
}

enum status FtlImpl_Bast::write(Event &event)
{
	long logicalBlockAddress = (event.get_logical_address() >> addressShift);
	Address eventAddress;
	eventAddress.set_linear_address(event.get_logical_address());

	LogPageBlock *logBlock = NULL;

	if (log_map.find(logicalBlockAddress) == log_map.end())
		allocate_new_logblock(logBlock, logicalBlockAddress, event);

	logBlock = log_map[logicalBlockAddress];

	block_state logBlockState = controller.get_block_state(logBlock->address);

	assert(logBlockState != INACTIVE);

	// Can it fit inside the existing log block. Issue the request.
	uint numValid = controller.get_num_valid(&logBlock->address);
	if (numValid < BLOCK_SIZE)
	{
		logBlock->pages[eventAddress.page] = numValid;

		Address logBlockAddress = logBlock->address;
		controller.get_free_page(logBlockAddress);

		event.set_address(logBlockAddress);
	} else {
		if (!is_sequential(logBlock, logicalBlockAddress, event))
			random_merge(logBlock, logicalBlockAddress, event);

		allocate_new_logblock(logBlock, logicalBlockAddress, event);

		// Write the current io to a new block.
		logBlock->pages[eventAddress.page] = 0;
		Address dataPage = logBlock->address;
		dataPage.valid = PAGE;
		event.set_address(dataPage);
	}

	manager.insert_events(event);

	if (controller.issue(event) == FAILURE)
		return FAILURE;

	event.consolidate_metaevent(event);

	return SUCCESS;
}

