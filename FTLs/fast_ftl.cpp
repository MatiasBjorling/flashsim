/* fast_ftl.cpp
 *
 * Copyright 2011 Matias Bjørling
 *s
 * FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Implementation of the FAST FTL described in the paper
 * "A Log buffer-Based Flash Translation Layer Using Fully-Associative Sector Translation by Lee et. al."
 */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <queue>
#include <iostream>
#include <signal.h>
#include "../ssd.h"

using namespace ssd;

FtlImpl_Fast::FtlImpl_Fast(Controller &controller):
	FtlParent(controller)
{
	addressSize = log(NUMBER_OF_ADDRESSABLE_BLOCKS)/log(2);
	addressShift = log(BLOCK_SIZE)/log(2);

	printf("Total required bits for representation: %i (Address: %i Block: %i) \n", addressSize + addressShift, addressSize, addressShift);

	// Initialise block mapping table.
	data_list = new long[NUMBER_OF_ADDRESSABLE_BLOCKS];
	std::fill_n(data_list, NUMBER_OF_ADDRESSABLE_BLOCKS, -1);

	pin_list = new bool[NUMBER_OF_ADDRESSABLE_BLOCKS*BLOCK_SIZE];

	// SW
	sequential_offset = 0;
	sequential_logicalblock_address = -1;

	log_page_next = 0;

	log_pages = NULL;

	printf("Total mapping table size: %luKB\n", NUMBER_OF_ADDRESSABLE_BLOCKS * sizeof(uint) / 1024);
	printf("Using FAST FTL.\n");
}

FtlImpl_Fast::~FtlImpl_Fast(void)
{
	delete data_list;
	delete log_pages;
}

void FtlImpl_Fast::initialize_log_pages()
{
	if (log_pages != NULL)
		return;

	Event event = Event(WRITE, 1, 1, 0);
	// RW
	log_pages = new LogPageBlock;
	log_pages->address = Block_manager::instance()->get_free_block(LOG, event);

	LogPageBlock *next = log_pages;
	for (uint i=0;i<FAST_LOG_PAGE_LIMIT-1;i++)
	{
		LogPageBlock *newLPB = new LogPageBlock();
		newLPB->address = Block_manager::instance()->get_free_block(LOG, event);
		next->next = newLPB;
		next = newLPB;
	}
}
enum status FtlImpl_Fast::read(Event &event)
{
	initialize_log_pages();

	// Find block
	long lookupBlock = (event.get_logical_address() >> addressShift);
	uint lbnOffset = event.get_logical_address() % BLOCK_SIZE;

	Address eventAddress = Address(event.get_logical_address(), PAGE);

	LogPageBlock *currentBlock = log_pages;

	bool found = false;
	while (!found && currentBlock != NULL)
	{
		for (int i=0;i<currentBlock->numPages;i++)
		{
			//event.incr_time_taken(RAM_READ_DELAY);

			if (currentBlock->aPages[i] == (long)event.get_logical_address())
			{
				Address readAddress = Address(currentBlock->address.get_linear_address() + i, PAGE);
				event.set_address(readAddress);

				// Cancel the while and for loop
				found = true;
				break;
			}
		}

		currentBlock = currentBlock->next;
	}

	if (!found)
	{
		if (sequential_logicalblock_address == lookupBlock && sequential_offset > lbnOffset)
		{
			event.set_address(Address(sequential_address.get_linear_address() + lbnOffset, PAGE));
		}
		else if (data_list[lookupBlock] != -1) // If page is in the data block
		{
			event.set_address(Address(data_list[lookupBlock] + lbnOffset , PAGE));
		} else { // Empty
			event.set_address(Address(0, PAGE));
			event.set_noop(true);
		}
	}

	//printf("Reading %li for %lu\n", event.get_address().get_linear_address(), event.get_logical_address());

	// Statistics
	controller.stats.numFTLRead++;

	return controller.issue(event);
}

