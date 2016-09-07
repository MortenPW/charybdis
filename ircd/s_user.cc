/*
 *  ircd-ratbox: A slightly useful ircd.
 *  s_user.c: User related functions.
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

namespace ircd {

static void report_and_set_user_flags(client::client *, struct ConfItem *);
void user_welcome(client::client *source_p);

/*
 * show_lusers -
 *
 * inputs	- pointer to client
 * output	-
 * side effects	- display to client user counts etc.
 */
void
show_lusers(client::client *source_p)
{
	if(rb_dlink_list_length(&lclient_list) > (unsigned long)MaxClientCount)
		MaxClientCount = rb_dlink_list_length(&lclient_list);

	if((rb_dlink_list_length(&lclient_list) + rb_dlink_list_length(&serv_list)) >
	   (unsigned long)MaxConnectionCount)
		MaxConnectionCount = rb_dlink_list_length(&lclient_list) +
					rb_dlink_list_length(&serv_list);

	sendto_one_numeric(source_p, RPL_LUSERCLIENT, form_str(RPL_LUSERCLIENT),
			   (Count.total - Count.invisi),
			   Count.invisi,
			   (int)rb_dlink_list_length(&global_serv_list));

	if(rb_dlink_list_length(&oper_list) > 0)
		sendto_one_numeric(source_p, RPL_LUSEROP,
				   form_str(RPL_LUSEROP),
				   (int)rb_dlink_list_length(&oper_list));

	if(rb_dlink_list_length(&unknown_list) > 0)
		sendto_one_numeric(source_p, RPL_LUSERUNKNOWN,
				   form_str(RPL_LUSERUNKNOWN),
				   (int)rb_dlink_list_length(&unknown_list));

	if(!chan::chans.empty())
		sendto_one_numeric(source_p, RPL_LUSERCHANNELS,
				   form_str(RPL_LUSERCHANNELS),
				   chan::chans.size());

	sendto_one_numeric(source_p, RPL_LUSERME, form_str(RPL_LUSERME),
			   (int)rb_dlink_list_length(&lclient_list),
			   (int)rb_dlink_list_length(&serv_list));

	sendto_one_numeric(source_p, RPL_LOCALUSERS,
			   form_str(RPL_LOCALUSERS),
			   (int)rb_dlink_list_length(&lclient_list),
			   Count.max_loc,
			   (int)rb_dlink_list_length(&lclient_list),
			   Count.max_loc);

	sendto_one_numeric(source_p, RPL_GLOBALUSERS, form_str(RPL_GLOBALUSERS),
			   Count.total, Count.max_tot,
			   Count.total, Count.max_tot);

	sendto_one_numeric(source_p, RPL_STATSCONN,
			   form_str(RPL_STATSCONN),
			   MaxConnectionCount, MaxClientCount,
			   Count.totalrestartcount);
}

/* check if we should exit a client due to authd decision
 * inputs	- client server, client connecting
 * outputs	- true if exited, false if not
 * side effects	- messages/exits client if authd rejected and not exempt
 */
static bool
authd_check(client::client *client_p, client::client *source_p)
{
	struct ConfItem *aconf = source_p->localClient->att_conf;
	rb_dlink_list varlist = { NULL, NULL, 0 };
	bool reject = false;
	char *reason;

	if(source_p->preClient->auth.accepted == true)
		return reject;

	substitution_append_var(&varlist, "nick", source_p->name);
	substitution_append_var(&varlist, "ip", source_p->sockhost);
	substitution_append_var(&varlist, "host", source_p->host);
	substitution_append_var(&varlist, "dnsbl-host", source_p->preClient->auth.data);
	substitution_append_var(&varlist, "network-name", ServerInfo.network_name);
	reason = substitution_parse(source_p->preClient->auth.reason, &varlist);

	switch(source_p->preClient->auth.cause)
	{
	case 'B':	/* Blacklists */
		{
			struct BlacklistStats *stats;
			char *blacklist = source_p->preClient->auth.data;

			if(bl_stats != NULL)
				if((stats = (BlacklistStats *)rb_dictionary_retrieve(bl_stats, blacklist)) != NULL)
					stats->hits++;

			if(is_exempt_kline(*source_p) || IsConfExemptDNSBL(aconf))
			{
				sendto_one_notice(source_p, ":*** Your IP address %s is listed in %s, but you are exempt",
						source_p->sockhost, blacklist);
				break;
			}

			sendto_realops_snomask(sno::REJ, L_NETWIDE,
				"Listed on DNSBL %s: %s (%s@%s) [%s] [%s]",
				blacklist, source_p->name, source_p->username, source_p->host,
				is_ip_spoof(*source_p) ? "255.255.255.255" : source_p->sockhost,
				source_p->info);

			sendto_one(source_p, form_str(ERR_YOUREBANNEDCREEP),
				me.name, source_p->name, reason);

			sendto_one_notice(source_p, ":*** Your IP address %s is listed in %s",
				source_p->sockhost, blacklist);
			add_reject(source_p, NULL, NULL);
			exit_client(client_p, source_p, &me, "Banned (DNS blacklist)");
			reject = true;
		}
		break;
	case 'O':	/* OPM */
		{
			char *proxy = source_p->preClient->auth.data;
			char *port = strrchr(proxy, ':');

			if(port == NULL)
			{
				/* This shouldn't happen, better tell the ops... */
				ierror("authd sent us a malformed OPM string %s", proxy);
				sendto_realops_snomask(sno::GENERAL, L_ALL,
					"authd sent us a malformed OPM string %s", proxy);
				break;
			}

			/* Terminate the proxy type */
			*(port++) = '\0';

			if(is_exempt_kline(*source_p) || IsConfExemptProxy(aconf))
			{
				sendto_one_notice(source_p,
					":*** Your IP address %s has been detected as an open proxy (type %s, port %s), but you are exempt",
					source_p->sockhost, proxy, port);
				break;
			}
			sendto_realops_snomask(sno::REJ, L_NETWIDE,
				"Open proxy %s/%s: %s (%s@%s) [%s] [%s]",
				proxy, port,
				source_p->name,
				source_p->username, source_p->host,
				is_ip_spoof(*source_p) ? "255.255.255.255" : source_p->sockhost,
				source_p->info);

			sendto_one(source_p, form_str(ERR_YOUREBANNEDCREEP),
					me.name, source_p->name, reason);

			sendto_one_notice(source_p,
				":*** Your IP address %s has been detected as an open proxy (type %s, port %s)",
				source_p->sockhost, proxy, port);
			add_reject(source_p, NULL, NULL);
			exit_client(client_p, source_p, &me, "Banned (Open proxy)");
			reject = true;
		}
		break;
	default:	/* Unknown, but handle the case properly */
		if(is_exempt_kline(*source_p))
		{
			sendto_one_notice(source_p,
				":*** You were rejected, but you are exempt (reason: %s)",
				reason);
			break;
		}
		sendto_realops_snomask(sno::REJ, L_NETWIDE,
			"Rejected by authentication system (reason %s): %s (%s@%s) [%s] [%s]",
			reason, source_p->name, source_p->username, source_p->host,
			is_ip_spoof(*source_p) ? "255.255.255.255" : source_p->sockhost,
			source_p->info);

		sendto_one(source_p, form_str(ERR_YOUREBANNEDCREEP),
			me.name, source_p->name, reason);

		sendto_one_notice(source_p, ":*** Rejected by authentication system: %s",
			reason);
		add_reject(source_p, NULL, NULL);
		exit_client(client_p, source_p, &me, "Banned (authentication system)");
		reject = true;
		break;
	}

	if(reject)
		ServerStats.is_ref++;

	substitution_free(&varlist);

	return reject;
}

