/*
 * This file is part of Luna
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*******************************************************************************
 *
 *  Lua API utility functions (lua_util.h)
 *  ---
 *  Provide pushing and checking functions for internal structures
 *
 *  Created: 03.02.2012 02:25:34
 *
 ******************************************************************************/
#ifndef LUA_UTIL
#define LUA_UTIL

#include <lua.h>

#include "lua_manager.h"
#include "../user.h"
#include "../channel.h"


int api_push_script(lua_State *, luna_script *);
int api_push_channel(lua_State *, irc_channel *);
int api_push_user(lua_State *, irc_user *);
int api_push_luna_user(lua_State *, luna_user *);
int api_push_loglevels(lua_State *);
int api_push_sender(lua_State *, irc_sender *);

luna_state *api_getstate(lua_State *);
luna_user *api_checkuser(lua_State *, int index);

int api_setfield_s(lua_State *, int, const char *, const char *);
int api_setfield_n(lua_State *, int, const char *, double);

int script_cmp(void *, void *);
void script_free(void *);

int api_command_helper(lua_State *, const char *, ...);

#endif