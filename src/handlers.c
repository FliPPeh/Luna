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

/******************************************************************************
 *
 *  Event handling (handlers.c)
 *  ---
 *  Takes care of core events
 *
 *  Created: 25.02.2011 14:37:59
 *
 ******************************************************************************/
#include <stdlib.h>
#include <string.h>

#include "irc.h"
#include "state.h"
#include "net.h"
#include "util.h"
#include "channel.h"
#include "lua_api/lua_serializable.h"
#include "lua_api/lua_manager.h"
#include "lua_api/lua_util.h"
#include "lua_api/lua_source.h"
#include "lua_api/lua_script.h"
#include "lua_api/lua_channel.h"
#include "mm.h"

char *_strdup(const char *);

int handle_ping(luna_state *,    irc_event *);
int handle_numeric(luna_state *, irc_event *);
int handle_privmsg(luna_state *, irc_event *);
int handle_join(luna_state *,    irc_event *);
int handle_part(luna_state *,    irc_event *);
int handle_quit(luna_state *,    irc_event *);
int handle_notice(luna_state *,  irc_event *);
int handle_nick(luna_state *,    irc_event *);
int handle_mode(luna_state *,    irc_event *);
int handle_invite(luna_state *,  irc_event *);
int handle_topic(luna_state *,   irc_event *);
int handle_kick(luna_state *,    irc_event *);
int handle_unknown(luna_state *, irc_event *);
int handle_command(luna_state *, irc_event *, const char *, char *);
int handle_ctcp(luna_state *,    irc_event *, const char *, char *);
int handle_action(luna_state *,  irc_event *, const char *);

int handle_command_load(luna_state *, irc_event *, const char *);
int handle_command_reload(luna_state *, irc_event *, const char *);
int handle_command_unload(luna_state *, irc_event *, const char *);

int handle_server_supports(luna_state *, irc_event *);

int handle_mode_change(luna_state *, const char *, const char *, char **, int);
int mode_set(luna_state *, const char *, char, const char *);
int mode_unset(luna_state *, const char *, char, const char *);

char *
_strdup(const char *s)
{
    char *n = mm_malloc(strlen(s) + 1);

    return strcpy(n, s);
}


int
handle_event(luna_state *env, irc_event *ev)
{
    /* First, send the raw event */
    luaX_source src = luaX_make_source(&(ev->from));
    luaX_string msg = luaX_make_string(ev->msg);
    luaX_string_array args = luaX_make_string_array((const char **)ev->param,
                             ev->param_count);
    luaX_string cmd;

    char numeric[4] = {0};

    if (ev->type != IRCEV_NUMERIC)
    {
        cmd = luaX_make_string(irc_event_type_to_string(ev->type));
    }
    else
    {
        snprintf(numeric, sizeof(numeric), "%d", ev->numeric);
        cmd = luaX_make_string(numeric);
    }

    signal_dispatch(env, "raw", &src, &cmd, &args, &msg, NULL);

    /* Core event handlers */
    switch (ev->type)
    {
    case IRCEV_NUMERIC:
        return handle_numeric(env, ev);
    case IRCEV_PING:
        return handle_ping(env, ev);
    case IRCEV_PRIVMSG:
        return handle_privmsg(env, ev);
    case IRCEV_JOIN:
        return handle_join(env, ev);
    case IRCEV_PART:
        return handle_part(env, ev);
    case IRCEV_QUIT:
        return handle_quit(env, ev);
    case IRCEV_NOTICE:
        return handle_notice(env, ev);
    case IRCEV_NICK:
        return handle_nick(env, ev);
    case IRCEV_MODE:
        return handle_mode(env, ev);
    case IRCEV_INVITE:
        return handle_invite(env, ev);
    case IRCEV_TOPIC:
        return handle_topic(env, ev);
    case IRCEV_KICK:
        return handle_kick(env, ev);
    default:
        return handle_unknown(env, ev);
    }

    return 0;
}