/*
** register_local_user
**      This function is called when both NICK and USER messages
**      have been accepted for the client, in whatever order. Only
**      after this, is the USER message propagated.
**
**      NICK's must be propagated at once when received, although
**      it would be better to delay them too until full info is
**      available. Doing it is not so simple though, would have
**      to implement the following:
**
**      (actually it has been implemented already for a while) -orabidoo
**
**      1) user telnets in and gives only "NICK foobar" and waits
**      2) another user far away logs in normally with the nick
**         "foobar" (quite legal, as this server didn't propagate
**         it).
**      3) now this server gets nick "foobar" from outside, but
**         has alread the same defined locally. Current server
**         would just issue "KILL foobar" to clean out dups. But,
**         this is not fair. It should actually request another
**         nick from local user or kill him/her...
 */
int
register_local_user(client::client *client_p, client::client *source_p)
{
	struct ConfItem *aconf, *xconf;
	char tmpstr2[BUFSIZE];
	char ipaddr[HOSTIPLEN];
	char myusername[USERLEN+1];
	int status;

	s_assert(NULL != source_p);
	s_assert(my_connect(*source_p));

	if(source_p == NULL)
		return -1;

	if(is_any_dead(*source_p))
		return -1;

	if(ConfigFileEntry.ping_cookie)
	{
		if(!(source_p->flags & client::flags::PINGSENT) && source_p->localClient->random_ping == 0)
		{
			source_p->localClient->random_ping = (uint32_t)(((rand() * rand()) << 1) | 1);
			sendto_one(source_p, "PING :%08X",
				   (unsigned int) source_p->localClient->random_ping);
			source_p->flags |= client::flags::PINGSENT;
			return -1;
		}
		if(!(source_p->flags & client::flags::PING_COOKIE))
		{
			return -1;
		}
	}

	/* hasnt finished client cap negotiation */
	if(source_p->flags & client::flags::CLICAP)
		return -1;

	/* Waiting on authd */
	if(source_p->preClient->auth.cid)
		return -1;

	client_p->localClient->last = rb_current_time();

	/* XXX - fixme. we shouldnt have to build a users buffer twice.. */
	if(!is_got_id(*source_p) && (strchr(source_p->username, '[') != NULL))
	{
		const char *p;
		int i = 0;

		p = source_p->username;

		while(*p && i < USERLEN)
		{
			if(*p != '[')
				myusername[i++] = *p;
			p++;
		}

		myusername[i] = '\0';
	}
	else
		rb_strlcpy(myusername, source_p->username, sizeof myusername);

	if((status = check_client(client_p, source_p, myusername)) < 0)
		return (CLIENT_EXITED);

	/* Apply nick override */
	if(*source_p->preClient->spoofnick)
	{
		char note[NICKLEN + 10];

		del_from_client_hash(source_p->name, source_p);
		rb_strlcpy(source_p->name, source_p->preClient->spoofnick, NICKLEN + 1);
		add_to_client_hash(source_p->name, source_p);

		snprintf(note, NICKLEN + 10, "Nick: %s", source_p->name);
		rb_note(source_p->localClient->F, note);
	}

	if(!valid_hostname(source_p->host))
	{
		sendto_one_notice(source_p, ":*** Notice -- You have an illegal character in your hostname");

		rb_strlcpy(source_p->host, source_p->sockhost, sizeof(source_p->host));
 	}

	aconf = source_p->localClient->att_conf;

	if(aconf == NULL)
	{
		exit_client(client_p, source_p, &me, "*** Not Authorised");
		return (CLIENT_EXITED);
	}

	if(IsConfSSLNeeded(aconf) && !IsSSL(source_p))
	{
		ServerStats.is_ref++;
		sendto_one_notice(source_p, ":*** Notice -- You need to use SSL/TLS to use this server");
		exit_client(client_p, source_p, &me, "Use SSL/TLS");
		return (CLIENT_EXITED);
	}

	if(!is_got_id(*source_p))
	{
		const char *p;
		int i = 0;

		if(IsNeedIdentd(aconf))
		{
			ServerStats.is_ref++;
			sendto_one_notice(source_p, ":*** Notice -- You need to install identd to use this server");
			exit_client(client_p, source_p, &me, "Install identd");
			return (CLIENT_EXITED);
		}

		/* dont replace username if its supposed to be spoofed --fl */
		if(!IsConfDoSpoofIp(aconf) || !strchr(aconf->info.name, '@'))
		{
			p = myusername;

			if(!IsNoTilde(aconf))
				source_p->username[i++] = '~';

			while (*p && i < USERLEN)
			{
				if(*p != '[')
					source_p->username[i++] = *p;
				p++;
			}

			source_p->username[i] = '\0';
		}
	}

	if(IsNeedSasl(aconf) && suser(user(*source_p)).empty())
	{
		ServerStats.is_ref++;
		sendto_one_notice(source_p, ":*** Notice -- You need to identify via SASL to use this server");
		exit_client(client_p, source_p, &me, "SASL access only");
		return (CLIENT_EXITED);
	}

	/* password check */
	if(!EmptyString(aconf->passwd))
	{
		const char *encr;

		if(EmptyString(source_p->localClient->passwd))
			encr = "";
		else if(IsConfEncrypted(aconf))
			encr = rb_crypt(source_p->localClient->passwd, aconf->passwd);
		else
			encr = source_p->localClient->passwd;

		if(encr == NULL || strcmp(encr, aconf->passwd))
		{
			ServerStats.is_ref++;
			sendto_one(source_p, form_str(ERR_PASSWDMISMATCH), me.name, source_p->name);
			exit_client(client_p, source_p, &me, "Bad Password");
			return (CLIENT_EXITED);
		}

		/* clear password only if used now, otherwise send it
		 * to services -- jilles */
		if(source_p->localClient->passwd)
		{
			memset(source_p->localClient->passwd, 0, strlen(source_p->localClient->passwd));
			rb_free(source_p->localClient->passwd);
			source_p->localClient->passwd = NULL;
		}
	}

	/* report and set flags (kline exempt etc.) as needed in source_p */
	report_and_set_user_flags(source_p, aconf);

	/* Limit clients */
	/*
	 * We want to be able to have servers and F-line clients
	 * connect, so save room for "buffer" connections.
	 * Smaller servers may want to decrease this, and it should
	 * probably be just a percentage of the MAXCLIENTS...
	 *   -Taner
	 */
	/* Except "F:" clients */
	if(rb_dlink_list_length(&lclient_list) >=
	    (unsigned long)GlobalSetOptions.maxclients && !IsConfExemptLimits(aconf))
	{
		sendto_realops_snomask(sno::FULL, L_ALL,
				     "Too many clients, rejecting %s[%s].", source_p->name, source_p->host);

		ServerStats.is_ref++;
		exit_client(client_p, source_p, &me, "Sorry, server is full - try later");
		return (CLIENT_EXITED);
	}

	/* kline exemption extends to xline too */
	if(!is_exempt_kline(*source_p) &&
	   (xconf = find_xline(source_p->info, 1)) != NULL)
	{
		ServerStats.is_ref++;
		add_reject(source_p, xconf->host, NULL);
		exit_client(client_p, source_p, &me, "Bad user info");
		return CLIENT_EXITED;
	}

	/* authd rejection check */
	if(authd_check(client_p, source_p))
		return CLIENT_EXITED;

	/* valid user name check */

	if(!valid_username(source_p->username))
	{
		sendto_realops_snomask(sno::REJ, L_ALL,
				     "Invalid username: %s (%s@%s)",
				     source_p->name, source_p->username, source_p->host);
		ServerStats.is_ref++;
		sendto_one_notice(source_p, ":*** Your username is invalid. Please make sure that your username contains "
					    "only alphanumeric characters.");
		sprintf(tmpstr2, "Invalid username [%s]", source_p->username);
		exit_client(client_p, source_p, &me, tmpstr2);
		return (CLIENT_EXITED);
	}

	/* end of valid user name check */

	/* Store original hostname -- jilles */
	rb_strlcpy(source_p->orighost, source_p->host, HOSTLEN + 1);

	/* Spoof user@host */
	if(*source_p->preClient->spoofuser)
		rb_strlcpy(source_p->username, source_p->preClient->spoofuser, USERLEN + 1);
	if(*source_p->preClient->spoofhost)
	{
		rb_strlcpy(source_p->host, source_p->preClient->spoofhost, HOSTLEN + 1);
		if (irccmp(source_p->host, source_p->orighost))
			set_dyn_spoof(*source_p);
	}

	source_p->mode |= (ConfigFileEntry.default_umodes & ~ConfigFileEntry.oper_only_umodes);

	call_hook(h_new_local_user, source_p);

	/* If they have died in send_* or were thrown out by the
	 * new_local_user hook don't do anything. */
	if(is_any_dead(*source_p))
		return CLIENT_EXITED;

	/* To avoid inconsistencies, do not abort the registration
	 * starting from this point -- jilles
	 */
	rb_inet_ntop_sock((struct sockaddr *)&source_p->localClient->ip, ipaddr, sizeof(ipaddr));

	sendto_realops_snomask(sno::CCONN, L_ALL,
			     "Client connecting: %s (%s@%s) [%s] {%s} [%s]",
			     source_p->name, source_p->username, source_p->orighost,
			     show_ip(NULL, source_p) ? ipaddr : "255.255.255.255",
			     get_client_class(source_p), source_p->info);

	sendto_realops_snomask(sno::CCONNEXT, L_ALL,
			"CLICONN %s %s %s %s %s %s 0 %s",
			source_p->name, source_p->username, source_p->orighost,
			show_ip(NULL, source_p) ? ipaddr : "255.255.255.255",
			get_client_class(source_p),
			/* mirc can sometimes send ips here */
			show_ip(NULL, source_p) ? source_p->localClient->fullcaps : "<hidden> <hidden>",
			source_p->info);

	add_to_hostname_hash(source_p->orighost, source_p);

	/* Allocate a UID if it was not previously allocated.
	 * If this already occured, it was probably during SASL auth...
	 */
	if(!*source_p->id)
	{
		rb_strlcpy(source_p->id, client::generate_uid(), sizeof(source_p->id));
		add_to_id_hash(source_p->id, source_p);
	}

	if (IsSSL(source_p))
		source_p->mode |= umode::SSLCLIENT;

	if (source_p->mode & umode::INVISIBLE)
		Count.invisi++;

	s_assert(!is_client(*source_p));
	rb_dlinkMoveNode(&source_p->localClient->tnode, &unknown_list, &lclient_list);
	set_client(*source_p);

	source_p->servptr = &me;
	source_p->lnode = users(serv(*source_p->servptr)).emplace(end(users(serv(*source_p->servptr))), source_p);

	/* Increment our total user count here */
	if(++Count.total > Count.max_tot)
		Count.max_tot = Count.total;

	Count.totalrestartcount++;

	s_assert(source_p->localClient != NULL);

	if(rb_dlink_list_length(&lclient_list) > (unsigned long)Count.max_loc)
	{
		Count.max_loc = rb_dlink_list_length(&lclient_list);
		if(!(Count.max_loc % 10))
			sendto_realops_snomask(sno::GENERAL, L_ALL,
					     "New Max Local Clients: %d", Count.max_loc);
	}

	/* they get a reduced limit */
	if(find_tgchange(source_p->sockhost))
		source_p->localClient->targets_free = tgchange::INITIAL_LOW;
	else
		source_p->localClient->targets_free = tgchange::INITIAL;

	monitor_signon(source_p);
	user_welcome(source_p);

	free_pre_client(source_p);

	introduce_client(client_p, source_p, source_p->name, 1);
	return 0;
}

