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
	aPages = new long[BLOCK_SIZE];

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

	// Detect required number of bits for logical address size
	addressSize = log(NUMBER_OF_ADDRESSABLE_BLOCKS)/log(2);
	addressShift = log(BLOCK_SIZE)/log(2);

	// Find required number of bits for block size
	printf("Total required bits for representation: %i (Address: %i Block: %i) \n", addressSize + addressShift, addressSize, addressShift);

	// Initialise block mapping table.
	data_list = new long[NUMBER_OF_ADDRESSABLE_BLOCKS];

	for (uint i=0;i<NUMBER_OF_ADDRESSABLE_BLOCKS;i++)
		data_list[i] = -1;

	printf("Total mapping table size: %luKB\n", NUMBER_OF_ADDRESSABLE_BLOCKS * sizeof(uint) / 1024);
	printf("Using BAST FTL.\n");
}

FtlImpl_Bast::~FtlImpl_Bast(void)
{
	delete data_list;
}

enum status FtlImpl_Bast::read(Event &event)
{
	// Find block
	long lookupBlock = (event.get_logical_address() >> addressShift);
	Address eventAddress = Address(event.get_logical_address(), PAGE);

	LogPageBlock *logBlock = NULL;
	if (log_map.find(lookupBlock) != log_map.end())
		logBlock = log_map[lookupBlock];

	controller.stats.numMemoryRead++;

	// If page is in the log block
	if (logBlock != NULL && logBlock->pages[eventAddress.page] != -1)
	{
		Address returnAddress = Address(logBlock->address.get_linear_address()+logBlock->pages[eventAddress.page], PAGE);
		event.set_address(returnAddress);
	}
	else  if ((data_list[lookupBlock] == -1 && logBlock != NULL && logBlock->pages[eventAddress.page] == -1) || (data_list[lookupBlock] == -1 && logBlock == NULL))
	{
		event.set_address(Address(0, PAGE));
	} else { // page is in the data block
		Address returnAddress = Address(data_list[lookupBlock]+ event.get_logical_address() % BLOCK_SIZE , PAGE);
		event.set_address(returnAddress);
	}

	if (controller.get_state(event.get_address()) == INVALID)
		event.set_address(Address(0, PAGE));

	// Statistics
	controller.stats.numFTLRead++;

	return controller.issue(event);
}

enum status FtlImpl_Bast::write(Event &event)
{
	LogPageBlock *logBlock = NULL;

	long lba = (event.get_logical_address() >> addressShift);

	Address eventAddress = Address(event.get_logical_address(), PAGE);

	if (log_map.find(lba) == log_map.end())
		allocate_new_logblock(logBlock, lba, event);

	controller.stats.numMemoryRead++;

	logBlock = log_map[lba];

	// Can it fit inside the existing log block. Issue the request.
 	uint numValid = controller.get_num_valid(&logBlock->address);
	if (numValid < BLOCK_SIZE)
	{
		if (logBlock->pages[eventAddress.page] != -1)
		{
			Address replace_address = Address(logBlock->address.get_linear_address()+logBlock->pages[eventAddress.page], PAGE);
			event.set_replace_address(replace_address);
		}

		logBlock->pages[eventAddress.page] = numValid;

		Address logBlockAddress = logBlock->address;

		controller.get_free_page(logBlockAddress);
		event.set_address(logBlockAddress);
	} else {
		if (!is_sequential(logBlock, lba, event))
			random_merge(logBlock, lba, event);

		allocate_new_logblock(logBlock, lba, event);
		logBlock = log_map[lba];
		// Write the current io to a new block.
		logBlock->pages[eventAddress.page] = 0;
		Address dataPage = logBlock->address;
		dataPage.valid = PAGE;
		event.set_address(dataPage);

	}

	if (data_list[lba] != -1)
	{

		int offset = event.get_logical_address() % BLOCK_SIZE;
		Address replace = new Address(data_list[lba]+offset, PAGE);
		if (controller.get_block_pointer(replace)->get_state(offset) != EMPTY)
			event.set_replace_address(replace);
	}

	// Statistics
	controller.stats.numFTLWrite++;

	return controller.issue(event);
}

enum status FtlImpl_Bast::trim(Event &event)
{
	// Find block
	long lookupBlock = (event.get_logical_address() >> addressShift);
	Address eventAddress = Address(event.get_logical_address(), PAGE);

	LogPageBlock *logBlock = NULL;
	if (log_map.find(lookupBlock) != log_map.end())
		logBlock = log_map[lookupBlock];

	controller.stats.numMemoryRead++;

	Address returnAddress;

	if (logBlock != NULL && logBlock->pages[eventAddress.page] != -1) // If page is in the log block
	{
		returnAddress = Address(logBlock->address.get_linear_address()+logBlock->pages[eventAddress.page], PAGE);
		Block *lBlock = controller.get_block_pointer(returnAddress);
		lBlock->invalidate_page(returnAddress.page);

		logBlock->pages[eventAddress.page] = -1; // Reset the mapping

		if (lBlock->get_state() == INACTIVE) // All pages invalid, force an erase. PTRIM style.
		{
			dispose_logblock(logBlock, lookupBlock);
			Block_manager::instance()->erase_and_invalidate(event, returnAddress, LOG);
		}

	}

	if (data_list[lookupBlock] != -1) // Datablock
	{
		Address dataAddress = Address(data_list[lookupBlock]+event.get_logical_address() % BLOCK_SIZE , PAGE);
		Block *dBlock = controller.get_block_pointer(dataAddress);
		dBlock->invalidate_page(dataAddress.page);

		if (dBlock->get_state() == INACTIVE) // All pages invalid, force an erase. PTRIM style.
		{
			data_list[lookupBlock] = -1;
			Block_manager::instance()->erase_and_invalidate(event, dataAddress, DATA);
		}

	}

