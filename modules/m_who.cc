/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_who.c: Shows who is on a channel.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2005 ircd-ratbox development team
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

#define FIELD_CHANNEL    0x0001
#define FIELD_HOP        0x0002
#define FIELD_FLAGS      0x0004
#define FIELD_HOST       0x0008
#define FIELD_IP         0x0010
#define FIELD_IDLE       0x0020
#define FIELD_NICK       0x0040
#define FIELD_INFO       0x0080
#define FIELD_SERVER     0x0100
#define FIELD_QUERYTYPE  0x0200 /* cookie for client */
#define FIELD_USER       0x0400
#define FIELD_ACCOUNT    0x0800
#define FIELD_OPLEVEL    0x1000 /* meaningless and stupid, but whatever */

static const char who_desc[] =
	"Provides the WHO command to display information for users on a channel";

struct who_format
{
	int fields;
	const char *querytype;
};

static void m_who(struct MsgBuf *, client::client &, client::client &, int, const char **);

static void do_who_on_channel(client::client &source, chan::chan *chptr,
			      int server_oper, int member,
			      struct who_format *fmt);
static void who_global(client::client &source, const char *mask, int server_oper, int operspy, struct who_format *fmt);
static void do_who(client::client &source,
		   client::client *target_p, chan::chan *, chan::membership *msptr,
		   struct who_format *fmt);

struct Message who_msgtab = {
	"WHO", 0, 0, 0, 0,
	{mg_unreg, {m_who, 2}, mg_ignore, mg_ignore, mg_ignore, {m_who, 2}}
};

static int
_modinit(void)
{
	supported::add("WHOX");
	return 0;
}

static void
_moddeinit(void)
{
	supported::del("WHOX");
}

mapi_clist_av1 who_clist[] = { &who_msgtab, NULL };
DECLARE_MODULE_AV2(who, _modinit, _moddeinit, who_clist, NULL, NULL, NULL, NULL, who_desc);

