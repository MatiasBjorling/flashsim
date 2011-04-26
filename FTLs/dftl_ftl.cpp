/* Copyright 2011 Matias Bjørling */

/* dftp_ftl.cpp  */

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

/* Implementation of the DFTL described in the paper
 * "DFTL: A Flasg Translation Layer Employing Demand-based Selective Caching og Page-level Address Mappings"
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

FtlImpl_Dftl::MPage::MPage(long vpn, long ppn, double create_ts)
{
	this->vpn = vpn;
	this->ppn = ppn;
	this->create_ts = create_ts;
	this->modified_ts = create_ts;
}

FtlImpl_Dftl::MPage::MPage()
{
	this->vpn = 0;
	this->ppn = -1;
	this->create_ts = -1;
	this->modified_ts = -1;
}


FtlImpl_Dftl::FtlImpl_Dftl(Controller &controller):
	FtlParent(controller)
{
	// Trivial assumption checks
	if (sizeof(int) != 4) assert("integer is not 4 bytes");

	addressPerPage = 0;
	addressSize = 32;

	currentDataPage = -1;
	currentTranslationPage = -1;

	// Detect required number of bits for logical address size
	//for (int size = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * 4; size > 0; addressSize++) size /= 2;

	// Find required number of bits for block size
	addressPerPage = (PAGE_SIZE/ceil(addressSize / 8.0)); // 8 bits per byte.

	printf("Total required bits for representation: Address size: %i Total per page: %i \n", addressSize, addressPerPage);

	totalCMTentries = CACHE_DFTL_LIMIT * addressPerPage;
	printf("Number of elements in Cached Mapping Table (CMT): %i\n", totalCMTentries);

	// Initialise block mapping table.
	uint ssdSize = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE;

	trans_map = new MPage[ssdSize];

	printf("Total size to map: %uKB\n", ssdSize * PAGE_SIZE / 1024);
	printf("Using DFTL.\n");
	return;
}


FtlImpl_Dftl::MPage &FtlImpl_Dftl::select_victim_entry()
{
	MPage evictPage;

	std::map<long, MPage*>::iterator i = cmt.begin();
	while (i != cmt.end())
	{
		MPage *tmpMPage = (*i).second;
		if (evictPage.modified_ts > tmpMPage->modified_ts)
			evictPage = *tmpMPage;
		++i;
	}

	return evictPage;
}

FtlImpl_Dftl::MPage &FtlImpl_Dftl::consult_GTD(long dlpn, Event &event)
{
	MPage *mpage;

	// TODO: Add translation page counter

	// Lookup mapping page
	if (trans_map[dlpn].ppn == -1)
	{
		trans_map[dlpn].vpn = dlpn;
		trans_map[dlpn].create_ts = event.get_start_time();
		trans_map[dlpn].modified_ts = event.get_start_time();
	}

	mpage = &trans_map[dlpn];

	// Simulate that we goto translation map and read the mapping page.
	Event *readEvent = new Event(READ, event.get_logical_address(), 1, event.get_start_time());
	readEvent->set_address(Address(mpage->ppn, PAGE));
	readEvent->set_noop(true);

	Event *lastEvent = event.get_last_event(event);
	lastEvent->set_next(*readEvent);

	return *mpage;
}

void FtlImpl_Dftl::reset_MPage(FtlImpl_Dftl::MPage &mpage)
{
	mpage.create_ts = -1;
	mpage.modified_ts = -1;
	mpage.ppn = -1;
}

bool FtlImpl_Dftl::lookup_CMT(long dlpn, FtlImpl_Dftl::MPage &mpage)
{
	if (cmt.find(dlpn) == cmt.end())
		return false;

	mpage = *cmt[dlpn];

	return true;
}

long FtlImpl_Dftl::get_free_translation_page()
{
	if (currentTranslationPage == -1 || currentTranslationPage % BLOCK_SIZE == BLOCK_SIZE -1)
		currentTranslationPage = manager.get_free_block(LOG).get_linear_address();
	else
		currentTranslationPage++;

	return currentTranslationPage;
}

long FtlImpl_Dftl::get_free_data_page()
{
	if (currentDataPage == -1 || currentDataPage % BLOCK_SIZE == BLOCK_SIZE -1)
		currentDataPage = manager.get_free_block(DATA).get_linear_address();
	else
		currentDataPage++;

	return currentDataPage;
}

FtlImpl_Dftl::~FtlImpl_Dftl(void)
{
	delete trans_map;


	return;
}

enum status FtlImpl_Dftl::read(Event &event)
{
	return controller.issue(event);
}


enum status FtlImpl_Dftl::write(Event &event)
{
	printf("write IO: %u\n", event.get_logical_address());
	/* 1. Lookup in CMT if the mapping exist
	 * 2. If, then serve
	 * 3. If not, then goto GDT, lookup page
	 * 4. If CMT full, evict a page
	 * 5. Add mapping to CMT
	 * 6. Serve request
	 */

	MPage mapping;
	if (lookup_CMT(event.get_logical_address(),mapping))
	{
		mapping.modified_ts = event.get_start_time();

		// Inform the ssd model that it should invalidate the previous page.
		Address killAddress = Address(mapping.ppn, PAGE);
		event.set_replace_address(killAddress);
	} else {
		mapping = consult_GTD(event.get_logical_address(), event);

		if (cmt.size() >= 100 /* totalCMTentries */)
		{
			// Find page to evict
			MPage evictPage = select_victim_entry();

//			if (evictPage->create_ts != evictPage->modified_ts)
//			{
//				// Evict page
//				// Inform the ssd model that it should invalidate the previous page.
//				Address killAddress = Address(evictPage->ppn, PAGE);
//				event.set_replace_address(killAddress);
//
//				evictPage->ppn = get_free_data_page();
//			}

			// Remove page from cache.
			reset_MPage(evictPage);
			cmt.erase(evictPage.vpn);

			printf("Erased vpn: %li\n", evictPage.vpn);
		}

		cmt[mapping.vpn] = &mapping;
		printf("Writing vpn: %li\n", mapping.vpn);
	}

	printf("Size of cmt: %i\n", cmt.size());

	// Get next available data page
	mapping.ppn = get_free_data_page();

	event.set_address(Address(mapping.ppn, PAGE));

	if (controller.issue(event) == FAILURE)
		return FAILURE;

	event.consolidate_metaevent(event);

	return SUCCESS;
}