int
handle_privmsg(luna_state *env, irc_event *ev)
{
    char msgcopy[LINELEN];
    char *isitme = NULL;
    char *command = NULL;
    int priv;

    /* Needs at least the target */
    if (ev->param_count < 1)
        return 1;

    priv = strchr(env->chantypes, ev->param[0][0]) == NULL;

    /* Make a copy of the message that we can modify without screwing
     * later operations */
    memset(msgcopy, 0, sizeof(msgcopy));

    strcpy(msgcopy, ev->msg);

    if ((ev->msg[0] == 0x01) && (ev->msg[strlen(ev->msg) - 1] == 0x01))
    {
        char *ctcp = NULL;
        char *args = NULL;

        ev->msg[strlen(ev->msg) - 1] = '\0';

        ctcp = strtok(ev->msg + 1, " ");
        args = strtok(NULL, "");

        return handle_ctcp(env, ev, ctcp, args);
    }

    /* Check for a "<botnick>: <command> <...>" command */
    if  (((isitme = strtok(msgcopy, ":")) != NULL) &&
            (strcasecmp(isitme, env->userinfo.nick) == 0))
    {
        if ((command = strtok(NULL, " ")) != NULL)
            handle_command(env, ev, command, strtok(NULL, ""));
    }

    luaX_string s = luaX_make_string(ev->msg);

    if (priv)
    {
        luaX_source src = luaX_make_source(&(ev->from));

        signal_dispatch(env, "private_message", &src, &s, NULL);
    }
    else
    {
        luaX_channel ch;
        luaX_chanuser cu;

        luaX_make_channel(&ch, ev->param[0]);
        luaX_make_chanuser(&cu, ev->from.nick, &ch);

        signal_dispatch(env, "public_message", &cu, &ch, &s, NULL);
    }

    return 0;
}


int
handle_ctcp(luna_state *env, irc_event *ev, const char *ctcp, char *msg)
{
    luaX_string cmd = luaX_make_string(ctcp);
    luaX_string mesg = luaX_make_string(msg);

    int priv = strchr(env->chantypes, ev->param[0][0]) == NULL;

    /* Special case for /ME commands */
    if (!strcmp(ctcp, "ACTION"))
        return handle_action(env, ev, msg);

    if (priv)
    {
        luaX_source src = luaX_make_source(&(ev->from));

        if (ev->type == IRCEV_PRIVMSG)
            signal_dispatch(env, "private_ctcp", &src, &cmd, &mesg, NULL);
        else if (ev->type == IRCEV_NOTICE)
            signal_dispatch(env, "private_ctcp_response",
                            &src, &cmd, &mesg, NULL);
    }
    else
    {
        luaX_channel ch;
        luaX_chanuser cu;

        luaX_make_channel(&ch, ev->param[0]);
        luaX_make_chanuser(&cu, ev->from.nick, &ch);

        if (ev->type == IRCEV_PRIVMSG)
            signal_dispatch(env, "public_ctcp", &cu, &ch, &cmd, &mesg, NULL);
        else if (ev->type == IRCEV_NOTICE)
            signal_dispatch(env, "public_ctcp_response",
                            &cu, &ch, &cmd, &mesg, NULL);
    }

    return 0;
}


int
handle_action(luna_state *env, irc_event *ev, const char *message)
{
    luaX_string msg = luaX_make_string(message);
    int priv = strchr(env->chantypes, ev->param[0][0]) == NULL;

    if (priv)
    {
        luaX_source src = luaX_make_source(&(ev->from));

        signal_dispatch(env, "private_action", &src, &msg, NULL);
    }
    else
    {
        luaX_channel ch;
        luaX_chanuser cu;

        luaX_make_channel(&ch, ev->param[0]);
        luaX_make_chanuser(&cu, ev->from.nick, &ch);

        signal_dispatch(env, "public_action", &cu, &ch, &msg, NULL);
    }

    return 0;
}