/*
** m_who
**      parv[1] = nickname mask list
**      parv[2] = additional selection flag and format options
*/
static void
m_who(struct MsgBuf *msgbuf_p, client::client &client, client::client &source, int parc, const char *parv[])
{
	static time_t last_used = 0;
	client::client *target_p;
	chan::membership *msptr;
	char *mask;
	rb_dlink_node *lp;
	chan::chan *chptr = NULL;
	int server_oper = parc > 2 ? (*parv[2] == 'o') : 0;	/* Show OPERS only */
	int member;
	int operspy = 0;
	struct who_format fmt;
	const char *s;
	char maskcopy[512];

	fmt.fields = 0;
	fmt.querytype = NULL;
	if (parc > 2 && (s = strchr(parv[2], '%')) != NULL)
	{
		s++;
		for (; *s != '\0'; s++)
		{
			switch (*s)
			{
				case 'c': fmt.fields |= FIELD_CHANNEL; break;
				case 'd': fmt.fields |= FIELD_HOP; break;
				case 'f': fmt.fields |= FIELD_FLAGS; break;
				case 'h': fmt.fields |= FIELD_HOST; break;
				case 'i': fmt.fields |= FIELD_IP; break;
				case 'l': fmt.fields |= FIELD_IDLE; break;
				case 'n': fmt.fields |= FIELD_NICK; break;
				case 'r': fmt.fields |= FIELD_INFO; break;
				case 's': fmt.fields |= FIELD_SERVER; break;
				case 't': fmt.fields |= FIELD_QUERYTYPE; break;
				case 'u': fmt.fields |= FIELD_USER; break;
				case 'a': fmt.fields |= FIELD_ACCOUNT; break;
				case 'o': fmt.fields |= FIELD_OPLEVEL; break;
				case ',':
					  s++;
					  fmt.querytype = s;
					  s += strlen(s);
					  s--;
					  break;
			}
		}
		if (EmptyString(fmt.querytype) || strlen(fmt.querytype) > 3)
			fmt.querytype = "0";
	}

	rb_strlcpy(maskcopy, parv[1], sizeof maskcopy);
	mask = maskcopy;

	collapse(mask);

	/* '/who *' */
	if((*(mask + 1) == '\0') && (*mask == '*'))
	{
		if(source.user == NULL)
			return;

		if (!chans(user(source)).empty())
		{
			auto *const chan(begin(chans(user(source)))->first);
			do_who_on_channel(source, chan, server_oper, true, &fmt);
		}

		sendto_one(&source, form_str(RPL_ENDOFWHO),
			   me.name, source.name, "*");
		return;
	}

	if(IsOperSpy(&source) && *mask == '!')
	{
		mask++;
		operspy = 1;

		if(EmptyString(mask))
		{
			sendto_one(&source, form_str(RPL_ENDOFWHO),
					me.name, source.name, parv[1]);
			return;
		}
	}

	/* '/who #some_channel' */
	if(chan::has_prefix(mask))
	{
		/* List all users on a given channel */
		chptr = chan::get(parv[1] + operspy, std::nothrow);

		if(chptr != NULL)
		{
			if (!is(source, umode::OPER) && !ratelimit_client_who(&source, size(chptr->members)/50))
			{
				sendto_one(&source, form_str(RPL_LOAD2HI),
						me.name, source.name, "WHO");
				sendto_one(&source, form_str(RPL_ENDOFWHO),
					   me.name, source.name, "*");
				return;
			}

//			if(operspy)
//				report_operspy(&source, "WHO", chptr->name.c_str());

			if(is_member(chptr, &source) || operspy)
				do_who_on_channel(source, chptr, server_oper, true, &fmt);
			else if(!is_secret(chptr))
				do_who_on_channel(source, chptr, server_oper, false, &fmt);
		}

		sendto_one(&source, form_str(RPL_ENDOFWHO),
			   me.name, source.name, parv[1] + operspy);
		return;
	}

	/* '/who nick' */

	if(((target_p = client::find_named_person(mask)) != NULL) &&
	   (!server_oper || is(*target_p, umode::OPER)))
	{
		int isinvis = 0;

		isinvis = is(*target_p, umode::INVISIBLE);
		for(const auto &pit : chans(user(*target_p)))
		{
			chptr = pit.first;
			msptr = get(chptr->members, source, std::nothrow);

			if(isinvis && !member)
			{
				msptr = nullptr;
				continue;
			}

			if(member || (!isinvis && is_public(chptr)))
				break;

			msptr = nullptr;
		}

		/* if we stopped midlist, msptr is the membership for
		 * target_p of chptr
		 */
		do_who(source, target_p, chptr, msptr, &fmt);

		sendto_one(&source, form_str(RPL_ENDOFWHO),
			   me.name, source.name, mask);
		return;
	}

	if(!is_flood_done(source))
		flood_endgrace(&source);

	/* it has to be a global who at this point, limit it */
	if(!is(source, umode::OPER))
	{
		if((last_used + ConfigFileEntry.pace_wait) > rb_current_time() || !ratelimit_client(&source, 1))
		{
			sendto_one(&source, form_str(RPL_LOAD2HI),
					me.name, source.name, "WHO");
			sendto_one(&source, form_str(RPL_ENDOFWHO),
				   me.name, source.name, "*");
			return;
		}
		else
			last_used = rb_current_time();
	}

	/* Note: operspy_dont_care_user_info does not apply to
	 * who on channels */
	if(IsOperSpy(&source) && ConfigFileEntry.operspy_dont_care_user_info)
		operspy = 1;

	/* '/who 0' for a global list.  this forces clients to actually
	 * request a full list.  I presume its because of too many typos
	 * with "/who" ;) --fl
	 */
	if((*(mask + 1) == '\0') && (*mask == '0'))
		who_global(source, NULL, server_oper, 0, &fmt);
	else
		who_global(source, mask, server_oper, operspy, &fmt);

	sendto_one(&source, form_str(RPL_ENDOFWHO),
		   me.name, source.name, mask);
}

/* who_common_channel
 * inputs	- pointer to client requesting who
 * 		- pointer to channel member chain.
 *		- char * mask to match
 *		- int if oper on a server or not
 *		- pointer to int maxmatches
 *		- format options
 * output	- NONE
 * side effects - lists matching invisible clients on specified channel,
 * 		  marks matched clients.
 */