enum status FtlImpl_Fast::write(Event &event)
{
	initialize_log_pages();

	long logicalBlockAddress = event.get_logical_address() >> addressShift;
	Address eventAddress = Address(event.get_logical_address(), PAGE);

	pin_list[event.get_logical_address()] = true;

	uint lbnOffset = event.get_logical_address() % BLOCK_SIZE;

	// if a collision occurs at offset of the data block of pbn.
	if (data_list[logicalBlockAddress] == -1)
	{
		Address newBlock = Block_manager::instance()->get_free_block(DATA, event);

		// Register the mapping
		data_list[logicalBlockAddress] = newBlock.get_linear_address();

		// Store it in the right offset and save to event
		newBlock += lbnOffset;
		newBlock.valid = PAGE;

		event.set_address(newBlock);
	} else {

		Address dataAddress = Address(data_list[logicalBlockAddress]+lbnOffset, PAGE);

		if (get_state(dataAddress) == EMPTY)
			event.set_address(dataAddress);
		else
			write_to_log_block(event, logicalBlockAddress);
	}

	// Insert go sarbage collection
	Block_manager::instance()->insert_events(event);

	// Statistics
	controller.stats.numFTLWrite++;

	//printf("Writing %li for %lu\n", event.get_address().get_linear_address(), event.get_logical_address());

	return controller.issue(event);
}

enum status FtlImpl_Fast::trim(Event &event)
{
	initialize_log_pages();

	// Find block
	long lookupBlock = (event.get_logical_address() >> addressShift);
	uint lbnOffset = event.get_logical_address() % BLOCK_SIZE;

	Address eventAddress = Address(event.get_logical_address(), PAGE);

	LogPageBlock *currentBlock = log_pages;

	bool found = false;
	while (!found && currentBlock != NULL)
	{
		for (int i=0;i<currentBlock->numPages;i++)
		{
			if (currentBlock->aPages[i] == (long)event.get_logical_address())
			{
				Address address = Address(currentBlock->address.get_linear_address() + i, PAGE);
				Block *block = controller.get_block_pointer(address);
				block->invalidate_page(address.page);

				currentBlock->aPages[i] = -1;

				if (block->get_state() == INACTIVE) // All pages invalid, force an erase. PTRIM style.
				{
					Block_manager::instance()->erase_and_invalidate(event, currentBlock->address, LOG);
					data_list[lookupBlock] = -1;
				}

				// Cancel the while and for loop
				found = true;
				break;
			}
		}

		currentBlock = currentBlock->next;
	}

	if (!found)
	{
		if (sequential_logicalblock_address == lookupBlock && sequential_offset > lbnOffset)
		{
			Address address = Address(sequential_address.get_linear_address() + lbnOffset, PAGE);
			Block *block = controller.get_block_pointer(address);
			block->invalidate_page(address.page);

			if (block->get_state() == INACTIVE) // All pages invalid, force an erase. PTRIM style.
			{
				Block_manager::instance()->erase_and_invalidate(event, address, LOG);
				sequential_logicalblock_address = -1;
			}

		}
		else if (data_list[lookupBlock] != -1) // If page is in the data block
		{
			Address address = Address(data_list[lookupBlock] + lbnOffset , PAGE);

			Block *block = controller.get_block_pointer(address);
			block->invalidate_page(address.page);

			if (block->get_state() == INACTIVE) // All pages invalid, force an erase. PTRIM style.
			{
				Block_manager::instance()->erase_and_invalidate(event, address, LOG);
				data_list[lookupBlock] = -1;
			}
		}
	}

	event.set_noop(true);
	event.set_address(Address(0, PAGE));

	// Insert garbage collection
	Block_manager::instance()->insert_events(event);

	// Statistics
	controller.stats.numFTLTrim++;

	return controller.issue(event);
}

