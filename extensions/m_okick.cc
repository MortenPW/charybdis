/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_okick.c: Kicks a user from a channel with much prejudice.
 *
 *  Copyright (C) 2002 by the past and present ircd coders, and others.
 *  Copyright (C) 2004 ircd-ratbox Development Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 */

using namespace ircd;

static const char okick_desc[] = "Allow admins to forcibly kick users from channels with the OKICK command";

static void mo_okick(struct MsgBuf *msgbuf_p, client::client &client, client::client &source, int parc, const char *parv[]);

struct Message okick_msgtab = {
	"OKICK", 0, 0, 0, 0,
	{mg_unreg, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {mo_okick, 4}}
};

mapi_clist_av1 okick_clist[] = { &okick_msgtab, NULL };

DECLARE_MODULE_AV2(okick, NULL, NULL, okick_clist, NULL, NULL, NULL, NULL, okick_desc);

/*
** m_okick
**      parv[1] = channel
**      parv[2] = client to kick
**      parv[3] = kick comment
*/
static void
mo_okick(struct MsgBuf *msgbuf_p, client::client &client, client::client &source, int parc, const char *parv[])
{
	client::client *who;
	client::client *target_p;
	chan::chan *chptr;
	chan::membership *msptr;
	int chasing = 0;
	char *comment;
	char *name;
	char *p = NULL;
	char *user;
	static char buf[BUFSIZE];

	if(*parv[2] == '\0')
	{
		sendto_one(&source, form_str(ERR_NEEDMOREPARAMS), me.name, source.name, "KICK");
		return;
	}

	if(my(source) && !is_flood_done(source))
		flood_endgrace(&source);

	comment = (EmptyString(LOCAL_COPY(parv[3]))) ? LOCAL_COPY(parv[2]) : LOCAL_COPY(parv[3]);
	if(strlen(comment) > (size_t) TOPICLEN)
		comment[TOPICLEN] = '\0';

	*buf = '\0';
	if((p = (char *)strchr(parv[1], ',')))
		*p = '\0';

	name = LOCAL_COPY(parv[1]);

	chptr = chan::get(name, std::nothrow);
	if(!chptr)
	{
		sendto_one_numeric(&source, ERR_NOSUCHCHANNEL, form_str(ERR_NOSUCHCHANNEL), name);
		return;
	}


	if((p = (char *)strchr(parv[2], ',')))
		*p = '\0';
	user = LOCAL_COPY(parv[2]);	// strtoken(&p2, parv[2], ",");
	if(!(who = find_chasing(&source, user, &chasing)))
	{
		return;
	}

	if((target_p = find_client(user)) == NULL)
	{
		sendto_one(&source, form_str(ERR_NOSUCHNICK), user);
		return;
	}

	if((msptr = get(chptr->members, *target_p, std::nothrow)) == NULL)
	{
		sendto_one(&source, form_str(ERR_USERNOTINCHANNEL), parv[1], parv[2]);
		return;
	}

	sendto_wallops_flags(umode::WALLOP, &me,
			     "OKICK called for %s %s by %s!%s@%s",
			     chptr->name.c_str(), target_p->name,
			     source.name, source.username, source.host);
	ilog(L_MAIN, "OKICK called for %s %s by %s",
	     chptr->name.c_str(), target_p->name,
	     get_oper_name(&source));
	/* only sends stuff for #channels remotely */
	sendto_server(NULL, chptr, NOCAPS, NOCAPS,
			":%s WALLOPS :OKICK called for %s %s by %s!%s@%s",
			me.name, chptr->name.c_str(), target_p->name,
			source.name, source.username, source.host);

	sendto_channel_local(chan::ALL_MEMBERS, chptr, ":%s KICK %s %s :%s",
			     me.name, chptr->name.c_str(), who->name, comment);
	sendto_server(&me, chptr, CAP_TS6, NOCAPS,
		      ":%s KICK %s %s :%s", me.id, chptr->name.c_str(), who->id, comment);

	del(*chptr, *target_p);
}