int
handle_command(luna_state *env, irc_event *ev, const char *cmd, char *rest)
{
    luaX_string scmd = luaX_make_string(cmd);
    luaX_string srest = luaX_make_string(rest);

    int priv = strchr(env->chantypes, ev->param[0][0]) == NULL;

    luna_user *user = user_match(env, &(ev->from));

    if ((user) && (strchr(user->flags, 'o')))
    {
        if (!strcasecmp(cmd, "load"))
        {
            char *script = strtok(rest, " ");
            handle_command_load(env, ev, script);
        }
        else if (!strcasecmp(cmd, "reload"))
        {
            char *script = strtok(rest, " ");
            handle_command_reload(env, ev, script);
        }
        else if (!strcasecmp(cmd, "unload"))
        {
            char *script = strtok(rest, " ");
            handle_command_unload(env, ev, script);
        }
        else if (!strcasecmp(cmd, "reloadusers"))
        {
            if (!users_reload(env, "users.txt"))
                net_sendfln(env, "PRIVMSG %s :%s: Reloaded %d users!",
                            ev->param[0], ev->from.nick, env->users->length);
            else
                net_sendfln(env, "PRIVMSG %s :%s: Failed to load users :(",
                            ev->param[0], ev->from.nick);
        }
    }

    if (priv)
    {
        luaX_source src = luaX_make_source(&(ev->from));

        signal_dispatch(env, "private_command", &src, &scmd, &srest, NULL);
    }
    else
    {
        luaX_chanuser cu;
        luaX_channel ch;

        luaX_make_channel(&ch, ev->param[0]);
        luaX_make_chanuser(&cu, ev->from.nick, &ch);

        signal_dispatch(env, "public_command", &cu, &ch, &scmd, &srest, NULL);
    }

    return 0;
}


int
handle_ping(luna_state *env, irc_event *ev)
{
    const char *pingstr;

    if ((ev->msg == NULL) && (ev->param_count < 1))
        return 1;

    pingstr = (ev->param_count > 0) ? ev->param[0] : ev->msg;

    net_sendfln(env, "PONG :%s", pingstr);
    signal_dispatch(env, "ping", NULL);

    return 0;
}


int
handle_numeric(luna_state *env, irc_event *ev)
{
    int i = 0;

    irc_user *target = NULL;

    switch (ev->numeric)
    {
    case 5: /* ISUPPORT */
        handle_server_supports(env, ev);

        break;

    case 315: /* WHO End */

        /* param 0: me
         * param 1: channel
         */
        if (ev->param_count < 2)
            return 1;

        target = channel_get_user(env, ev->param[1], ev->param[0]);

        if (target)
        {
            luaX_channel ch;
            luaX_chanuser cu;

            luaX_make_channel(&ch, ev->param[1]);
            luaX_make_chanuser(&cu, ev->param[0], &ch);

            signal_dispatch(env, "channel_join", &cu, &ch, NULL);
        }

        break;

    case 376:
        /* Dispatch connect event */
        signal_dispatch(env, "connect", NULL);

        break;

    case 352: /* REPL_WHO */

        /* param 0: me
         * param 1: channel
         * param 2: user
         * param 3: host
         * param 4: server
         * param 5: nick
         * param 6: modestr
         * msg: <hops> <realname> */
        if (ev->param_count < 7)
            return 1;

        channel_add_user(env,
                         ev->param[1],
                         ev->param[5],
                         ev->param[2],
                         ev->param[3]);

        target = channel_get_user(env, ev->param[1], ev->param[5]);

        if (target)
        {
            /*
             * TODO: Dynamic mode size in case IRC networks start
             *       allowing Unicode flags?
             */
            int max = sizeof(env->userprefix) / sizeof(env->userprefix[0]);
            char *mode = ev->param[6];

            while (*mode++)
            {
                for (i = 0; ((i < max) && (env->userprefix[i].prefix != 0));
                        ++i)
                {
                    if (env->userprefix[i].prefix == *mode)
                    {
                        int len = strlen(target->modes);
                        target->modes[len] = env->userprefix[i].mode;
                        target->modes[len + 1] = 0;
                    }
                }
            }
        }

        break;

    case 332: /* TOPIC */

        if (ev->param_count < 2)
            return 1;

        channel_set_topic(env, ev->param[1], ev->msg);

        break;

    case 333: /* TOPIC META */

        if (ev->param_count < 4)
            return 1;

        channel_set_topic_meta(env,
                               ev->param[1],
                               ev->param[2],
                               atoi(ev->param[3]));

        break;

    case 324: /* REPL_MODE */

        /* <server> 324 <me> <channel> <flags> [param[,param[,...]]] */
        if (ev->param_count < 4)
            return 1;

        handle_mode_change(env, ev->param[1], ev->param[2], ev->param, 3);

        break;

    case 329: /* REPL_MODE2 */

        /* :aperture.esper.net 329 Luna^ #lulz2 1298798377 */
        if (ev->param_count < 3)
            return 1;

        channel_set_creation_time(env, ev->param[1], atoi(ev->param[2]));

        break;
    }

    return 0;
}