static void
who_common_channel(client::client &source, chan::chan *chptr,
		   const char *mask, int server_oper, int *maxmatches,
		   struct who_format *fmt)
{
	for(const auto &pit : chptr->members.global)
	{
		const auto &target(pit.first);
		const auto &member(pit.second);

		if(!is(*target, umode::INVISIBLE) || is_marked(*target))
			continue;

		if(server_oper && !is(*target, umode::OPER))
			continue;

		set_mark(*target);

		if(*maxmatches > 0)
		{
			if((mask == NULL) ||
					match(mask, target->name) || match(mask, target->username) ||
					match(mask, target->host) || match(mask, target->servptr->name) ||
					(is(source, umode::OPER) && match(mask, target->orighost)) ||
					match(mask, target->info))
			{
				do_who(source, target, chptr, nullptr, fmt);
				--(*maxmatches);
			}
		}
	}
}

/*
 * who_global
 *
 * inputs	- pointer to client requesting who
 *		- char * mask to match
 *		- int if oper on a server or not
 *		- int if operspy or not
 *		- format options
 * output	- NONE
 * side effects - do a global scan of all clients looking for match
 *		  this is slightly expensive on EFnet ...
 *		  marks assumed cleared for all clients initially
 *		  and will be left cleared on return
 */
static void
who_global(client::client &source, const char *mask, int server_oper, int operspy, struct who_format *fmt)
{
	chan::membership *msptr;
	client::client *target_p;
	rb_dlink_node *lp, *ptr;
	int maxmatches = 500;

	/* first, list all matching INvisible clients on common channels
	 * if this is not an operspy who
	 */
	if(!operspy)
	{
		for(const auto &pit : chans(user(source)))
		{
			auto &chan(pit.first);
			who_common_channel(source, chan, mask, server_oper, &maxmatches, fmt);
		}
	}
//	else if (!ConfigFileEntry.operspy_dont_care_user_info)
//		report_operspy(&source, "WHO", mask);

	/* second, list all matching visible clients and clear all marks
	 * on invisible clients
	 * if this is an operspy who, list all matching clients, no need
	 * to clear marks
	 */
	RB_DLINK_FOREACH(ptr, global_client_list.head)
	{
		target_p = (client::client *)ptr->data;
		if(!is_person(*target_p))
			continue;

		if(is(*target_p, umode::INVISIBLE) && !operspy)
		{
			clear_mark(*target_p);
			continue;
		}

		if(server_oper && !is(*target_p, umode::OPER))
			continue;

		if(maxmatches > 0)
		{
			if(!mask ||
					match(mask, target_p->name) || match(mask, target_p->username) ||
					match(mask, target_p->host) || match(mask, target_p->servptr->name) ||
					(is(source, umode::OPER) && match(mask, target_p->orighost)) ||
					match(mask, target_p->info))
			{
				do_who(source, target_p, nullptr, nullptr, fmt);
				--maxmatches;
			}
		}
	}

	if (maxmatches <= 0)
		sendto_one(&source,
			form_str(ERR_TOOMANYMATCHES),
			me.name, source.name, "WHO");
}

/*
 * do_who_on_channel
 *
 * inputs	- pointer to client requesting who
 *		- pointer to channel to do who on
 *		- The "real name" of this channel
 *		- int if &source is a server oper or not
 *		- int if client is member or not
 *		- format options
 * output	- NONE
 * side effects - do a who on given channel
 */
static void
do_who_on_channel(client::client &source, chan::chan *chptr,
		  int server_oper, int source_member, struct who_format *fmt)
{
	for(auto &pit : chptr->members.global)
	{
		const auto &target(pit.first);
		auto &member(pit.second);

		if(server_oper && !is(*target, umode::OPER))
			continue;

		if(source_member || !is(*target, umode::INVISIBLE))
			do_who(source, target, chptr, &member, fmt);
	}
}

/*
 * append_format
 *
 * inputs	- pointer to buffer
 *		- size of buffer
 *		- pointer to position
 *		- format string
 *		- arguments for format
 * output	- NONE
 * side effects - position incremented, possibly beyond size of buffer
 *		  this allows detecting overflow
 */
static void
append_format(char *buf, size_t bufsize, size_t *pos, const char *fmt, ...)
{
	size_t max, result;
	va_list ap;

	max = *pos >= bufsize ? 0 : bufsize - *pos;
	va_start(ap, fmt);
	result = vsnprintf(buf + *pos, max, fmt, ap);
	va_end(ap);
	*pos += result;
}

