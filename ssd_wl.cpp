/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_wl.cpp is part of FlashSim. */

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

/* Wear_leveler class
 * Brendan Tauras 2009-11-04
 *
 * This class is a stub class for the user to use as a template for implementing
 * his/her wear leveler scheme.  The wear leveler class was added to simplify
 * and modularize the wear-leveling in FTL schemes. */

#include <new>
#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

Wear_leveler::Wear_leveler(FtlParent &ftl)
{

	return;
}

Wear_leveler::~Wear_leveler(void)
{
	return;
}

enum status Wear_leveler::insert(const Address &address)
{
	return SUCCESS;
}