/*
 * introduce_clients
 *
 * inputs	-
 * output	-
 * side effects - This common function introduces a client to the rest
 *		  of the net, either from a local client connect or
 *		  from a remote connect.
 */
void
introduce_client(client::client *client_p, client::client *source_p, const char *nick, int use_euid)
{
	char ubuf[BUFSIZE];
	client::client *identifyservice_p;
	char *p;
	hook_data_umode_changed hdata;
	hook_data_client hdata2;

	delta(umode::table, 0, source_p->mode, ubuf);

	if(my(*source_p))
		send_umode(*source_p, *source_p, ubuf);

	s_assert(has_id(source_p));

	if (use_euid)
		sendto_server(client_p, NULL, CAP_EUID | CAP_TS6, NOCAPS,
				":%s EUID %s %d %ld %s %s %s %s %s %s %s :%s",
				source_p->servptr->id, nick,
				source_p->hopcount + 1,
				(long) source_p->tsinfo, ubuf,
				source_p->username, source_p->host,
				is_ip_spoof(*source_p) ? "0" : source_p->sockhost,
				source_p->id,
				is_dyn_spoof(*source_p) ? source_p->orighost : "*",
				suser(user(*source_p)).empty()? "*" : suser(user(*source_p)).c_str(),
				source_p->info);

	sendto_server(client_p, NULL, CAP_TS6, use_euid ? CAP_EUID : NOCAPS,
		      ":%s UID %s %d %ld %s %s %s %s %s :%s",
		      source_p->servptr->id, nick,
		      source_p->hopcount + 1,
		      (long) source_p->tsinfo, ubuf,
		      source_p->username, source_p->host,
		      is_ip_spoof(*source_p) ? "0" : source_p->sockhost,
		      source_p->id, source_p->info);

	if(!EmptyString(source_p->certfp))
		sendto_server(client_p, NULL, CAP_TS6, NOCAPS,
				":%s ENCAP * CERTFP :%s",
				use_id(source_p), source_p->certfp);

	if (is_dyn_spoof(*source_p))
	{
		sendto_server(client_p, NULL, CAP_TS6, use_euid ? CAP_EUID : NOCAPS, ":%s ENCAP * REALHOST %s",
				use_id(source_p), source_p->orighost);
	}

	if (!suser(user(*source_p)).empty())
	{
		sendto_server(client_p, NULL, CAP_TS6, use_euid ? CAP_EUID : NOCAPS, ":%s ENCAP * LOGIN %s",
				use_id(source_p), suser(user(*source_p)).c_str());
	}

	if(my_connect(*source_p) && source_p->localClient->passwd)
	{
		if (!EmptyString(ConfigFileEntry.identifyservice) &&
				!EmptyString(ConfigFileEntry.identifycommand))
		{
			/* use user@server */
			p = strchr(ConfigFileEntry.identifyservice, '@');
			if (p != NULL)
				identifyservice_p = find_named_client(p + 1);
			else
				identifyservice_p = NULL;
			if (identifyservice_p != NULL)
			{
				if (!EmptyString(source_p->localClient->auth_user))
					sendto_one(identifyservice_p, ":%s PRIVMSG %s :%s %s %s",
							get_id(source_p, identifyservice_p),
							ConfigFileEntry.identifyservice,
							ConfigFileEntry.identifycommand,
							source_p->localClient->auth_user,
							source_p->localClient->passwd);
				else
					sendto_one(identifyservice_p, ":%s PRIVMSG %s :%s %s",
							get_id(source_p, identifyservice_p),
							ConfigFileEntry.identifyservice,
							ConfigFileEntry.identifycommand,
							source_p->localClient->passwd);
			}
		}
		memset(source_p->localClient->passwd, 0, strlen(source_p->localClient->passwd));
		rb_free(source_p->localClient->passwd);
		source_p->localClient->passwd = NULL;
	}

	/* let modules providing usermodes know that we've got a new user,
	 * why is this here? -- well, some modules need to be able to send out new
	 * information about a client, so this was the best place to do it
	 *    --nenolod
	 */
	hdata.client = source_p;
	hdata.oldumodes = 0;
	hdata.oldsnomask = 0;
	call_hook(h_umode_changed, &hdata);

	/* On the other hand, some modules need to know when a client is
	 * being introduced, period.
	 * --gxti
	 */
	hdata2.client = client_p;
	hdata2.target = source_p;
	call_hook(h_introduce_client, &hdata2);
}

