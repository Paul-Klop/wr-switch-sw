/*
 * White Rabbit MAC utils
 * Copyright (C) 2010, 2017 CERN.
 *
 * Version:     wrsw_rtud v1.0
 *
 * Authors:     Tomasz Wlostowski (tomasz.wlostowski@cern.ch)
 * Authors:     Adam Wujek (adam.wujek@cern.ch)
 *
 * Description: MAC address type related operations.
 *
 * Fixes:		Benoit Rat
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <libwr/mac.h>
#include <stdio.h>
#include <ctype.h>

/**
 * \brief Helper function to convert mac address into a string
 * WARNING: this returns static storage
 */
char *mac_to_string(uint8_t mac[ETH_ALEN])
{
	static char str[40];
	snprintf(str, 40, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
		 mac[2], mac[3], mac[4], mac[5]);
	return str;
}

/**
 * \brief Write mac address into a buffer to avoid concurrent access on static variable.
 */
char *mac_to_buffer(uint8_t mac[ETH_ALEN], char buffer[ETH_ALEN_STR])
{
	if (mac && buffer)
		snprintf(buffer, ETH_ALEN_STR, "%02x:%02x:%02x:%02x:%02x:%02x",
			 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return buffer;
}

/**
 * \brief Write mac address into a buffer to avoid concurrent access on static variable.
 */
char *mac_to_buffer_no_colons(uint8_t mac[ETH_ALEN], char buffer[2*ETH_ALEN+1])
{
	if (mac && buffer)
		snprintf(buffer, ETH_ALEN_STR, "%02x%02x%02x%02x%02x%02x",
			 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return buffer;
}

/**
 * \brief Function to retrieve mac address from text input (argument in terminal)
 */
int mac_from_str(uint8_t *tomac, const char *fromstr)
{
	if (tomac == 0 || fromstr == 0)
		return -1;
	return sscanf(fromstr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", tomac + 0,
		      tomac + 1, tomac + 2, tomac + 3, tomac + 4, tomac + 5);
}

/**
 * \brief Function to verify correctness of MAC address
 */
int mac_verify(char *mac_str)
{
	uint8_t mac[ETH_ALEN];
	char buffer[ETH_ALEN_STR];

	/* Check the length of a string containing MAC */
	if (ETH_ALEN_STR == strnlen(mac_str, ETH_ALEN_STR) + 1) {
		/* Convert string to mac */
		mac_from_str(mac, mac_str);
		/* Convert mac to string */
		mac_to_buffer(mac, buffer);
		/* Compare original string with the converted back and forth
		 * they should be the same */
		if (!memcmp(mac_str, buffer, ETH_ALEN_STR)) {
			/* MAC is ok */
			return 0;
		}
	}
	return -1;
}


/**
 * \brief Function to lowercase the mac address
 */
int mac_to_lower(char *mac_str)
{
	int i;

	if (ETH_ALEN_STR != strnlen(mac_str, ETH_ALEN_STR) + 1)
		return -1;

	for (i = 0; i < ETH_ALEN_STR; i++) 
		mac_str[i] = tolower(mac_str[i]);
	return 0;
}
