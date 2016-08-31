/*
 *  ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 *  m_etrace.c: Gives local opers a trace output with added info.
 *
 *  Copyright (C) 2002-2003 Lee Hardy <lee@leeh.co.uk>
 *  Copyright (C) 2002-2005 ircd-ratbox development team
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *  1.Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  2.Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  3.The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 *  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 *  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 *  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

using namespace ircd;

static const char etrace_desc[] =
    "Provides enhanced tracing facilities to opers (ETRACE, CHANTRACE, and MASKTRACE)";

static void mo_etrace(struct MsgBuf *, client::client &, client::client &, int, const char **);
static void me_etrace(struct MsgBuf *, client::client &, client::client &, int, const char **);
static void m_chantrace(struct MsgBuf *, client::client &, client::client &, int, const char **);
static void mo_masktrace(struct MsgBuf *, client::client &, client::client &, int, const char **);

struct Message etrace_msgtab = {
	"ETRACE", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, {me_etrace, 0}, {mo_etrace, 0}}
};
struct Message chantrace_msgtab = {
	"CHANTRACE", 0, 0, 0, 0,
	{mg_ignore, {m_chantrace, 2}, mg_ignore, mg_ignore, mg_ignore, {m_chantrace, 2}}
};
struct Message masktrace_msgtab = {
	"MASKTRACE", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {mo_masktrace, 2}}
};

static int
_modinit(void)
{
	supported::add("ETRACE");
	return 0;
}

static void
_moddeinit(void)
{
	supported::del("ETRACE");
}

mapi_clist_av1 etrace_clist[] = { &etrace_msgtab, &chantrace_msgtab, &masktrace_msgtab, NULL };

DECLARE_MODULE_AV2(etrace, _modinit, _moddeinit, etrace_clist, NULL, NULL, NULL, NULL, etrace_desc);

static void do_etrace(client::client &source, int ipv4, int ipv6);
static void do_etrace_full(client::client &source);
static void do_single_etrace(client::client &source, client::client *target_p);

static const char *empty_sockhost = "255.255.255.255";
static const char *spoofed_sockhost = "0";

/*
 * m_etrace
 *      parv[1] = options [or target]
 *	parv[2] = [target]
 */
static void
mo_etrace(struct MsgBuf *msgbuf_p, client::client &client, client::client &source, int parc, const char *parv[])
{
	if(parc > 1 && !EmptyString(parv[1]))
	{
		if(!irccmp(parv[1], "-full"))
			do_etrace_full(source);
#ifdef RB_IPV6
		else if(!irccmp(parv[1], "-v6"))
			do_etrace(source, 0, 1);
		else if(!irccmp(parv[1], "-v4"))
			do_etrace(source, 1, 0);
#endif
		else
		{
			client::client *target_p = client::find_named_person(parv[1]);

			if(target_p)
			{
				if(!my(*target_p))
					sendto_one(target_p, ":%s ENCAP %s ETRACE %s",
						get_id(&source, target_p),
						target_p->servptr->name,
						get_id(target_p, target_p));
				else
					do_single_etrace(source, target_p);
			}
			else
				sendto_one_numeric(&source, ERR_NOSUCHNICK,
						form_str(ERR_NOSUCHNICK), parv[1]);
		}
	}
	else
		do_etrace(source, 1, 1);
}

static void
me_etrace(struct MsgBuf *msgbuf_p, client::client &client, client::client &source, int parc, const char *parv[])
{
	client::client *target_p;

	if(!is(source, umode::OPER) || parc < 2 || EmptyString(parv[1]))
		return;

	/* we cant etrace remote clients.. we shouldnt even get sent them */
	if((target_p = client::find_person(parv[1])) && my(*target_p))
		do_single_etrace(source, target_p);

        sendto_one_numeric(&source, RPL_ENDOFTRACE, form_str(RPL_ENDOFTRACE),
				target_p ? target_p->name : parv[1]);
}

static void
do_etrace(client::client &source, int ipv4, int ipv6)
{
	client::client *target_p;
	rb_dlink_node *ptr;

	/* report all direct connections */
	RB_DLINK_FOREACH(ptr, lclient_list.head)
	{
		target_p = (client::client *)ptr->data;

#ifdef RB_IPV6
		if((!ipv4 && GET_SS_FAMILY(&target_p->localClient->ip) == AF_INET) ||
		   (!ipv6 && GET_SS_FAMILY(&target_p->localClient->ip) == AF_INET6))
			continue;
#endif

		sendto_one(&source, form_str(RPL_ETRACE),
			   me.name, source.name,
			   is(*target_p, umode::OPER) ? "Oper" : "User",
			   get_client_class(target_p),
			   target_p->name, target_p->username, target_p->host,
			   show_ip(&source, target_p) ? target_p->sockhost : "255.255.255.255",
			   target_p->info);
	}

	sendto_one_numeric(&source, RPL_ENDOFTRACE, form_str(RPL_ENDOFTRACE), me.name);
}

static void
do_etrace_full(client::client &source)
{
	rb_dlink_node *ptr;

	RB_DLINK_FOREACH(ptr, lclient_list.head)
		do_single_etrace(source, (client::client *)ptr->data);

	sendto_one_numeric(&source, RPL_ENDOFTRACE, form_str(RPL_ENDOFTRACE), me.name);
}

/*
 * do_single_etrace  - searches local clients and displays those matching
 *                     a pattern
 * input             - source client, target client
 * output	     - etrace results
 * side effects	     - etrace results are displayed
 */