/*
 * valid_hostname - check hostname for validity
 *
 * Inputs       - pointer to user
 * Output       - true if valid, false if not
 * Side effects - NONE
 *
 * NOTE: this doesn't allow a hostname to begin with a dot and
 * will not allow more dots than chars.
 */
bool
valid_hostname(const char *hostname)
{
	const char *p = hostname, *last_slash = 0;
	int found_sep = 0;

	s_assert(NULL != p);

	if(hostname == NULL)
		return false;

	if(!strcmp(hostname, "localhost"))
		return true;

	if('.' == *p || ':' == *p || '/' == *p)
		return false;

	while (*p)
	{
		if(!rfc1459::is_host(*p))
			return false;
                if(*p == '.' || *p == ':')
  			found_sep++;
		else if(*p == '/')
		{
			found_sep++;
			last_slash = p;
		}
		p++;
	}

	if(found_sep == 0)
		return false;

	if(last_slash && rfc1459::is_digit(last_slash[1]))
		return false;

	return true;
}

/*
 * valid_username - check username for validity
 *
 * Inputs       - pointer to user
 * Output       - true if valid, false if not
 * Side effects - NONE
 *
 * Absolutely always reject any '*' '!' '?' '@' in an user name
 * reject any odd control characters names.
 * Allow '.' in username to allow for "first.last"
 * style of username
 */
