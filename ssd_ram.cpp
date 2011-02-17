/* Copyright 2009, 2010 Brendan Tauras */

/* ssd_ram.cpp is part of FlashSim. */

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

/* Ram class
 *
 * Brendan Tauras 2009-06-03
 *
 * This is a basic implementation that only provides delay updates to events
 * based on a delay value multiplied by the size (number of pages) needed to
 * be read or written.
 */

#include <assert.h>
#include <stdio.h>
#include "ssd.h"

using namespace ssd;

 
Ram::Ram(double read_delay, double write_delay):
	read_delay(read_delay),
	write_delay(write_delay)
{
	if(read_delay <= 0)
	{
		fprintf(stderr, "RAM: %s: constructor received negative read delay value\n\tsetting read delay to 0.0\n", __func__);
		read_delay = 0.0;
	}
	if(write_delay <= 0)
	{
		fprintf(stderr, "RAM: %s: constructor received negative write delay value\n\tsetting write delay to 0.0\n", __func__);
		write_delay = 0.0;
	}
	return;
}

Ram::~Ram(void)
{
	return;
}

enum status Ram::read(Event &event)
{
	assert(read_delay >= 0.0);
	(void) event.incr_time_taken(read_delay * event.get_size());
	return SUCCESS;
}

enum status Ram::write(Event &event)
{
	assert(write_delay >= 0.0);
	(void) event.incr_time_taken(write_delay * event.get_size());
	return SUCCESS;
}