static void
do_single_etrace(client::client &source, client::client *target_p)
{
	/* note, we hide fullcaps for spoofed users, as mirc can often
	 * advertise its internal ip address in the field --fl
	 */
	if(!show_ip(&source, target_p))
		sendto_one(&source, form_str(RPL_ETRACEFULL),
				me.name, source.name,
				is(*target_p, umode::OPER) ? "Oper" : "User",
				get_client_class(target_p),
				target_p->name, target_p->username, target_p->host,
				"255.255.255.255", "<hidden> <hidden>", target_p->info);
	else
		sendto_one(&source, form_str(RPL_ETRACEFULL),
				me.name, source.name,
				is(*target_p, umode::OPER) ? "Oper" : "User",
				get_client_class(target_p),
				target_p->name, target_p->username,
				target_p->host, target_p->sockhost,
				target_p->localClient->fullcaps, target_p->info);
}

static void
m_chantrace(struct MsgBuf *msgbuf_p, client::client &client, client::client &source, int parc, const char *parv[])
{
	client::client *target_p;
	chan::chan *chptr;
	const char *sockhost;
	const char *name;
	rb_dlink_node *ptr;
	int operspy = 0;

	name = parv[1];

	if(IsOperSpy(&source) && parv[1][0] == '!')
	{
		name++;
		operspy = 1;

		if(EmptyString(name))
		{
			sendto_one(&source, form_str(ERR_NEEDMOREPARAMS),
					me.name, source.name, "CHANTRACE");
			return;
		}
	}

	if((chptr = chan::get(name, std::nothrow)) == NULL)
	{
		sendto_one_numeric(&source, ERR_NOSUCHCHANNEL, form_str(ERR_NOSUCHCHANNEL),
				name);
		return;
	}

	/* dont report operspys for nonexistant channels. */
//	if(operspy)
//		report_operspy(&source, "CHANTRACE", chptr->name.c_str());

	if(!operspy && !is_member(chptr, &client))
	{
		sendto_one_numeric(&source, ERR_NOTONCHANNEL, form_str(ERR_NOTONCHANNEL),
				chptr->name.c_str());
		return;
	}

	for(const auto &pit : chptr->members.global)
	{
		auto &target_p(pit.first);
		auto &member(pit.second);

		if(EmptyString(target_p->sockhost))
			sockhost = empty_sockhost;
		else if(!show_ip(&source, target_p))
			sockhost = spoofed_sockhost;
		else
			sockhost = target_p->sockhost;

		sendto_one(&source, form_str(RPL_ETRACE),
				me.name, source.name,
				is(*target_p, umode::OPER) ? "Oper" : "User",
				/* class field -- pretend its server.. */
				target_p->servptr->name,
				target_p->name, target_p->username, target_p->host,
				sockhost, target_p->info);
	}

	sendto_one_numeric(&source, RPL_ENDOFTRACE, form_str(RPL_ENDOFTRACE), me.name);
}

static void
match_masktrace(client::client &source, rb_dlink_list *list,
	const char *username, const char *hostname, const char *name,
	const char *gecos)
{
	client::client *target_p;
	rb_dlink_node *ptr;
	const char *sockhost;

	RB_DLINK_FOREACH(ptr, list->head)
	{
		target_p = (client::client *)ptr->data;
		if(!is_person(*target_p))
			continue;

		if(EmptyString(target_p->sockhost))
			sockhost = empty_sockhost;
		else if(!show_ip(&source, target_p))
			sockhost = spoofed_sockhost;
		else
			sockhost = target_p->sockhost;

		if(match(username, target_p->username) &&
		   (match(hostname, target_p->host) ||
		    match(hostname, target_p->orighost) ||
		    match(hostname, sockhost) || match_ips(hostname, sockhost)))
		{
			if(name != NULL && !match(name, target_p->name))
				continue;

			if(gecos != NULL && !match_esc(gecos, target_p->info))
				continue;

			sendto_one(&source, form_str(RPL_ETRACE),
				me.name, source.name,
				is(*target_p, umode::OPER) ? "Oper" : "User",
				/* class field -- pretend its server.. */
				target_p->servptr->name,
				target_p->name, target_p->username, target_p->host,
				sockhost, target_p->info);
		}
	}
}

static void
mo_masktrace(struct MsgBuf *msgbuf_p, client::client &client, client::client &source, int parc,
	const char *parv[])
{
	char *name, *username, *hostname, *gecos;
	const char *mask;
	int operspy = 0;

	mask = parv[1];
	name = LOCAL_COPY(parv[1]);
	collapse(name);

	if(IsOperSpy(&source) && parv[1][0] == '!')
	{
		name++;
		mask++;
		operspy = 1;
	}

	if(parc > 2 && !EmptyString(parv[2]))
	{
		gecos = LOCAL_COPY(parv[2]);
		collapse_esc(gecos);
	} else
		gecos = NULL;


	if((hostname = strchr(name, '@')) == NULL)
	{
		sendto_one_notice(&source, ":Invalid parameters");
		return;
	}

	*hostname++ = '\0';

	if((username = strchr(name, '!')) == NULL)
	{
		username = name;
		name = NULL;
	} else
		*username++ = '\0';

	if(EmptyString(username) || EmptyString(hostname))
	{
		sendto_one_notice(&source, ":Invalid parameters");
		return;
	}

	if(operspy) {
		if (!ConfigFileEntry.operspy_dont_care_user_info)
		{
			char buf[512];
			rb_strlcpy(buf, mask, sizeof(buf));
			if(!EmptyString(gecos)) {
				rb_strlcat(buf, " ", sizeof(buf));
				rb_strlcat(buf, gecos, sizeof(buf));
			}

//			report_operspy(&source, "MASKTRACE", buf);
		}
		match_masktrace(source, &global_client_list, username, hostname, name, gecos);
	} else
		match_masktrace(source, &lclient_list, username, hostname, name, gecos);

	sendto_one_numeric(&source, RPL_ENDOFTRACE, form_str(RPL_ENDOFTRACE), me.name);
}