bool
valid_username(const char *username)
{
	int dots = 0;
	const char *p = username;

	s_assert(NULL != p);

	if(username == NULL)
		return false;

	if('~' == *p)
		++p;

	/* reject usernames that don't start with an alphanum
	 * i.e. reject jokers who have '-@somehost' or '.@somehost'
	 * or "-hi-@somehost", "h-----@somehost" would still be accepted.
	 */
	if(!rfc1459::is_alnum(*p))
		return false;

	while (*++p)
	{
		if((*p == '.') && ConfigFileEntry.dots_in_ident)
		{
			dots++;
			if(dots > ConfigFileEntry.dots_in_ident)
				return false;
			if(!rfc1459::is_user(p[1]))
				return false;
		}
		else if(!rfc1459::is_user(*p))
			return false;
	}
	return true;
}

/* report_and_set_user_flags
 *
 * Inputs       - pointer to source_p
 *              - pointer to aconf for this user
 * Output       - NONE
 * Side effects -
 * Report to user any special flags they are getting, and set them.
 */

static void
report_and_set_user_flags(client::client *source_p, struct ConfItem *aconf)
{
	/* If this user is being spoofed, tell them so */
	if(IsConfDoSpoofIp(aconf))
	{
		sendto_one_notice(source_p, ":*** Spoofing your IP");
	}

	/* If this user is in the exception class, Set it "E lined" */
	if(IsConfExemptKline(aconf))
	{
		set_exempt_kline(*source_p);
		sendto_one_notice(source_p, ":*** You are exempt from K/X lines");
	}

	if(IsConfExemptDNSBL(aconf))
		/* kline exempt implies this, don't send both */
		if(!IsConfExemptKline(aconf))
			sendto_one_notice(source_p, ":*** You are exempt from DNS blacklists");

	/* If this user is exempt from user limits set it F lined" */
	if(IsConfExemptLimits(aconf))
	{
		sendto_one_notice(source_p, ":*** You are exempt from user limits");
	}

	if(IsConfExemptFlood(aconf))
	{
		set_exempt_flood(*source_p);
		sendto_one_notice(source_p, ":*** You are exempt from flood limits");
	}

	if(IsConfExemptSpambot(aconf))
	{
		set_exempt_spambot(*source_p);
		sendto_one_notice(source_p, ":*** You are exempt from spambot checks");
	}

	if(IsConfExemptJupe(aconf))
	{
		set_exempt_jupe(*source_p);
		sendto_one_notice(source_p, ":*** You are exempt from juped channel warnings");
	}

	if(IsConfExemptResv(aconf))
	{
		set_exempt_resv(*source_p);
		sendto_one_notice(source_p, ":*** You are exempt from resvs");
	}

	if(IsConfExemptShide(aconf))
	{
		set_exempt_shide(*source_p);
		sendto_one_notice(source_p, ":*** You are exempt from serverhiding");
	}

	if(IsConfExtendChans(aconf))
	{
		set_extend_chans(*source_p);
		sendto_one_notice(source_p, ":*** You are exempt from normal channel limits");
	}
}

static void
show_other_user_mode(client::client &source,
                     client::client &target)
{
	char ubuf[128];
	mask(umode::table, target.mode, ubuf);

	if(my_connect(target) && target.snomask)
	{
		char sbuf[128];
		mask(sno::table, target.snomask, sbuf);
		sendto_one_notice(&source, ":Modes for %s are %s %s",
		                  target.name,
		                  ubuf,
		                  sbuf);
	}
	else
		sendto_one_notice(&source, ":Modes for %s are %s",
		                  target.name,
		                  ubuf);
}

