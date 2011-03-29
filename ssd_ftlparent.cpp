/* Copyright 2011 Matias Bjørling */

/* ssd_ftlparent.cpp  */

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

/*
 * Implements parent interface for all FTL implementations to use.
 */

#include "ssd.h"

using namespace ssd;


FtlParent::FtlParent(Controller &controller) : controller(controller), manager(*this)
{
	return;
}


ssd::ulong FtlParent::get_erases_remaining(const Address &address) const
{
	return controller.get_erases_remaining(address);
}

void FtlParent::get_least_worn(Address &address) const
{
	controller.get_least_worn(address);
	return;
}

enum page_state FtlParent::get_state(const Address &address) const
{
	return controller.get_state(address);
}

enum block_state FtlParent::get_block_state(const Address &address) const
{
	return controller.get_block_state(address);
}