int
handle_join(luna_state *env, irc_event *ev)
{
    const char *c;

    if ((ev->msg == NULL) && ev->param_count < 1)
        return 1;

    c = ev->msg ? ev->msg : ev->param[0];

    luaX_channel ch;
    luaX_chanuser cu;

    luaX_make_channel(&ch, c);
    luaX_make_chanuser(&cu, ev->from.nick, &ch);

    /* Is it me? */
    /* TODO: Instead of now, call join hook for self after /WHO END numeric */
    if (!strcasecmp(ev->from.nick, env->userinfo.nick))
    {
        /* Yes! Add channel to list */
        channel_add(env, c);

        /* Query userlist */
        net_sendfln(env, "WHO %s", c);
        net_sendfln(env, "MODE %s", c);
    }
    else
    {
        /* Nah, add user to channel */
        channel_add_user(env, c, ev->from.nick, ev->from.user, ev->from.host);
        signal_dispatch(env, "channel_join", &cu, &ch, NULL);
    }

    return 0;
}


int
handle_part(luna_state *env, irc_event *ev)
{
    luaX_channel ch;
    luaX_chanuser cu;

    if (ev->param_count < 1)
        return 1;

    luaX_make_channel(&ch, ev->param[0]);
    luaX_make_chanuser(&cu, ev->from.nick, &ch);

    if (ev->msg)
    {
        luaX_string reason = luaX_make_string(ev->msg);
        signal_dispatch(env, "channel_part", &cu, &ch, &reason, NULL);
    }
    else
    {
        signal_dispatch(env, "channel_part", &cu, &ch, NULL);
    }

    /* Is it me? */
    if (!strcasecmp(ev->from.nick, env->userinfo.nick))
    {
        /* Yes! Remove channel from list */
        channel_remove(env, ev->param[0]);
    }
    else
    {
        /* Nah, remove user from channel */
        channel_remove_user(env, ev->param[0], ev->from.nick);
    }

    return 0;
}


int
handle_quit(luna_state *env, irc_event *ev)
{
    /* Remove user from all channels */
    list_node *cur;
    luaX_string reason = luaX_make_string(ev->msg);
    luaX_source src = luaX_make_source(&(ev->from));

    for (cur = env->channels->root; cur != NULL; cur = cur->next)
    {
        irc_channel *channel = (irc_channel *)(cur->data);
        channel_remove_user(env, channel->name, ev->from.nick);
    }

    signal_dispatch(env, "user_quit", &src, &reason, NULL);

    return 0;
}


int
handle_notice(luna_state *env, irc_event *ev)
{
    luaX_string target;

    if (ev->param_count < 1)
        return 1;

    target = luaX_make_string(ev->param[0]);
    luaX_string msg = luaX_make_string(ev->msg);
    luaX_source src = luaX_make_source(&(ev->from));

    /* CTCP response */
    if ((ev->msg[0] == 0x01) && (ev->msg[strlen(ev->msg) - 1] == 0x01))
    {
        char *ctcp;
        char *args;

        ev->msg[strlen(ev->msg) - 1] = '\0';

        ctcp = strtok(ev->msg + 1, " ");
        args = strtok(NULL, "");

        return handle_ctcp(env, ev, ctcp, args);
    }
    else
    {
        signal_dispatch(env, "notice", &src, &target, &msg, NULL);
    }

    return 0;
}