/*
 * user_mode - set get current users mode
 *
 * m_umode() added 15/10/91 By Darren Reed.
 * parv[1] - username to change mode for
 * parv[2] - modes to change
 */
int
user_mode(client::client *client_p, client::client *source_p, int parc, const char *parv[])
{
	int flag;
	int i;
	char *m;
	const char *pm;
	client::client *target_p;
	int what, setflags;
	bool badflag = false;	/* Only send one bad flag notice */
	bool showsnomask = false;
	unsigned int setsnomask;
	char buf[BUFSIZE];
	hook_data_umode_changed hdata;

	what = MODE_ADD;

	if(parc < 2)
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS), me.name, source_p->name, "MODE");
		return 0;
	}

	if((target_p = my(*source_p) ? client::find_named_person(parv[1]) : client::find_person(parv[1])) == NULL)
	{
		if(my_connect(*source_p))
			sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL,
					   form_str(ERR_NOSUCHCHANNEL), parv[1]);
		return 0;
	}

	/* Dont know why these were commented out..
	 * put them back using new sendto() funcs
	 */

	if(is_server(*source_p))
	{
		sendto_realops_snomask(sno::GENERAL, L_ADMIN,
				     "*** Mode for User %s from %s", parv[1], source_p->name);
		return 0;
	}

	if(source_p != target_p)
	{
		if (my_oper(*source_p) && parc < 3)
			show_other_user_mode(*source_p, *target_p);
		else
			sendto_one(source_p, form_str(ERR_USERSDONTMATCH), me.name, source_p->name);
		return 0;
	}

	if(parc < 3)
	{
		mask(umode::table, source_p->mode, buf);
		sendto_one_numeric(source_p, RPL_UMODEIS, form_str(RPL_UMODEIS), buf);

		if(source_p->snomask)
		{
			char buf[128];
			mask(sno::table, source_p->snomask, buf);
			sendto_one_numeric(source_p, RPL_SNOMASK, form_str(RPL_SNOMASK), buf);
		}

		return 0;
	}

	/* find flags already set for user */
	setflags = source_p->mode;
	setsnomask = source_p->snomask;

	/*
	 * parse mode change string(s)
	 */
	for (pm = parv[2]; *pm; pm++)
		switch (*pm)
		{
		case '+':
			what = MODE_ADD;
			break;
		case '-':
			what = MODE_DEL;
			break;

		case 'o':
			if(what == MODE_ADD)
			{
				if(is_server(*client_p) && !is_oper(*source_p))
				{
					++Count.oper;
					set_oper(*source_p);
					rb_dlinkAddAlloc(source_p, &oper_list);
				}
			}
			else
			{
				/* Only decrement the oper counts if an oper to begin with
				 * found by Pat Szuta, Perly , perly@xnet.com
				 */

				if(!is_oper(*source_p))
					break;

				clear_oper(*source_p);

				Count.oper--;

				if(my_connect(*source_p))
				{
					clear(*source_p, ConfigFileEntry.oper_only_umodes);
					if (!is(*source_p, umode::SERVNOTICE) && source_p->snomask != 0)
					{
						source_p->snomask = 0;
						showsnomask = true;
					}
					source_p->flags &= ~OPER_FLAGS;

					rb_free(source_p->localClient->opername);
					source_p->localClient->opername = NULL;

					rb_dlinkFindDestroy(source_p, &local_oper_list);
					privilegeset_unref(source_p->localClient->privset);
					source_p->localClient->privset = NULL;
				}

				rb_dlinkFindDestroy(source_p, &oper_list);
			}
			break;

			/* we may not get these,
			 * but they shouldnt be in default
			 */

		/* can only be set on burst */
		case 'S':
		case 'Z':
		case ' ':
		case '\n':
		case '\r':
		case '\t':
			break;

		case 's':
			if (my_connect(*source_p))
			{
				if(!is_oper(*source_p)
						&& (ConfigFileEntry.oper_only_umodes & umode::SERVNOTICE))
				{
					if (what == MODE_ADD || is(*source_p, umode::SERVNOTICE))
						badflag = true;
					continue;
				}
				showsnomask = true;
				if(what == MODE_ADD)
				{
					if (parc > 3)
						source_p->snomask = delta(sno::table, source_p->snomask, parv[3]);
					else
						source_p->snomask |= sno::GENERAL;
				}
				else
					source_p->snomask = 0;
				if (source_p->snomask != 0)
					set(*source_p, umode::SERVNOTICE);
				else
					clear(*source_p, umode::SERVNOTICE);
				break;
			}
			/* FALLTHROUGH */
		default:
			if (my_connect(*source_p) && *pm == 'Q' && !ConfigChannel.use_forward)
			{
				badflag = true;
				break;
			}

			if((flag = umode::table[(unsigned char) *pm]))
			{
				if(my_connect(*source_p)
						&& ((!is_oper(*source_p)
							&& (ConfigFileEntry.oper_only_umodes & flag))))
				{
					if (what == MODE_ADD || source_p->mode & flag)
						badflag = true;
				}
				else
				{
					if(what == MODE_ADD)
						set(*source_p, flag);
					else
						clear(*source_p, flag);
				}
			}
			else
			{
				if(my_connect(*source_p))
					badflag = true;
			}
			break;
		}

	if(badflag)
		sendto_one(source_p, form_str(ERR_UMODEUNKNOWNFLAG), me.name, source_p->name);

	if(my(*source_p) && (source_p->snomask & sno::NCHANGE) && !IsOperN(source_p))
	{
		sendto_one_notice(source_p, ":*** You need oper and nick_changes flag for +s +n");
		source_p->snomask &= ~sno::NCHANGE;	/* only tcm's really need this */
	}

	if(my(*source_p) && is(*source_p, umode::OPERWALL) && !IsOperOperwall(source_p))
	{
		sendto_one_notice(source_p, ":*** You need oper and operwall flag for +z");
		source_p->mode &= ~umode::OPERWALL;
	}

	if(my_connect(*source_p) && (source_p->mode & umode::ADMIN) &&
	   (!IsOperAdmin(source_p) || IsOperHiddenAdmin(source_p)))
	{
		sendto_one_notice(source_p, ":*** You need oper and admin flag for +a");
		clear(*source_p, umode::ADMIN);
	}

	/* let modules providing usermodes know that we've changed our usermode --nenolod */
	hdata.client = source_p;
	hdata.oldumodes = setflags;
	hdata.oldsnomask = setsnomask;
	call_hook(h_umode_changed, &hdata);

	if(!(setflags & umode::INVISIBLE) && is(*source_p, umode::INVISIBLE))
		++Count.invisi;
	if((setflags & umode::INVISIBLE) && !is(*source_p, umode::INVISIBLE))
		--Count.invisi;
	/*
	 * compare new flags with old flags and send string which
	 * will cause servers to update correctly.
	 */
	send_umode_out(*client_p, *source_p, setflags);
	if (showsnomask && my_connect(*source_p))
	{
		char snobuf[128];
		sendto_one_numeric(source_p, RPL_SNOMASK, form_str(RPL_SNOMASK),
		                   mask(sno::table, source_p->snomask, snobuf));
	}

	return (0);
}

