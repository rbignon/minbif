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

#include <libpurple/purple.h>
#include <cassert>

#include "purple.h"
#include "purple_handler.h"
#include "im.h"
#include "../version.h"
#include "../log.h"

IM* Purple::im = NULL;


void Purple::Init(IM* im)
{
	if(Purple::im)
	{
		b_log[W_ERR] << "These is already a purple instance!";
		throw PurpleError();
	}
	purple_util_set_user_dir(im->getUserPath().c_str());

	PurpleHandler::Init();

	if (!purple_core_init(BITLBEE_VERSION_NAME))
	{
		b_log[W_ERR] << "Initialization of the Purple core failed.";
		throw PurpleError();
	}

	Purple::im = im;
}

void Purple::Uninit()
{
	assert(im != NULL);

	purple_core_quit();
}
