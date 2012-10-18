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
 *  User management (user.c)
 *  ---
 *  Manage the state->users
 *
 *  Created: 26.02.2011 17:36:49
 *
 ******************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "user.h"
#include "linked_list.h"


int
users_load(luna_state *state, const char *file)
{
    FILE *userfile = fopen(file, "r");
    if (userfile)
    {
        char buffer[256];

        while (!feof(userfile))
        {
            char *strip = NULL;
            char *id    = NULL;
            char *mask  = NULL;
            char *flags = NULL;
            char *level = NULL;

            memset(buffer, 0, sizeof(buffer));

            if (!fgets(buffer, sizeof(buffer), userfile))
                break;

            strip = strtok(buffer, "\n");

            id    = strtok(strip, ":");
            mask  = strtok(NULL, ":");
            flags = strtok(NULL, ":");
            level = strtok(NULL, "");

            if (id && mask && flags)
            {
                luna_user *user = malloc(sizeof(*user));

                if (user)
                {
                    memset(user, 0, sizeof(*user));

                    strncpy(user->id, id, sizeof(user->id) - 1);
                    strncpy(user->hostmask, mask, sizeof(user->hostmask) - 1);
                    strncpy(user->flags, flags, sizeof(user->flags) - 1);
                    strncpy(user->level, level, sizeof(user->level) - 1);

                    list_push_back(state->users, user);
                }
            }
        }

        fclose(userfile);
        return 0;
    }

    return 1;
}


int
users_unload(luna_state *state)
{
    list_node *cur = NULL;

    for (cur = state->users->root; cur != NULL; cur = state->users->root)
        list_delete(state->users, cur->data, &free);

    return 0;
}


int
users_reload(luna_state *state, const char *file)
{
    users_unload(state);

    return users_load(state, file);
}


int
users_write(luna_state *state, const char *file)
{
    FILE *f = fopen(file, "w");

    if (f)
    {
        list_node *cur = NULL;

        for (cur = state->users->root; cur != NULL; cur = cur->next)
        {
            luna_user *u = (luna_user *)(cur->data);

            fprintf(f, "%s:%s:%s:%s\n", u->id, u->hostmask, u->flags, u->level);
        }

        fclose(f);

        return 0;
    }

    return 1;
}


int
users_add(luna_state *state, const char *id, const char *hostmask,
          const char *flags, const char *level)
{
    luna_user *user = (luna_user *)list_find(state->users, (void *)hostmask,
                       &luna_user_host_cmp);
    if (user)
        return 1;

    user = malloc(sizeof(*user));

    if (user)
    {
        memset(user, 0, sizeof(*user));

        strncpy(user->id, id, sizeof(user->id) - 1);
        strncpy(user->hostmask, hostmask, sizeof(user->hostmask) - 1);
        strncpy(user->flags, flags, sizeof(user->flags) - 1);
        strncpy(user->level, level, sizeof(user->level) - 1);

        list_push_back(state->users, user);
    }

    return 0;
}


int
users_remove(luna_state *state, const char *hostmask)
{
    void *user = list_find(state->users, (void *)hostmask, &luna_user_host_cmp);

    if (user)
    {
        list_delete(state->users, user, &free);
        return 0;
    }

    return 1;
}


int
luna_user_cmp(const void *data, const void *list_data)
{
    char host[256];

    luna_user *user = (luna_user *)list_data;
    irc_sender *key = (irc_sender *)data;

    memset(host, 0, sizeof(host));
    snprintf(host, sizeof(host), "%s!%s@%s", key->nick, key->user, key->host);

    return !(strwcasecmp(host, user->hostmask));
}


int
luna_user_host_cmp(const void *data, const void *list_data)
{
    char *key = (char *)data;
    luna_user *user = (luna_user *)list_data;

    return strcasecmp(key, user->hostmask);
}


luna_user *
user_match(luna_state *state, irc_sender *who)
{
    void *user = list_find(state->users, (void *)who, &luna_user_cmp);

    return (luna_user *)user;
}


int
user_match_level(luna_state *state, irc_sender *s, const char *level)
{
    void *user = list_find(state->users, (void *)s, &luna_user_cmp);

    if (user)
    {
        luna_user *user_real = (luna_user *)user;

        return strcasecmp(user_real->level, level);
    }

    return 1;
}


char *
user_get_level(luna_state *state, irc_sender *s)
{
    void *user = list_find(state->users, (void *)s, &luna_user_cmp);

    if (user)
    {
        luna_user *user_real = (luna_user *)user;

        return user_real->level;
    }

    return NULL;
}


int
strwcmp(const char *str, const char *pat)
{
    switch (*pat)
    {
        case 0:
            return !(*str);

        case '*':
            return (strwcmp(str, pat + 1) || (*str && strwcmp(str + 1, pat)));

        case '?':
            return (*str && strwcmp(str + 1, pat + 1));

        default:
            return ((*str == *pat) && (strwcmp(str + 1, pat + 1)));
    }

    return 0;
}


int
strwcasecmp(const char *str, const char *pat)
{
    switch (*pat)
    {
        case 0:
            return !(*str);

        case '*':
            return ((strwcasecmp(str, pat + 1) ||
                    (*str && strwcasecmp(str + 1, pat))));

        case '?':
            return (*str && strwcasecmp(str + 1, pat + 1));

        default:
            return ((tolower(*str) == tolower(*pat)) &&
                    (strwcasecmp(str + 1, pat + 1)));
    }

    return 0;
}
