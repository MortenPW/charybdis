/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_webirc.c: Makes CGI:IRC users appear as coming from their real host
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2006 ircd-ratbox development team
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
/* Usage:
 * auth {
 *   user = "webirc@<cgiirc ip>"; # if identd used, put ident username instead
 *   password = "<password>"; # encryption possible
 *   spoof = "webirc."
 *   class = "users";
 * };
 * Possible flags:
 *   encrypted - password is encrypted (recommended)
 *   kline_exempt - klines on the cgiirc ip are ignored
 * dlines are checked on the cgiirc ip (of course).
 * k/d/x lines, auth blocks, user limits, etc are checked using the
 * real host/ip.
 * The password should be specified unencrypted in webirc_password in
 * cgiirc.config
 */

#include "stdinc.h"
#include "client.h"		/* client struct */
#include "match.h"
#include "hostmask.h"
#include "send.h"		/* sendto_one */
#include "numeric.h"		/* ERR_xxx */
#include "ircd.h"		/* me */
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "s_serv.h"
#include "hash.h"
#include "s_conf.h"
#include "reject.h"

static const char webirc_desc[] = "Adds support for the WebIRC system";

static void mr_webirc(struct MsgBuf *msgbuf_p, struct Client *, struct Client *, int, const char **);

struct Message webirc_msgtab = {
	"WEBIRC", 0, 0, 0, 0,
	{{mr_webirc, 5}, mg_reg, mg_ignore, mg_ignore, mg_ignore, mg_reg}
};

mapi_clist_av1 webirc_clist[] = { &webirc_msgtab, NULL };

DECLARE_MODULE_AV2(webirc, NULL, NULL, webirc_clist, NULL, NULL, NULL, NULL, webirc_desc);

/*
 * mr_webirc - webirc message handler
 *      parv[1] = password
 *      parv[2] = fake username (we ignore this)
 *	parv[3] = fake hostname
 *	parv[4] = fake ip
 */
static void
mr_webirc(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct ConfItem *aconf;
	const char *encr;
	struct rb_sockaddr_storage addr;

	if ((!strchr(parv[4], '.') && !strchr(parv[4], ':')) ||
			strlen(parv[4]) + (*parv[4] == ':') >=
			sizeof(source_p->sockhost))
	{
		sendto_one(source_p, "NOTICE * :Invalid IP");
		return;
	}

	aconf = find_address_conf(client_p->host, client_p->sockhost,
				IsGotId(client_p) ? client_p->username : "webirc",
				IsGotId(client_p) ? client_p->username : "webirc",
				(struct sockaddr *) &client_p->localClient->ip,
				GET_SS_FAMILY(&client_p->localClient->ip), NULL);
	if (aconf == NULL || !(aconf->status & CONF_CLIENT))
		return;
	if (!IsConfDoSpoofIp(aconf) || irccmp(aconf->info.name, "webirc."))
	{
		/* XXX */
		sendto_one(source_p, "NOTICE * :Not a CGI:IRC auth block");
		return;
	}
	if (EmptyString(aconf->passwd))
	{
		sendto_one(source_p, "NOTICE * :CGI:IRC auth blocks must have a password");
		return;
	}

	if (EmptyString(parv[1]))
		encr = "";
	else if (IsConfEncrypted(aconf))
		encr = rb_crypt(parv[1], aconf->passwd);
	else
		encr = parv[1];

	if (encr == NULL || strcmp(encr, aconf->passwd))
	{
		sendto_one(source_p, "NOTICE * :CGI:IRC password incorrect");
		return;
	}

	if (rb_inet_pton_sock(parv[4], (struct sockaddr *)&addr) <= 0)
	{
		sendto_one(source_p, "NOTICE * :Invalid IP");
		return;
	}

	if (*parv[4] == ':')
	{
		source_p->sockhost[0] = '0';
		rb_strlcpy(source_p->sockhost + 1, parv[4],
				sizeof(source_p->sockhost) - 1);
	}
	else
		rb_strlcpy(source_p->sockhost, parv[4],
				sizeof(source_p->sockhost));

	if(strlen(parv[3]) <= HOSTLEN)
		rb_strlcpy(source_p->host, parv[3], sizeof(source_p->host));
	else
		rb_strlcpy(source_p->host, source_p->sockhost, sizeof(source_p->host));

	source_p->localClient->ip = addr;

	/* Check dlines now, klines will be checked on registration */
	if((aconf = find_dline((struct sockaddr *)&source_p->localClient->ip,
			       GET_SS_FAMILY(&source_p->localClient->ip))))
	{
		if(!(aconf->status & CONF_EXEMPTDLINE))
		{
			exit_client(client_p, source_p, &me, "D-lined");
			return;
		}
	}

	sendto_one(source_p, "NOTICE * :CGI:IRC host/IP set to %s %s", parv[3], parv[4]);
}