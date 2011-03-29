/* Copyright 2011 Matias Bjørling */

/* page_ftl.cpp  */

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

/* Implements a very simple page-level FTL without merge */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../ssd.h"

using namespace ssd;

FtlImpl_Page::FtlImpl_Page(Controller &controller):
	FtlParent(controller)
{
	currentPage = 0;

	uint numCells = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE;
	map = new long[numCells];
	for (uint i=0;i<numCells;i++)
		map[i] = -1;

	return;
}

FtlImpl_Page::~FtlImpl_Page(void)
{
	delete map;
	return;
}

enum status FtlImpl_Page::read(Event &event)
{
	if (map[event.get_logical_address()] == -1)
	{
		fprintf(stderr, "Page not written! Logical Address: %i\n", event.get_logical_address());
		return FAILURE;
	}

	// Lookup mapping
	event.set_address(resolve_logical_address(map[event.get_logical_address()]));

	page_state s = controller.get_state(event.get_address());

	fprintf(stderr, "CP: %u LP: %u\n", map[event.get_logical_address()], event.get_logical_address());
	if (s == VALID)
	{
		controller.issue(event);
	}
	else
	{
		fprintf(stderr, "Page warning: Not able to read page as it has not been written or is invalid.");
		return FAILURE;
	}


	return SUCCESS;
}

enum status FtlImpl_Page::write(Event &event)
{
	Address address = resolve_logical_address(currentPage);

	// Physical address of I/O.
	event.set_address(address);

	// Update mappings
	map[event.get_logical_address()] = currentPage;

	fprintf(stderr, "CP: %u LP: %u\n", map[event.get_logical_address()], event.get_logical_address());

	currentPage++;

	controller.issue(event);

	return SUCCESS;
}

inline Address FtlImpl_Page::resolve_logical_address(uint logicalAddress)
{
	uint numCells = SSD_SIZE * PACKAGE_SIZE * DIE_SIZE * PLANE_SIZE * BLOCK_SIZE;

	Address phyAddress;
	phyAddress.package = floor(logicalAddress / (numCells / SSD_SIZE));
	phyAddress.die = floor(logicalAddress / (numCells / SSD_SIZE / PACKAGE_SIZE));
	phyAddress.plane = floor(logicalAddress / (numCells / SSD_SIZE / PACKAGE_SIZE / DIE_SIZE));
	phyAddress.block = floor(logicalAddress / (numCells / SSD_SIZE / PACKAGE_SIZE / DIE_SIZE / PLANE_SIZE));
	phyAddress.page = logicalAddress % BLOCK_SIZE;
	phyAddress.valid = PAGE;

	fprintf(stderr, "numCells: %i package: %i die: %i plane: %i block: %i page: %i\n", numCells, phyAddress.package, phyAddress.die, phyAddress.plane, phyAddress.block, phyAddress.page);

	return phyAddress;
}