/*
 * send the MODE string for user (user) to connection client_p
 * -avalon
 */
void
send_umode(client::client &to,
           const client::client &source,
           const char *const &buf)
{
	sendto_one(&to, ":%s MODE %s :%s",
	           source.name,
	           source.name,
	           buf);
}

void
send_umode(client::client &to,
           const client::client &after,
           const umode::mask &before)
{
	char buf[128];
	delta(umode::table, before, after.mode, buf);
	send_umode(to, after, buf);
}

/*
 * send_umode_out
 *
 * inputs	-
 * output	- NONE
 * side effects -
 */
void
send_umode_out(client::client &client,
               const client::client &source,
               const umode::mask &before)
{
	char buf[BUFSIZE];
	delta(umode::table, before, source.mode, buf);

	rb_dlink_node *ptr;
	RB_DLINK_FOREACH(ptr, serv_list.head)
	{
		auto &target(*reinterpret_cast<client::client *>(ptr->data));
		if((target != client) && (target != source))
			sendto_one(&target, ":%s MODE %s :%s",
			           get_id(source, target),
			           get_id(source, target),
			           buf);
	}

	if(my(client))
		send_umode(client, source, buf);
}

/*
 * user_welcome
 *
 * inputs	- client pointer to client to welcome
 * output	- NONE
 * side effects	-
 */
void
user_welcome(client::client *source_p)
{
	sendto_one_numeric(source_p, RPL_WELCOME, form_str(RPL_WELCOME), ServerInfo.network_name, source_p->name);
	sendto_one_numeric(source_p, RPL_YOURHOST, form_str(RPL_YOURHOST),
		   get_listener_name(source_p->localClient->listener), info::version.c_str());
	sendto_one_numeric(source_p, RPL_CREATED, form_str(RPL_CREATED), info::compiled.c_str());

	sendto_one_numeric(source_p, RPL_MYINFO, form_str(RPL_MYINFO),
	                   me.name,
	                   info::version.c_str(),
	                   client::mode::available,
	                   chan::mode::arity[0],
	                   chan::mode::arity[1]);

	supported::show(*source_p);

	show_lusers(source_p);

	if(ConfigFileEntry.short_motd)
	{
		sendto_one_notice(source_p, ":*** Notice -- motd was last changed at %s", cache::motd::user_motd_changed);
		sendto_one_notice(source_p, ":*** Notice -- Please read the motd if you haven't read it");

		sendto_one(source_p, form_str(RPL_MOTDSTART),
			   me.name, source_p->name, me.name);

		sendto_one(source_p, form_str(RPL_MOTD),
			   me.name, source_p->name, "*** This is the short motd ***");

		sendto_one(source_p, form_str(RPL_ENDOFMOTD), me.name, source_p->name);
	}
	else
		cache::motd::send_user(*source_p);
}

/* oper_up()
 *
 * inputs	- pointer to given client to oper
 *		- pointer to ConfItem to use
 * output	- none
 * side effects	- opers up source_p using aconf for reference
 */