void FtlImpl_Fast::switch_sequential(Event &event)
{
	// Add to empty list i.e. switch without erasing the datablock.

	if (data_list[sequential_logicalblock_address] != -1)
		Block_manager::instance()->invalidate(Address(data_list[sequential_logicalblock_address], BLOCK), DATA);

	data_list[sequential_logicalblock_address] = sequential_address.get_linear_address();

	update_map_block(event);

	controller.stats.numLogMergeSwitch++;
}

void FtlImpl_Fast::merge_sequential(Event &event)
{
	if (sequential_logicalblock_address == -1)
		return;

	// Do merge (n reads, n writes and 2 erases (gc'ed))
	Address eventAddress = Address(event.get_logical_address(), PAGE);

	Address newDataBlock = Block_manager::instance()->get_free_block(DATA, event);
	//printf("Using new data block with address: %lu Block: %u\n", newDataBlock.get_linear_address(), newDataBlock.block);

	if (Block_manager::instance()->get_num_free_blocks() < 5)
		Block_manager::instance()->insert_events(event);

	for (uint i=0;i<BLOCK_SIZE;i++)
	{
		// Lookup page table and see if page exist in log page
		Address readAddress;

		Address seq = Address(sequential_address.get_linear_address() + i, PAGE);
		if (get_state(seq) == VALID)
			readAddress = seq;
		else if (data_list[sequential_logicalblock_address] != -1 && get_state(Address(data_list[sequential_logicalblock_address] + i, PAGE)) == VALID)
			readAddress.set_linear_address(data_list[sequential_logicalblock_address] + i, PAGE);
		else
			continue; // Empty page

		Event readEvent = Event(READ, event.get_logical_address(), 1, event.get_start_time());
		readEvent.set_address(readAddress);
		if (controller.issue(readEvent) == FAILURE) { printf("Read failed\n"); return; }

		Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time()+readEvent.get_time_taken());
		writeEvent.set_payload((char*)page_data + readAddress.get_linear_address() * PAGE_SIZE);
		writeEvent.set_address(Address(newDataBlock.get_linear_address() + i, PAGE));
		if (controller.issue(writeEvent) == FAILURE) {  printf("Write failed\n"); return; }

		event.incr_time_taken(writeEvent.get_time_taken() + readEvent.get_time_taken());

		// Statistics
		controller.stats.numFTLRead++;
		controller.stats.numFTLWrite++;
		controller.stats.numWLRead++;
		controller.stats.numWLWrite++;
	}

	// Invalidate inactive pages
	Block_manager::instance()->invalidate(&sequential_address, DATA);
	if (data_list[sequential_logicalblock_address] != -1)
		Block_manager::instance()->invalidate(Address(data_list[sequential_logicalblock_address], BLOCK), DATA);

	// Update mapping
	data_list[sequential_logicalblock_address] = newDataBlock.get_linear_address();

	controller.stats.numLogMergeFull++;

	update_map_block(event);
}

