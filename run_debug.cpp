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

/* Interactive debugger.
 * Yishuai Li
 * Dec 29, 2015 */

#include <ctime>
#include <iostream>
#include "ssd.h"

using namespace ssd;

/* Input Format

   Each request consists of two or three fields, separated by whitespaces:
   1. One character, representing the type of I/O: (R)ead, (W)rite, (T)rim;
   2. One integer, representing the virtual address;
   3. (Write only) One integer, representing the data written.

 * Output Format

   For read requests, the program responds with two integers: the data read, and
   the physical address being read.
 */

void debug(Ssd& ssd) throw (std::invalid_argument)
{
	char ioType;
	ulong vaddr;
	char buffer[PAGE_SIZE];
	while (std::cin >> ioType >> vaddr)
	{
		event_type type;
		switch (ioType)
		{
			case 'R':
			case 'r':
				type = READ;
				break;
			case 'W':
			case 'w':
				type = WRITE;
				std::cin >> *(int*) buffer;
				break;
			case 'T':
			case 't':
				type = TRIM;
				break;
			default:
				throw std::invalid_argument("Invalid I/O type!");
		}
		global_buffer = nullptr;
		ssd.event_arrive(type, vaddr, 1, time(NULL), buffer);
		if (type == READ)
		{
			std::cout << (global_buffer ? *(int*) global_buffer : 0) << '\t';
			std::cout << global_buffer << std::endl;
		}
	}
}

int main() throw (std::invalid_argument)
{
	load_config();
	print_config(stderr);

	Ssd ssd;

	std::clog << "INITIALIZING SSD" << std::endl;

	debug(ssd);
}