int
handle_nick(luna_state *env, irc_event *ev)
{
    const char *newnick;

    if ((ev->msg == NULL) && (ev->param_count < 1))
        return 1;

    newnick = ev->msg ? ev->msg : ev->param[0];
    luaX_string nick = luaX_make_string(newnick);
    luaX_source src = luaX_make_source(&(ev->from));

    /* Is it me? */
    if (!strcasecmp(ev->from.nick, env->userinfo.nick))
    {
        /* Rename myself internally */
        memset(env->userinfo.nick, 0, sizeof(env->userinfo.nick));
        strncpy(env->userinfo.nick, newnick, sizeof(env->userinfo.nick) - 1);
    }

    /* Rename user in all channels */
    user_rename(env, ev->from.nick, newnick);

    signal_dispatch(env, "nick_change", &src, &nick, NULL);

    return 0;
}


int
handle_mode(luna_state *env, irc_event *ev)
{
    /* <sender> MODE <channel> <flags> [param[,param[,...]]] */

    if (ev->param_count < 2)
        return 1;

    /* If not me... */
    if (strcasecmp(ev->param[0], env->userinfo.nick))
        handle_mode_change(env, ev->param[0], ev->param[1], ev->param, 2);

    return 0;
}


int
handle_invite(luna_state *env, irc_event *ev)
{
    luaX_string channel = luaX_make_string(ev->msg);
    luaX_source src = luaX_make_source(&(ev->from));

    signal_dispatch(env, "invite", &src, &channel, NULL);

    return 0;
}


int
handle_topic(luna_state *env, irc_event *ev)
{
    char hoststring[128];

    if (ev->param_count < 1)
        return 1;

    luaX_channel ch;
    luaX_chanuser cu;
    luaX_string reason = luaX_make_string(ev->msg);

    luaX_make_channel(&ch, ev->param[0]);
    luaX_make_chanuser(&cu, ev->from.nick, &ch);

    memset(hoststring, 0, sizeof(hoststring));
    snprintf(hoststring, sizeof(hoststring), "%s!%s@%s",
             ev->from.nick, ev->from.user, ev->from.host);

    channel_set_topic(env, ev->param[0], ev->msg);
    channel_set_topic_meta(env, ev->param[0], hoststring, time(NULL));

    signal_dispatch(env, "topic_change", &cu, &ch, &reason, NULL);

    return 0;
}


int
handle_kick(luna_state *env, irc_event *ev)
{
    luaX_channel ch;
    luaX_chanuser kicker;
    luaX_chanuser kicked;
    luaX_string reason;

    if (ev->param_count < 2)
        return 1;

    reason = luaX_make_string(ev->msg);

    luaX_make_channel(&ch, ev->param[0]);
    luaX_make_chanuser(&kicker, ev->from.nick, &ch);
    luaX_make_chanuser(&kicked, ev->param[1],  &ch);

    signal_dispatch(env, "user_kicked", &kicker, &ch, &kicked, &reason, NULL);

    /* Remove user from all channels (ev->param[1]) */
    if (!strcasecmp(ev->param[1], env->userinfo.nick))
    {
        /* It's me! Geez! */
        channel_remove(env, ev->param[0]);
    }
    else
    {
        /* Remove user from channel! */
        channel_remove_user(env, ev->param[0], ev->param[1]);
    }

    return 0;
}


int
handle_unknown(luna_state *env, irc_event *ev)
{
    return 0;
}


int
handle_command_load(luna_state *env, irc_event *ev, const char *name)
{
    void *already_loaded = list_find(env->scripts,
                                     (const void *)name, &script_cmp);

    if (already_loaded)
    {
        net_sendfln(env, "PRIVMSG %s :%s: Script already loaded!",
                    ev->param[0], ev->from.nick);
    }
    else
    {
        if (!script_load(env, name))
        {
            void *loaded = list_find(env->scripts,
                                     (const void *)name, &script_cmp);

            luna_script *script = (luna_script *)loaded;
            luaX_script scrpt = luaX_make_script(script);

            net_sendfln(env, "PRIVMSG %s :%s: Loaded script '%s v%s'",
                        ev->param[0], ev->from.nick,
                        script->name, script->version);

            signal_dispatch(env, "script_load", &scrpt, NULL);
        }
        else
        {
            net_sendfln(env, "PRIVMSG %s :%s: Failed to load script!",
                        ev->param[0], ev->from.nick);
        }
    }

    return 0;
}