bool FtlImpl_Fast::random_merge(LogPageBlock *logBlock, Event &event)
{
	std::map<long, bool> mergeBlocks;

	// Find blocks to merge
	for (int i=0;i<logBlock->numPages;i++)
	{
		event.incr_time_taken(RAM_READ_DELAY);

		long victimLBA = (logBlock->aPages[i] >> addressShift);
		if (victimLBA != -1)
			mergeBlocks[victimLBA] = true;
	}

	bool pinned[BLOCK_SIZE];

	typedef std::map<long, bool>::const_iterator CI;

	// Go though all the required merges
	for (CI m = mergeBlocks.begin(); m!=mergeBlocks.end(); ++m)
	{
		for (uint i=0;i<BLOCK_SIZE;++i)
			pinned[i] = false;

		if (Block_manager::instance()->get_num_free_blocks() < 5)
			Block_manager::instance()->insert_events(event);

		Address mergeAddress = Block_manager::instance()->get_free_block(DATA, event);

		long victimLBA = m->first;
		if (victimLBA == -1)
			continue;
		// Find the last block and then the next last etc.
		for (int logblockNr = FAST_LOG_PAGE_LIMIT; logblockNr > 0; logblockNr--)
		{
			LogPageBlock *lpb = log_pages;
			for (int i = 0;i<logblockNr-1;i++)
				lpb = lpb->next;

			// Go though the pages and see if any falls into the same category as the current logical block
			for (int i=lpb->numPages-1;i>0;i--)
			{
				event.incr_time_taken(RAM_READ_DELAY);

				if (lpb->aPages[i] == -1u)
					continue;

				// See if there is a conflict, if there isn't. Read and rewrite the page.
				long currentLBA = (lpb->aPages[i] >> addressShift);
				if (victimLBA == currentLBA)
				{
					Address writeAddress = Address(mergeAddress.get_linear_address() + (lpb->aPages[i]%BLOCK_SIZE), PAGE);
					if (pinned[lpb->aPages[i]%BLOCK_SIZE])
					{
						lpb->aPages[i] = -1;
					}
					else if (get_state(writeAddress) == EMPTY)
					{
						// Read the active log address
						Event readEvent = Event(READ, event.get_logical_address(), 1, event.get_start_time());
						Address readAddress = Address(lpb->address.get_linear_address()+i, PAGE);
						readEvent.set_address(readAddress);

						if (controller.issue(readEvent) == FAILURE) { printf("failed\n"); return false; }
						//event.consolidate_metaevent(readEvent);

						Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time()+readEvent.get_time_taken());
						writeEvent.set_payload((char*)page_data + readAddress.get_linear_address() * PAGE_SIZE);
						writeEvent.set_address(writeAddress);

						if (controller.issue(writeEvent) == FAILURE) { printf("failed\n"); return false; }
						//event.consolidate_metaevent(writeEvent);
						event.incr_time_taken(writeEvent.get_time_taken() + readEvent.get_time_taken());

						pinned[lpb->aPages[i]%BLOCK_SIZE] = true;

						// Statistics
						controller.stats.numFTLRead++;
						controller.stats.numFTLWrite++;
						controller.stats.numWLRead++;
						controller.stats.numWLWrite++;
					}
				}
			}
		}

		// Merge the data block with the pages from the log
		for (uint i=0;i<BLOCK_SIZE;i++)
		{
			event.incr_time_taken(RAM_READ_DELAY);

			Address writeAddress = Address(mergeAddress.get_linear_address() + i, PAGE);
			if (get_state(writeAddress) == EMPTY && pinned[i] == false)
			{
				Address readAddress = Address(data_list[victimLBA] + i, PAGE);
				if (get_state(readAddress) == VALID)
				{
					Event readEvent = Event(READ, event.get_logical_address(), 1, event.get_start_time());
					readEvent.set_address(readAddress);
					if (controller.issue(readEvent) == FAILURE) { printf("failed\n"); return false;	}
					//event.consolidate_metaevent(readEvent);

					// Write the page to merge address
					Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time()+readEvent.get_time_taken());
					writeEvent.set_payload((char*)page_data + readAddress.get_linear_address() * PAGE_SIZE);
					writeEvent.set_address(writeAddress);
					if (controller.issue(writeEvent) == FAILURE) { printf("failed\n"); return false;	}
					//event.consolidate_metaevent(writeEvent);

					event.incr_time_taken(writeEvent.get_time_taken() + readEvent.get_time_taken());

					pinned[i] = true;

					// Statistics
					controller.stats.numFTLRead++;
					controller.stats.numFTLWrite++;
					controller.stats.numWLRead++;
					controller.stats.numWLWrite++;
				}
			}
		}

		// Invalidate inactive pages
		Block_manager::instance()->invalidate(Address(data_list[victimLBA], BLOCK), DATA);

		data_list[victimLBA] = mergeAddress.get_linear_address();

	}

	controller.stats.numLogMergeFull++;

	update_map_block(event);

	return true;
}

