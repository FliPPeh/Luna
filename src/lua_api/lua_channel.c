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
 *  Lua channel object management (lua_self.h)
 *  ---
 *  Provide access to the global channel list within scripts
 *
 *  Created: 03.02.2012 20:41:01
 *
 ******************************************************************************/
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "../net.h"
#include "../state.h"
#include "lua_util.h"
#include "lua_channel.h"


int luaX_channels_getall(lua_State*);
int luaX_channels_find(lua_State*);

int luaX_channel_getchannelinfo(lua_State*);
int luaX_channel_gettopic(lua_State*);
int luaX_channel_getusers(lua_State*);
int luaX_channel_finduser(lua_State*);

int luaX_chuser_getuserinfo(lua_State*);
int luaX_chuser_getstatus(lua_State*);
int luaX_chuser_getchannel(lua_State*);

irc_channel *find_channel_by_name(lua_State*, const char*);
irc_channel *luaX_check_irc_channel_ud(lua_State*, int);

static const struct luaL_reg luaX_channel_functions[] = {
    { "getChannelList", luaX_channels_getall },
    { "find", luaX_channels_find },

    { NULL, NULL }
};

static const struct luaL_reg luaX_channel_methods[] = {
    { "getChannelInfo", luaX_channel_getchannelinfo },
    { "getTopic", luaX_channel_gettopic },
    { "getUserList", luaX_channel_getusers },
    { "findUser", luaX_channel_finduser },
    { NULL, NULL }
};

static const struct luaL_reg luaX_chuser_methods[] = {
    { "getUserInfo", luaX_chuser_getuserinfo },
    { "getStatus", luaX_chuser_getstatus },
    { "getChannel", luaX_chuser_getchannel },
    { NULL, NULL }
};


irc_channel *
find_channel_by_name(lua_State *L, const char *c)
{
    luna_state *state = api_getstate(L);

    return (irc_channel *)list_find(state->channels, (void *)c, &channel_cmp);
}


irc_channel *
luaX_check_irc_channel_ud(lua_State *L, int ind)
{
    luna_state *s = api_getstate(L);

    luaX_channel *c = (luaX_channel *)luaL_checkudata(L, ind, "luna.channel");
    void *res = NULL;

    if ((res = list_find(s->channels, c->name, &channel_cmp)) == NULL)
        luaL_error(L, "no such channel '%s'", c->name);

    return (irc_channel *)res;
}


void
luaX_push_chanuser(lua_State *L, luaX_chanuser *cu)
{
    luaX_chanuser *u =
        (luaX_chanuser *)lua_newuserdata(L, sizeof(luaX_chanuser));

    luaL_getmetatable(L, "luna.channel.user");
    lua_setmetatable(L, -2);

    memcpy(u, cu, sizeof(luaX_chanuser));

    return;
}


void
luaX_push_channel(lua_State *L, luaX_channel *c)
{
    luaX_channel *u =
        (luaX_channel *)lua_newuserdata(L, sizeof(luaX_channel));

    luaL_getmetatable(L, "luna.channel");
    lua_setmetatable(L, -2);

    memcpy(u, c, sizeof(luaX_channel));

    return;
}


int
luaX_channel_getchannelinfo(lua_State *L)
{
    irc_channel *target = luaX_check_irc_channel_ud(L, 1);

    lua_pushstring(L, target->name);
    lua_pushnumber(L, target->created);

    return 2;
}


int
luaX_channel_gettopic(lua_State *L)
{
    irc_channel *target = luaX_check_irc_channel_ud(L, 1);

    lua_pushstring(L, target->topic);
    lua_pushnumber(L, target->topic_set);
    lua_pushstring(L, target->topic_setter);

    return 2;
}


int
luaX_channel_getusers(lua_State *L)
{
    int i = 1;
    int array;

    irc_channel *target = luaX_check_irc_channel_ud(L, 1);
    list_node *cur = NULL;

    array = (lua_newtable(L), lua_gettop(L));

    for (cur = target->users->root; cur != NULL; cur = cur->next)
    {
        irc_user *curuser = (irc_user *)cur->data;
        luaX_chanuser *udc =
            (luaX_chanuser *)lua_newuserdata(L, sizeof(luaX_chanuser));

        luaL_getmetatable(L, "luna.channel.user");
        lua_setmetatable(L, -2);

        strncpy(udc->channel.name, target->name, sizeof(udc->channel.name) - 1);
        strncpy(udc->nick,         curuser->nick, sizeof(udc->nick) - 1);

        lua_rawseti(L, array, i++);
    }

    return 1;
}


