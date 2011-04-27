/* Copyright 2011 Matias Bjørling */

/* bdftp_ftl.cpp  */

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

/* Implementation of BDFTL. A block-level optimization for DFTL.
 *
 * Global Mapping Table GMT
 * Global Translation Directory GTD (Maintained in memory)
 * Cached Mapping Table CMT (Uses LRU to pick victim)
 *
 * Dlpn/Dppn Data Logical/Physical Page Number
 * Mlpn/Mppn Translation Logical/Physical Page Number
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

FtlImpl_BDftl::BPage::BPage()
{
	this->pbn = -1;
	nextPage = 0;
	optimal = true;
}

FtlImpl_BDftl::FtlImpl_BDftl(Controller &controller):
	FtlImpl_DftlParent(controller)
{

	uint ssdBlockSize = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE;
	block_map = new BPage[ssdBlockSize];

	// TODO: Add SSDBlockSize to the calculation.
	//printf("Total size to map: %uKB\n", ssdSize * PAGE_SIZE / 1024);
	printf("Using BDFTL.\n");
	return;
}



FtlImpl_BDftl::~FtlImpl_BDftl(void)
{
	delete block_map;
	return;
}

enum status FtlImpl_BDftl::read(Event &event)
{
	uint dlpn = event.get_logical_address();
	uint dlbn = dlpn / BLOCK_SIZE;

	// Block-level lookup
	if (block_map[dlbn].optimal == false)
	{
		// DFTL lookup
		resolve_mapping(event, false);

		event.set_address(Address(trans_map[dlpn].ppn, PAGE));
	} else {
		uint dppn = block_map[dlbn].pbn + (dlpn % BLOCK_SIZE);

		event.set_address(Address(dppn, PAGE));
	}

	controller.stats.numMemoryRead += 2; // Block-level lookup + range check
	controller.stats.numFTLRead++; // Page read

	if (controller.issue(event) == FAILURE)
		return FAILURE;

	event.consolidate_metaevent(event);

	return SUCCESS;
}


enum status FtlImpl_BDftl::write(Event &event)
{
	uint dlpn = event.get_logical_address();
	uint dlbn = dlpn / BLOCK_SIZE;

	// Block-level lookup
	if (block_map[dlbn].optimal == false)
	{
		// DFTL lookup
		resolve_mapping(event, true);

		// Get next available data page
		trans_map[dlpn].ppn = get_free_data_page();

		// Finish DFTL logic
		event.set_address(Address(trans_map[dlpn].ppn, PAGE));
	} else {
		// Optimised case for block level lookup

		// Get new block if necessary
		if (block_map[dlbn].pbn == -1u)
			block_map[dlbn].pbn = manager.get_free_block(DATA).get_linear_address();

		unsigned char dppn = dlpn % BLOCK_SIZE;
		if (block_map[dlbn].nextPage == dppn)
		{
			controller.stats.numMemoryWrite++; // Update next page
			event.incr_time_taken(RAM_WRITE_DELAY);
			block_map[dlbn].nextPage++;
			event.set_address(Address(block_map[dlbn].pbn + dppn, PAGE));
		} else {
			// Transfer the block to DFTL.
			printf("not yet implemented\n");
		}
	}

	controller.stats.numMemoryRead += 3; // Block-level lookup + range check + optimal check
	event.incr_time_taken(RAM_READ_DELAY*3);
	controller.stats.numFTLWrite++; // Page reads

	if (controller.issue(event) == FAILURE)
		return FAILURE;

	event.consolidate_metaevent(event);

	return SUCCESS;
}