bool FtlImpl_Fast::write_to_log_block(Event &event, long logicalBlockAddress)
{
	uint lbnOffset = event.get_logical_address() % BLOCK_SIZE;
	if (lbnOffset == 0) /* Case 1 in Figure 5 */
	{
		if (sequential_offset == BLOCK_SIZE)
		{
			/* The log block is filled with sequentially written sectors
			 * Perform switch operation
			 * After switch, the data block is erased and returned to the free-block list
			 */
			switch_sequential(event);
		} else {
			/* Before merge, a new block is allocated from the free-block list
			 * merge the SW log block with its corresponding data block
			 * after merge, the two blocks are erased and returned to the free-block list
			 */
			merge_sequential(event);
		}

		/* Get a block from the free-block list and use it as a SW log block
		 * Append data to the SW log block
		 * Update the SW log block part of the sector mapping table
		 */

		sequential_offset = 1;
		sequential_address = Block_manager::instance()->get_free_block(DATA, event);
		sequential_logicalblock_address = logicalBlockAddress;

		event.set_address(sequential_address);
	} else {
		if (sequential_logicalblock_address == logicalBlockAddress) // If the current owner for the SW log block is the same with lbn
		{
			// last_lsn = getLastLsnFromSMT(lbn) Sector mapping table

			if (lbnOffset == sequential_offset)// lsn is equivalent with (last_lsn+1)
			{
				// Append data to the SW log block
				Address seq = sequential_address;
				controller.get_free_page(seq);
				event.set_address(seq);

				sequential_offset++;

			} else {
				// Merge the SW log block with its corresponding data block
				// Get a block from the free-block list and use it as a SW log block
				merge_sequential(event);

				sequential_offset = 1;
				sequential_address = Block_manager::instance()->get_free_block(DATA, event);
				sequential_logicalblock_address = logicalBlockAddress;

				// Append data to the SW log block
				event.set_address(sequential_address);
			}
			// Update the SW log block part of the sector mapping table
		} else {
			if (log_page_next == FAST_LOG_PAGE_LIMIT*BLOCK_SIZE) // There are no room in the RW log lock to write data
			{
				/*
				 * Select the first block of the RW log block list as a victim
				 * merge the victim with its corresponding data block
				 * get a block from the free block list and add it to the end of the RW log block list
				 * update the RW log block part of the sector-mapping table
				 */

				LogPageBlock *victim = log_pages;

				random_merge(victim, event);

				// Maintain the log page list
				log_pages = log_pages->next;
				Block_manager::instance()->invalidate(&victim->address, LOG);
				delete victim;

				// Create new LogPageBlock and append it to the log_pages list.
				LogPageBlock *newLPB = new LogPageBlock();
				newLPB->address = Block_manager::instance()->get_free_block(LOG, event);

				LogPageBlock *next = log_pages;
				while (next->next != NULL) next = next->next;

				next->next = newLPB;

				log_page_next -= BLOCK_SIZE;
			}

			// Append data to the RW log blocks.
			LogPageBlock *victim = log_pages;
			while (victim->numPages == (int)BLOCK_SIZE)
				victim = victim->next;

			victim->aPages[log_page_next % BLOCK_SIZE] = event.get_logical_address();
			victim->numPages++;

			Address rw = victim->address;
			rw.valid = PAGE;
			rw += log_page_next % BLOCK_SIZE;
			event.set_address(rw);

			log_page_next++;
		}
	}

	return true;
}

void FtlImpl_Fast::update_map_block(Event &event)
{
	Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time());
	writeEvent.set_address(Address(0, PAGE));
	writeEvent.set_noop(true);

	controller.issue(writeEvent);

	event.incr_time_taken(writeEvent.get_time_taken());

	controller.stats.numGCWrite++;
	controller.stats.numFTLWrite++;
}


void FtlImpl_Fast::print_ftl_statistics()
{
	Block_manager::instance()->print_statistics();
}