int
handle_command_reload(luna_state *env, irc_event *ev, const char *name)
{
    void *loaded = list_find(env->scripts, (const void *)name, &script_cmp);

    if (!loaded)
    {
        net_sendfln(env, "PRIVMSG %s :%s: Script not loaded! Loading...",
                    ev->param[0], ev->from.nick);

        handle_command_load(env, ev, name);
    }
    else
    {
        if ((!script_unload(env, name)) && (!script_load(env, name)))
        {
            void *n = list_find(env->scripts, (void *)name, &script_cmp);
            luna_script *script = (luna_script *)n;

            net_sendfln(env, "PRIVMSG %s :%s: Reloaded script '%s v%s'",
                        ev->param[0], ev->from.nick,
                        script->name, script->version);
        }
        else
        {
            net_sendfln(env, "PRIVMSG %s :%s: Failed to reload script!",
                        ev->param[0], ev->from.nick);
        }
    }

    return 0;
}


int
handle_command_unload(luna_state *env, irc_event *ev, const char *name)
{
    void *loaded = list_find(env->scripts, (void *)name, &script_cmp);
    luna_script temp = *((luna_script *)loaded);

    if (!loaded)
        net_sendfln(env, "PRIVMSG %s :%s: Script not loaded!",
                    ev->param[0], ev->from.nick);
    else if (!script_unload(env, name))
    {
        luaX_script script = luaX_make_script(&temp);

        net_sendfln(env, "PRIVMSG %s :%s: Unloaded script!",
                    ev->param[0], ev->from.nick);

        signal_dispatch(env, "script_unload", &script, NULL);
    }
    else
    {
        net_sendfln(env, "PRIVMSG %s :%s: Failed to unload script!",
                    ev->param[0], ev->from.nick);
    }

    return 0;
}


int
handle_server_supports(luna_state *env, irc_event *ev)
{
    int i;

    for (i = 1; i < ev->param_count; ++i)
    {
        char *key = strtok(ev->param[i], "=");
        char *val = strtok(NULL, "");

        if (!strcasecmp(key, "CHANMODES"))
        {
            char *tok = strtok(val, ",");
            strncpy(env->chanmodes.param_address, tok,
                    sizeof(env->chanmodes.param_address) - 1);

            tok = strtok(NULL, ",");
            strncpy(env->chanmodes.param_always, tok,
                    sizeof(env->chanmodes.param_always) - 1);

            tok = strtok(NULL, ",");
            strncpy(env->chanmodes.param_whenset, tok,
                    sizeof(env->chanmodes.param_whenset) - 1);

            tok = strtok(NULL, ",");
            strncpy(env->chanmodes.param_never, tok,
                    sizeof(env->chanmodes.param_never) - 1);
        }
        else if (!strcasecmp(key, "PREFIX"))
        {
            /*PREFIX=(ov)@+ */
            int k;
            int max = sizeof(env->userprefix) / sizeof(env->userprefix[0]);

            for (k = 0; (k < strlen(val) / 2 - 1) && (k < max); ++k)
            {
                env->userprefix[k].mode = *(val + k + 1);
                env->userprefix[k].prefix = *(val + k + strlen(val) / 2 + 1);

                channel_modes *m = &(env->chanmodes);

                if (strlen(m->param_nick) < sizeof(m->param_nick) - 1)
                {
                    int ind = strlen(m->param_nick);

                    m->param_nick[ind] = *(val + k + 1);
                    m->param_nick[ind + 1] = 0;
                }
            }
        }
        else if (!strcasecmp(key, "CHANTYPES"))
        {
            env->chantypes = _strdup(val);
        }
    }

    return 0;
}


