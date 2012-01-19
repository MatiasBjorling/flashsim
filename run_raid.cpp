/* Copyright 2012 Matias Bjørling */

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

#include "ssd.h"

#define SIZE 10

using namespace ssd;

int main()
{
	load_config();
	print_config(NULL);

	RaidSsd *ssd = new RaidSsd();

	double result;
	double cur_time = 1;

	for (int i = 0; i < SIZE; i++)
	{
		result = ssd -> event_arrive(WRITE, i*2, 1, 0);
		cur_time += result;
	}
	for (int i = 0; i < SIZE; i++)
	{
		result = ssd -> event_arrive(READ, i*2, 1, 0);
		cur_time += result;
	}

	printf("Total execution time %f\n", cur_time);

	delete ssd;
	return 0;
}
