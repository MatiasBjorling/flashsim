/* fast_ftl.cpp
 *
 * Copyright 2011 Matias Bjørling
 *
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
#include "../ssd.h"

using namespace ssd;

FtlImpl_Fast::FtlImpl_Fast(Controller &controller):
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
		data_list[i] = -1;

	// SW
	sequential_offset = 0;
	sequential_logicalblock_address = -1;

	log_page_next = 0;

	printf("Total mapping table size: %luKB\n", numBlocks * sizeof(uint) / 1024);
	printf("Using FAST FTL.\n");
	return;
}

FtlImpl_Fast::~FtlImpl_Fast(void)
{
	delete data_list;
	delete log_pages;

	return;
}

void FtlImpl_Fast::initialize_log_pages()
{
	if (log_pages != NULL)
		return;

	// RW
	log_pages = new LogPageBlock;
	log_pages->address = manager.get_free_block(LOG);

	LogPageBlock *next = log_pages;
	for (uint i=0;i<FAST_LOG_PAGE_LIMIT-1;i++)
	{
		LogPageBlock *newLPB = new LogPageBlock();
		newLPB->address = manager.get_free_block(LOG);
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

	Address eventAddress;
	eventAddress.set_linear_address(event.get_logical_address());

	LogPageBlock *currentBlock = log_pages;

	bool found = false;
	while (!found && currentBlock != NULL)
	{
		for (int i=0;i<currentBlock->numPages;i++)
		{
			event.incr_time_taken(RAM_READ_DELAY);

			if (currentBlock->aPages[i] == event.get_logical_address())
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
			manager.simulate_map_read(event);
		}
		else
		{
			printf("Address has not been written\n");
			return FAILURE;
		}
	}

	//printf("Reading %li for %lu\n", event.get_address().get_linear_address(), event.get_logical_address());

	// Insert garbage collection
	manager.insert_events(event);

	// Statistics
	controller.stats.numFTLRead++;

	return controller.issue(event);
}


void FtlImpl_Fast::allocate_new_logblock(LogPageBlock *logBlock, long logicalBlockAddress, Event &event)
{
	if (log_map.size() >= FAST_LOG_PAGE_LIMIT)
	{
		long exLogicalBlock = (*log_map.begin()).first;
		LogPageBlock *exLogBlock = (*log_map.begin()).second;

		printf("killing %li with address: %lu\n", exLogicalBlock, exLogBlock->address.get_linear_address());

//		if (!is_sequential(exLogBlock, exLogicalBlock, event))
//			random_merge(exLogBlock, exLogicalBlock, event);
	}

	logBlock = new LogPageBlock();
	logBlock->address = manager.get_free_block(LOG);

	//printf("Using new log block with address: %lu Block: %u at logical address: %li\n", logBlock->address.get_linear_address(), logBlock->address.block, logicalBlockAddress);
	log_map[logicalBlockAddress] = logBlock;
}

void FtlImpl_Fast::dispose_logblock(LogPageBlock *logBlock, long logicalBlockAddress)
{
	log_map.erase(logicalBlockAddress);
	delete logBlock;
}

void FtlImpl_Fast::switch_sequential(Event &event)
{
	// Add to empty list i.e. switch without erasing the datablock.

	if (data_list[sequential_logicalblock_address] != -1)
	{
		Address a = Address(data_list[sequential_logicalblock_address], BLOCK);
		manager.invalidate(a, DATA);
	}

	data_list[sequential_logicalblock_address] = sequential_address.get_linear_address();

	controller.stats.numLogMergeSwitch++;

	printf("Switch sequential\n");
}

void FtlImpl_Fast::merge_sequential(Event &event)
{
	if (sequential_logicalblock_address == -1)
		return;

	// Do merge (n reads, n writes and 2 erases (gc'ed))
	Address eventAddress;
	eventAddress.set_linear_address(event.get_logical_address());

	Address newDataBlock = manager.get_free_block(DATA);
	//printf("Using new data block with address: %lu Block: %u\n", newDataBlock.get_linear_address(), newDataBlock.block);

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
		Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time()+event.get_time_taken());

		readEvent.set_address(readAddress);
		readEvent.set_next(writeEvent);

		writeEvent.set_payload((char*)page_data + readAddress.get_linear_address() * PAGE_SIZE);
		writeEvent.set_address(Address(newDataBlock.get_linear_address() + i, PAGE));

		//printf("Merge. Writing %lu to %lu\n", readAddress.get_linear_address(), writeEvent.get_address().get_linear_address());

		if (controller.issue(readEvent) == FAILURE)
		{
			printf("failed\n");
			return;
		}

		event.consolidate_metaevent(readEvent);

		// Statistics
		controller.stats.numFTLRead++;
		controller.stats.numFTLWrite++;
	}

	// Invalidate inactive pages

	Address lBlock = Address(sequential_address);
	manager.invalidate(lBlock, LOG);
	if (data_list[sequential_logicalblock_address] != -1)
	{
		Address dBlock = Address(data_list[sequential_logicalblock_address], BLOCK);
		manager.invalidate(dBlock, DATA);
	}

	//printf("Moving %lu to %lu\n", sequential_logicalblock_address, newDataBlock.get_linear_address());

	// Update mapping
	data_list[sequential_logicalblock_address] = newDataBlock.get_linear_address();

	// Add erase events if necessary.
	manager.insert_events(event);

	controller.stats.numLogMergeFull++;

	printf("Merged sequential\n");

}

bool FtlImpl_Fast::random_merge(LogPageBlock *logBlock, Event &event)
{
	std::map<ulong, bool> mergeBlocks;

	// Find blocks to merge
	for (int i=0;i<logBlock->numPages;i++)
	{
		event.incr_time_taken(RAM_READ_DELAY);

		long victimLBA = (logBlock->aPages[i] >> addressShift);

		mergeBlocks[victimLBA] = true;
	}

	bool pinned[BLOCK_SIZE];

	typedef std::map<ulong, bool>::const_iterator CI;

	// Go though all the required merges
	for (CI m = mergeBlocks.begin(); m!=mergeBlocks.end(); ++m)
	{
		for (uint i=0;i<BLOCK_SIZE;++i)
			pinned[i] = false;

		Address mergeAddress = manager.get_free_block(DATA);

		long victimLBA = m->first;
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

				// See if there is a conflict, if there isn't. Read and write the page.
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
						Address readAddress = Address(lpb->address.get_linear_address()+i, PAGE);

						Event readEvent = Event(READ, event.get_logical_address(), 1, event.get_start_time());
						Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time()+event.get_time_taken());

						readEvent.set_address(readAddress);
						readEvent.set_next(writeEvent);

						writeEvent.set_payload((char*)page_data + readAddress.get_linear_address() * PAGE_SIZE);
						writeEvent.set_address(writeAddress);

						if (controller.issue(readEvent) == FAILURE)
						{
							printf("failed\n");
							return false;
						}

						event.consolidate_metaevent(readEvent);

						pinned[lpb->aPages[i]%BLOCK_SIZE] = true;

						// Statistics
						controller.stats.numFTLRead++;
						controller.stats.numFTLWrite++;
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
					Event writeEvent = Event(WRITE, event.get_logical_address(), 1, event.get_start_time()+event.get_time_taken());

					readEvent.set_address(readAddress);
					readEvent.set_next(writeEvent);

					// Write the page to merge address
					writeEvent.set_payload((char*)page_data + readAddress.get_linear_address() * PAGE_SIZE);
					writeEvent.set_address(writeAddress);

					if (controller.issue(readEvent) == FAILURE)
					{
						printf("failed\n");
						return false;
					}

					event.consolidate_metaevent(readEvent);

					pinned[i] = true;

					// Statistics
					controller.stats.numFTLRead++;
					controller.stats.numFTLWrite++;
				}
			}
		}

		// Invalidate inactive pages
		Address dBlock = new Address(data_list[victimLBA], BLOCK);
		manager.invalidate(dBlock, DATA);

		data_list[victimLBA] = mergeAddress.get_linear_address();

		std::cout << "Merged: " << m->first << "\n";
	}

	std::cout << "Finished.\n";

	controller.stats.numLogMergeFull++;

	// Add erase events if necessary.
	manager.insert_events(event);

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

		sequential_address = manager.get_free_block(DATA);
		sequential_logicalblock_address = logicalBlockAddress;
		sequential_offset = 1;

		Address seq = sequential_address;
		controller.get_free_page(seq);

		event.set_address(seq);
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
				sequential_address = manager.get_free_block(DATA);
				sequential_logicalblock_address = logicalBlockAddress;
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
				manager.invalidate(victim->address, LOG);
				delete victim;

				// Create new LogPageBlock and append it to the log_pages list.
				LogPageBlock *newLPB = new LogPageBlock();
				newLPB->address = manager.get_free_block(LOG);

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



enum status FtlImpl_Fast::write(Event &event)
{
	initialize_log_pages();

	long logicalBlockAddress = event.get_logical_address() >> addressShift;
	Address eventAddress = Address(event.get_logical_address(), PAGE);

	uint lbnOffset = event.get_logical_address() % BLOCK_SIZE;

	// if a collision occurs at offset of the data block of pbn.
	if (data_list[logicalBlockAddress] == -1)
	{
		Address newBlock = manager.get_free_block(DATA);

		// Register the mapping
		data_list[logicalBlockAddress] = newBlock.get_linear_address();

		// Store it in the right offset and save to event
		newBlock += lbnOffset;
		newBlock.valid = PAGE;

		event.set_address(newBlock);
	} else {

		Address dataAddress = Address(data_list[logicalBlockAddress]+lbnOffset, PAGE);

		if (get_state(dataAddress) == EMPTY)
		{
			event.set_address(dataAddress);
		}
		else
		{
			write_to_log_block(event, logicalBlockAddress);
		}
	}

	// Insert garbage collection
	manager.insert_events(event);

	// Add write events if necessary.
	manager.simulate_map_write(event);

	// Statistics
	controller.stats.numFTLWrite++;

	//printf("Writing %li for %lu\n", event.get_address().get_linear_address(), event.get_logical_address());

	return controller.issue(event);
}