int
handle_mode_change(luna_state *state, const char *channel,
                   const char *flags, char **args, int argind)
{
    int action = 0; /* 0 = set, 1 = unset */
    int i = argind;

    irc_channel *target = (irc_channel *)list_find(
                              state->channels, (const void *)channel, &channel_cmp);

    if (!target)
    {
        logger_log(state->logger, LOGLEV_WARNING, "Unknown channel `%s'",
                   channel);
        return 1;
    }

    while (*flags)
    {
        int max = sizeof(target->flags) / sizeof(target->flags[0]);

        switch (*flags)
        {
        case '+':
            action = 0;
            flags++;
            break;
        case '-':
            action = 1;
            flags++;
            break;
        }

        int flag = *flags - 'A';

        if (flag >= max)
        {
            logger_log(state->logger, LOGLEV_WARNING, "Mode `%c' out of range",
                       *flags);
            return 0;
        }

        /*
         * Do all modes that need an argument
         */
        if  (strchr(state->chanmodes.param_address, *flags) ||
                strchr(state->chanmodes.param_always, *flags) ||
                (strchr(state->chanmodes.param_whenset, *flags) && !action))
        {
            /* Set/Unset flag "*flags" with argument "args[i]" */
            char *arg = args[i++];

            if (!action)
            {
                if (strchr(state->chanmodes.param_address, *flags))
                {
                    /* Add to (or create) list */
                    if (!target->flags[flag].set)
                    {
                        /* Flag not set, set it and create the list */
                        target->flags[flag].set = 1;
                        target->flags[flag].type = FLAG_LIST;
                        list_init(&target->flags[flag].list);
                    }

                    list_push_back(target->flags[flag].list, _strdup(arg));
                }
                else
                {
                    target->flags[flag].set = 1;
                    target->flags[flag].type = FLAG_STRING;
                    target->flags[flag].string = _strdup(arg);
                }
            }
            else
            {
                /* Remove from list, possibly remove list, too */
                if (target->flags[flag].set)
                {
                    if (strchr(state->chanmodes.param_address, *flags))
                    {
                        char *entry = (char *)list_find(
                                          target->flags[flag].list,
                                          arg,
                                          (list_find_fn)&strcasecmp);

                        if (entry)
                        {
                            list_delete(target->flags[flag].list, entry,
                                        &mm_free);
                        }

                        if (target->flags[flag].list->length == 0)
                        {
                            list_destroy(target->flags[flag].list, &mm_free);
                            target->flags[flag].type = FLAG_NONE;
                            target->flags[flag].set = 0;
                        }
                    }
                    else
                    {
                        target->flags[flag].set = 0;
                        mm_free(target->flags[flag].string);
                        target->flags[flag].string = NULL;
                        target->flags[flag].type = FLAG_NONE;
                    }
                }
            }
        }
        else if (strchr(state->chanmodes.param_nick, *flags))
        {
            const char *arg = args[i++];

            irc_user *user = channel_get_user(state, channel, arg);

            if (user)
            {
                if (!action)
                {
                    if ((strlen(user->modes) < sizeof(user->modes))
                            && (!strchr(user->modes, *flags)))
                    {
                        int len = strlen(user->modes);
                        user->modes[len] = *flags;
                        user->modes[len + 1] = 0;
                    }
                }
                else
                {
                    const char *oldmodes = user->modes;
                    memset(user->modes, 0, sizeof(user->modes));

                    while (*oldmodes)
                    {
                        if (*oldmodes != *flags)
                        {
                            int len = strlen(user->modes);
                            user->modes[len] = *oldmodes;
                            user->modes[len + 1] = 0;
                        }

                        oldmodes++;
                    }
                }
            }
            else
            {
                logger_log(state->logger, LOGLEV_WARNING,
                           "Tried to alter unknown user `%s'", arg);
            }
        }
        else
        {
            /* Unknown flags and whenset-parameters */
            /* Unknown flags are treated as if they require no parameter */

            /* Set/Unset flag "*flags" for channel */
            if (!action)
            {
                target->flags[flag].set = 1;
            }
            else
            {
                target->flags[flag].set = 0;

                if (target->flags[flag].type == FLAG_STRING)
                    mm_free(target->flags[flag].string);

                target->flags[flag].type = FLAG_NONE;
            }
        }

        flags++;
    }

    return 0;
}

