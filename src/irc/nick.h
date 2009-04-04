/*
 * Copyright(C) 2009 Romain Bignon
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef NICK_H
#define NICK_H

#include <string>

class Nick
{
	std::string nickname, ident, hostname;

public:

	Nick(std::string nick, std::string ident, std::string hostname);
	~Nick();

	std::string getNickname() const { return nickname; }
	std::string getIdent() const { return ident; }
	std::string getHostname() const { return hostname; }
};

#endif /* NICK_H */