/*
 * do_who
 *
 * inputs	- pointer to client requesting who
 *		- pointer to client to do who on
 *		- channel membership or NULL
 *		- format options
 * output	- NONE
 * side effects - do a who on given person
 */

static void
do_who(client::client &source, client::client *target_p, chan::chan *chan, chan::membership *msptr, struct who_format *fmt)
{
	char status[16];
	char str[510 + 1]; /* linebuf.c will add \r\n */
	size_t pos;
	const char *q;

	sprintf(status, "%c%s%s",
		   away(user(*target_p)).size()? 'G' : 'H', is(*target_p, umode::OPER) ? "*" : "", msptr ? find_status(msptr, fmt->fields || IsCapable(&source, CLICAP_MULTI_PREFIX)) : "");

	if (fmt->fields == 0)
		sendto_one(&source, form_str(RPL_WHOREPLY), me.name,
			   source.name, msptr ? chan->name.c_str() : "*",
			   target_p->username, target_p->host,
			   target_p->servptr->name, target_p->name, status,
			   ConfigServerHide.flatten_links && !is(source, umode::OPER) && !is_exempt_shide(source) ? 0 : target_p->hopcount,
			   target_p->info);
	else
	{
		str[0] = '\0';
		pos = 0;
		append_format(str, sizeof str, &pos, ":%s %d %s",
				me.name, RPL_WHOSPCRPL, source.name);
		if (fmt->fields & FIELD_QUERYTYPE)
			append_format(str, sizeof str, &pos, " %s", fmt->querytype);
		if (fmt->fields & FIELD_CHANNEL)
			append_format(str, sizeof str, &pos, " %s", msptr? name(*chan).c_str() : "*");
		if (fmt->fields & FIELD_USER)
			append_format(str, sizeof str, &pos, " %s", target_p->username);
		if (fmt->fields & FIELD_IP)
		{
			if (show_ip(&source, target_p) && !EmptyString(target_p->sockhost) && strcmp(target_p->sockhost, "0"))
				append_format(str, sizeof str, &pos, " %s", target_p->sockhost);
			else
				append_format(str, sizeof str, &pos, " %s", "255.255.255.255");
		}
		if (fmt->fields & FIELD_HOST)
			append_format(str, sizeof str, &pos, " %s", target_p->host);
		if (fmt->fields & FIELD_SERVER)
			append_format(str, sizeof str, &pos, " %s", target_p->servptr->name);
		if (fmt->fields & FIELD_NICK)
			append_format(str, sizeof str, &pos, " %s", target_p->name);
		if (fmt->fields & FIELD_FLAGS)
			append_format(str, sizeof str, &pos, " %s", status);
		if (fmt->fields & FIELD_HOP)
			append_format(str, sizeof str, &pos, " %d", ConfigServerHide.flatten_links && !is(source, umode::OPER) && !is_exempt_shide(source) ? 0 : target_p->hopcount);
		if (fmt->fields & FIELD_IDLE)
			append_format(str, sizeof str, &pos, " %d", (int)(my(*target_p) ? rb_current_time() - target_p->localClient->last : 0));
		if (fmt->fields & FIELD_ACCOUNT)
		{
			/* display as in whois */
			q = suser(user(*target_p)).c_str();
			if (!EmptyString(q))
			{
				while(rfc1459::is_digit(*q))
					q++;
				if(*q == '\0')
					q = suser(user(*target_p)).c_str();
			}
			else
				q = "0";
			append_format(str, sizeof str, &pos, " %s", q);
		}
		if (fmt->fields & FIELD_OPLEVEL)
			append_format(str, sizeof str, &pos, " %s", is_chanop(msptr) ? "999" : "n/a");
		if (fmt->fields & FIELD_INFO)
			append_format(str, sizeof str, &pos, " :%s", target_p->info);

		if (pos >= sizeof str)
		{
			static bool warned = false;
			if (!warned)
				sendto_realops_snomask(sno::DEBUG, L_NETWIDE,
						"WHOX overflow while sending information about %s to %s",
						target_p->name, source.name);
			warned = true;
		}
		sendto_one(&source, "%s", str);
	}
}