	event.set_address(returnAddress);
	event.set_noop(true);

	// Statistics
	controller.stats.numFTLTrim++;

	return controller.issue(event);
}


void FtlImpl_Bast::allocate_new_logblock(LogPageBlock *logBlock, long lba, Event &event)
{
	if (log_map.size() >= BAST_LOG_PAGE_LIMIT)
	{
		int victim = random()%log_map.size()-1;
		std::map<long, LogPageBlock*>::iterator it = log_map.begin();

		for (int i=0;i<victim;i++)
			it++;

		long exLogicalBlock = (*it).first;
		LogPageBlock *exLogBlock = (*it).second;

		if (!is_sequential(exLogBlock, exLogicalBlock, event))
			random_merge(exLogBlock, exLogicalBlock, event);

		controller.stats.numPageBlockToPageConversion++;
	}

	logBlock = new LogPageBlock();
	logBlock->address = Block_manager::instance()->get_free_block(LOG, event);

	//printf("Using new log block with address: %lu Block: %u\n", logBlock->address.get_linear_address(), logBlock->address.block);
	log_map[lba] = logBlock;
}

void FtlImpl_Bast::dispose_logblock(LogPageBlock *logBlock, long lba)
{
	log_map.erase(lba);
	delete logBlock;
}

bool FtlImpl_Bast::is_sequential(LogPageBlock* logBlock, long lba, Event &event)
{
	// No page space. Merging required.
	/* 1. Log block merge
	 * 2. Log block switch
	 */

	// Is block switch possible? i.e. log block switch
	bool isSequential = true;
	for (uint i=0;i<BLOCK_SIZE;i++) if (logBlock->pages[i] != (int)i)
	{
		isSequential = false;
		break;
	}

	if (isSequential)
	{
		Block_manager::instance()->promote_block(DATA);

		// Add to empty list i.e. switch without erasing the datablock.
		if (data_list[lba] != -1)
		{
			Address a = Address(data_list[lba], PAGE);
			Block_manager::instance()->erase_and_invalidate(event, a, DATA);
		}

		data_list[lba] = logBlock->address.get_linear_address();
		dispose_logblock(logBlock, lba);

		controller.stats.numLogMergeSwitch++;
		update_map_block(event);
	}

	return isSequential;
}

bool FtlImpl_Bast::random_merge(LogPageBlock *logBlock, long lba, Event &event)
{
	/* Do merge (n reads, n writes and 2 erases (gc'ed))
	 * 1. Write page to new data block
	 * 1a Promote new log block.
	 * 2. Create BLOCK_SIZE reads
	 * 3. Create BLOCK_SIZE writes
	 * 4. Invalidate data block
	 * 5. promote new block as data block
	 * 6. put data and log block into the invalidate list.
	 */

	Address eventAddress = Address(event.get_logical_address(), PAGE);
	Address newDataBlock = Block_manager::instance()->get_free_block(DATA, event);

	int t=0;
	for (uint i=0;i<BLOCK_SIZE;i++)
	{
		// Lookup page table and see if page exist in log page
		Address readAddress;
		if (logBlock->pages[i] != -1)
			readAddress.set_linear_address(logBlock->address.get_linear_address() + logBlock->pages[i], PAGE);
		else if (data_list[lba] != -1)
			readAddress.set_linear_address(data_list[lba] + i, PAGE);
		else
			continue; // Empty page

		if (controller.get_state(readAddress) == EMPTY)
			continue;

		if (controller.get_state(readAddress) == INVALID) // A page might be invalidated by trim
			continue;

		Event readEvent = Event(READ, event.get_logical_address(), 1, event.get_start_time());
		readEvent.set_address(readAddress);
		controller.issue(readEvent);

		Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time()+readEvent.get_time_taken());
		writeEvent.set_address(Address(newDataBlock.get_linear_address() + i, PAGE));
		writeEvent.set_payload((char*)page_data + readAddress.get_linear_address() * PAGE_SIZE);
		writeEvent.set_replace_address(readAddress);
		controller.issue(writeEvent);

		//event.consolidate_metaevent(writeEvent);
		event.incr_time_taken(writeEvent.get_time_taken() + readEvent.get_time_taken());
		// Statistics
		controller.stats.numFTLRead++;
		controller.stats.numFTLWrite++;
		controller.stats.numWLRead++;
		controller.stats.numWLWrite++;
		t++;
	}

//	printf("t %i\n",t);

	// Invalidate inactive pages (LOG and DATA

	Block_manager::instance()->erase_and_invalidate(event, logBlock->address, LOG);

	if (data_list[lba] != -1)
	{
		Address a = Address(data_list[lba], PAGE);
		Block_manager::instance()->erase_and_invalidate(event, a, DATA);
	}

	// Update mapping
	data_list[lba] = newDataBlock.get_linear_address();
	update_map_block(event);

	dispose_logblock(logBlock, lba);

	controller.stats.numLogMergeFull++;
	return true;
}

void FtlImpl_Bast::update_map_block(Event &event)
{
	Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time());
	writeEvent.set_address(Address(0, PAGE));
	writeEvent.set_noop(true);

	controller.issue(writeEvent);

	event.incr_time_taken(writeEvent.get_time_taken());

	controller.stats.numGCWrite++;
	controller.stats.numFTLWrite++;
}


void FtlImpl_Bast::print_ftl_statistics()
{
	Block_manager::instance()->print_statistics();
}

