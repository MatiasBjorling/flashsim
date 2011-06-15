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
	return;
}

FtlImpl_Page::~FtlImpl_Page(void)
{

	return;
}

enum status FtlImpl_Page::read(Event &event)
{
	event.set_address(Address(0, PAGE));
	event.set_noop(true);

	controller.issue(event);

	return SUCCESS;
}

enum status FtlImpl_Page::write(Event &event)
{
	event.set_address(Address(1, PAGE));
	event.set_noop(true);

	controller.issue(event);

	return SUCCESS;
}

enum status FtlImpl_Page::trim(Event &event)
{
	return SUCCESS;
}