int
luaX_channel_finduser(lua_State *L)
{
    irc_channel *target = luaX_check_irc_channel_ud(L, 1);
    const char *user = luaL_checkstring(L, 2);

    luna_state *state = api_getstate(L);
    irc_user *target2 = NULL;

    if ((target2 = channel_get_user(state, target->name, user)) != NULL)
    {
        luaX_chanuser *udc =
            (luaX_chanuser *)lua_newuserdata(L, sizeof(luaX_chanuser));

        luaL_getmetatable(L, "luna.channel.user");
        lua_setmetatable(L, -2);

        strncpy(udc->channel.name, target->name, sizeof(udc->channel.name) - 1);
        strncpy(udc->nick,         target2->nick, sizeof(udc->nick) - 1);

        return 1;
    }

    lua_pushnil(L);
    return 1;
}


int
luaX_chuser_getuserinfo(lua_State *L)
{
    luaX_chanuser *ud =
        (luaX_chanuser *)luaL_checkudata(L, 1, "luna.channel.user");

    irc_user *target = NULL;
    luna_state *state = api_getstate(L);

    if ((target = channel_get_user(state, ud->channel.name, ud->nick)) != NULL)
    {
        lua_pushstring(L, target->nick);
        lua_pushstring(L, target->user);
        lua_pushstring(L, target->host);

        return 3;
    }

    return luaL_error(L, "no such user '%s' in channel '%s'",
                     ud->nick, ud->channel.name);
}


int
luaX_chuser_getstatus(lua_State *L)
{
    luaX_chanuser *ud =
        (luaX_chanuser *)luaL_checkudata(L, 1, "luna.channel.user");

    irc_user *target = NULL;
    luna_state *state = api_getstate(L);

    if ((target = channel_get_user(state, ud->channel.name, ud->nick)) != NULL)
    {
        if (target->op)
            lua_pushstring(L, "op");
        else if (target->voice)
            lua_pushstring(L, "voice");
        else
            lua_pushstring(L, "regular");

        return 1;
    }

    return luaL_error(L, "no such user '%s' in channel '%s'",
                     ud->nick, ud->channel.name);
}


int
luaX_chuser_getchannel(lua_State *L)
{
    luaX_chanuser *ud =
        (luaX_chanuser *)luaL_checkudata(L, 1, "luna.channel.user");

    luaX_channel *c = (luaX_channel *)lua_newuserdata(L, sizeof(luaX_channel));
    luaL_getmetatable(L, "luna.channel");
    lua_setmetatable(L, -2);

    strncpy(c->name, ud->channel.name, sizeof(c->name) - 1);

    return 1;
}


int
luaX_channels_getall(lua_State *L)
{
    int array;
    int i = 1;
    luna_state *state = api_getstate(L);
    list_node *cur = NULL;

    lua_newtable(L);
    array = lua_gettop(L);

    for (cur = state->channels->root; cur != NULL; cur = cur->next)
    {
        luaX_channel *u =
            (luaX_channel *)lua_newuserdata(L, sizeof(luaX_channel));
        irc_channel *chan = (irc_channel *)cur->data;

        luaL_getmetatable(L, "luna.channel");
        lua_setmetatable(L, -2);

        strncpy(u->name, chan->name, sizeof(u->name) - 1);

        lua_rawseti(L, array, i++);
    }

    return 1;
}


int
luaX_channels_find(lua_State *L)
{
    const char *chan = luaL_checkstring(L, 1);
    irc_channel *target = NULL;

    if ((target = find_channel_by_name(L, chan)) != NULL)
    {
        luaX_channel *u =
            (luaX_channel *)lua_newuserdata(L, sizeof(luaX_channel));

        luaL_getmetatable(L, "luna.channel");
        lua_setmetatable(L, -2);

        strncpy(u->name, target->name, sizeof(u->name) - 1);

        return 1;
    }

    lua_pushnil(L);
    return 1;
}


int
luaX_register_channel(lua_State *L, int regtable)
{
    int meta_channel = (luaL_newmetatable(L, "luna.channel"), lua_gettop(L));
    int meta_chuser;

    lua_pushstring(L, "__index");
    lua_pushvalue(L, meta_channel);
    lua_settable(L, meta_channel);

    luaL_openlib(L, NULL, luaX_channel_methods, 0);

    meta_chuser = (luaL_newmetatable(L, "luna.channel.user"), lua_gettop(L));

    lua_pushstring(L, "__index");
    lua_pushvalue(L, meta_chuser);
    lua_settable(L, meta_chuser);

    luaL_openlib(L, NULL, luaX_chuser_methods, 0);

    /* And register other functions inside regtable
     * luna.channels = { ... } */
    lua_pushstring(L, "channels");
    lua_newtable(L);
    luaL_register(L, NULL, luaX_channel_functions);
    lua_settable(L, regtable);


    /* Finally, register the meta table for extending */
    lua_pushstring(L, "types");
    lua_gettable(L, regtable);

    lua_pushstring(L, "channel");
    lua_pushvalue(L, meta_channel);
    lua_settable(L, -3);

    lua_pushstring(L, "channel_user");
    lua_pushvalue(L, meta_chuser);
    lua_settable(L, -3);

    return 1;
}