void
oper_up(client::client *source_p, struct oper_conf *oper_p)
{
	unsigned int old = source_p->mode, oldsnomask = source_p->snomask;
	hook_data_umode_changed hdata;

	set_oper(*source_p);

	if(oper_p->umodes)
		source_p->mode |= oper_p->umodes;
	else if(ConfigFileEntry.oper_umodes)
		source_p->mode |= ConfigFileEntry.oper_umodes;
	else
		source_p->mode |= client::mode::DEFAULT_OPER_UMODES;

	if (oper_p->snomask)
	{
		source_p->snomask |= oper_p->snomask;
		source_p->mode |= umode::SERVNOTICE;
	}
	else if (source_p->mode & umode::SERVNOTICE)
	{
		/* Only apply these if +s is already set -- jilles */
		if (ConfigFileEntry.oper_snomask)
			source_p->snomask |= ConfigFileEntry.oper_snomask;
		else
			source_p->snomask |= sno::DEFAULT_OPER_SNOMASK;
	}

	Count.oper++;

	set_extend_chans(*source_p);
	set_exempt_kline(*source_p);

	source_p->flags |= oper_p->flags;
	source_p->localClient->opername = rb_strdup(oper_p->name);
	source_p->localClient->privset = privilegeset_ref(oper_p->privset);

	rb_dlinkAddAlloc(source_p, &local_oper_list);
	rb_dlinkAddAlloc(source_p, &oper_list);

	if(IsOperAdmin(source_p) && !IsOperHiddenAdmin(source_p))
		source_p->mode |= umode::ADMIN;
	if(!IsOperN(source_p))
		source_p->snomask &= ~sno::NCHANGE;
	if(!IsOperOperwall(source_p))
		source_p->mode &= ~umode::OPERWALL;
	hdata.client = source_p;
	hdata.oldumodes = old;
	hdata.oldsnomask = oldsnomask;
	call_hook(h_umode_changed, &hdata);

	sendto_realops_snomask(sno::GENERAL, L_ALL,
			     "%s (%s!%s@%s) is now an operator", oper_p->name, source_p->name,
			     source_p->username, source_p->host);
	if(!(old & umode::INVISIBLE) && is(*source_p, umode::INVISIBLE))
		++Count.invisi;
	if((old & umode::INVISIBLE) && !is(*source_p, umode::INVISIBLE))
		--Count.invisi;
	send_umode_out(*source_p, *source_p, old);

	char snobuf[128];
	sendto_one_numeric(source_p, RPL_SNOMASK, form_str(RPL_SNOMASK),
	                   mask(sno::table, source_p->snomask, snobuf));

	sendto_one(source_p, form_str(RPL_YOUREOPER), me.name, source_p->name);
	sendto_one_notice(source_p, ":*** Oper privilege set is %s", oper_p->privset->name);
	sendto_one_notice(source_p, ":*** Oper privs are %s", oper_p->privset->privs);
	cache::motd::send_oper(*source_p);
}

void
change_nick_user_host(client::client *target_p,	const char *nick, const char *username,
		      const char *host, int newts, const char *format, ...)
{
	rb_dlink_node *ptr;
	chan::chan *chptr;
	int changed = irccmp(target_p->name, nick);
	int changed_case = strcmp(target_p->name, nick);
	int do_qjm = irccmp(target_p->username, username) || irccmp(target_p->host, host);
	char mode[10], modeval[NICKLEN * 2 + 2], reason[256];
	va_list ap;

	modeval[0] = '\0';

	if(changed)
	{
		target_p->tsinfo = newts;
		monitor_signoff(target_p);
	}
	chan::invalidate_bancache_user(target_p);

	if(do_qjm)
	{
		va_start(ap, format);
		vsnprintf(reason, 255, format, ap);
		va_end(ap);

		sendto_common_channels_local_butone(target_p, NOCAPS, CLICAP_CHGHOST, ":%s!%s@%s QUIT :%s",
				target_p->name, target_p->username, target_p->host,
				reason);

		for(const auto &pit : chans(user(*target_p)))
		{
			auto &chan(*pit.first);
			auto &member(*pit.second);
			char *mptr(mode);

			if(is_chanop(member))
			{
				*mptr++ = 'o';
				strcat(modeval, nick);
				strcat(modeval, " ");
			}

			if(is_voiced(member))
			{
				*mptr++ = 'v';
				strcat(modeval, nick);
			}

			*mptr = '\0';

			sendto_channel_local_with_capability_butone(target_p, chan::ALL_MEMBERS, NOCAPS, CLICAP_EXTENDED_JOIN | CLICAP_CHGHOST, &chan,
								    ":%s!%s@%s JOIN %s", nick, username, host, chan.name.c_str());
			sendto_channel_local_with_capability_butone(target_p, chan::ALL_MEMBERS, CLICAP_EXTENDED_JOIN, CLICAP_CHGHOST, &chan,
								    ":%s!%s@%s JOIN %s %s :%s", nick, username, host, chan.name.c_str(),
								    suser(user(*target_p)).empty()? "*" : suser(user(*target_p)).c_str(),
								    target_p->info);

			if(*mode)
				sendto_channel_local_with_capability_butone(target_p, chan::ALL_MEMBERS, NOCAPS, CLICAP_CHGHOST, &chan,
						":%s MODE %s +%s %s", target_p->servptr->name, chan.name.c_str(), mode, modeval);

			*modeval = '\0';
		}

		/* Resend away message to away-notify enabled clients. */
		if (away(user(*target_p)).size())
			sendto_common_channels_local_butone(target_p, CLICAP_AWAY_NOTIFY, CLICAP_CHGHOST, ":%s!%s@%s AWAY :%s",
							    nick, username, host,
							    away(user(*target_p)).c_str());

		sendto_common_channels_local_butone(target_p, CLICAP_CHGHOST, NOCAPS,
						    ":%s!%s@%s CHGHOST %s %s",
						    target_p->name, target_p->username, target_p->host, username, host);

		if(my(*target_p) && changed_case)
			sendto_one(target_p, ":%s!%s@%s NICK %s",
					target_p->name, username, host, nick);

		/* TODO: send some snotes to sno::NCHANGE/sno::CCONN/sno::CCONNEXT? */
	}
	else if(changed_case)
	{
		sendto_common_channels_local(target_p, NOCAPS, NOCAPS, ":%s!%s@%s NICK :%s",
				target_p->name, username, host, nick);

		if(my_connect(*target_p))
			sendto_realops_snomask(sno::NCHANGE, L_ALL,
					"Nick change: From %s to %s [%s@%s]",
					target_p->name, nick,
					target_p->username, target_p->host);
	}

	if (username != target_p->username)
		rb_strlcpy(target_p->username, username, sizeof target_p->username);

	rb_strlcpy(target_p->host, host, sizeof target_p->host);

	if (changed)
		whowas::add(*target_p);

	del_from_client_hash(target_p->name, target_p);
	rb_strlcpy(target_p->name, nick, NICKLEN);
	add_to_client_hash(target_p->name, target_p);

	if(changed)
	{
		monitor_signon(target_p);
		del_all_accepts(target_p);
	}
}

}