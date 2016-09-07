/*
 *  ircd-ratbox: A slightly useful ircd.
 *  m_join.c: Joins a channel.
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

static const char join_desc[] = "Provides the JOIN and TS6 SJOIN commands to facilitate joining and creating channels";

static void m_join(struct MsgBuf *, client::client &, client::client &, int, const char **);
static void ms_join(struct MsgBuf *, client::client &, client::client &, int, const char **);
static void ms_sjoin(struct MsgBuf *, client::client &, client::client &, int, const char **);

static int h_can_create_channel;
static int h_channel_join;

struct Message join_msgtab = {
	"JOIN", 0, 0, 0, 0,
	{mg_unreg, {m_join, 2}, {ms_join, 2}, mg_ignore, mg_ignore, {m_join, 2}}
};

struct Message sjoin_msgtab = {
	"SJOIN", 0, 0, 0, 0,
	{mg_unreg, mg_ignore, mg_ignore, {ms_sjoin, 4}, mg_ignore, mg_ignore}
};

mapi_clist_av1 join_clist[] = { &join_msgtab, &sjoin_msgtab, NULL };

mapi_hlist_av1 join_hlist[] = {
	{ "can_create_channel", &h_can_create_channel },
	{ "channel_join", &h_channel_join },
	{ NULL, NULL },
};

DECLARE_MODULE_AV2(join, NULL, NULL, join_clist, join_hlist, NULL, NULL, NULL, join_desc);

static void do_join_0(client::client &client, client::client &source);
static bool check_channel_name_loc(client::client &source, const char *name);
static void send_join_error(client::client &source, int numeric, const char *name);

static void set_final_mode(chan::modes *mode, chan::modes *oldmode);
static void remove_our_modes(chan::chan *chptr, client::client &source);
static void remove_ban_list(chan::chan &chan, client::client &source, chan::list &list, const char &c, const int &mems);

static char modebuf[chan::mode::BUFLEN];
static char parabuf[chan::mode::BUFLEN];
static const char *para[chan::mode::MAXPARAMS];
static char *mbuf;
static int pargs;

/* Check what we will forward to, without sending any notices to the user
 * -- jilles
 */
static chan::chan *
check_forward(client::client &source, chan::chan *chptr,
	     char *key, int *err)
{
	int depth = 0, i;
	const char *next = NULL;

	/* The caller (m_join) is only interested in the reason
	 * for the original channel.
	 */
	if ((*err = can_join(&source, chptr, key, &next)) == 0)
		return chptr;

	/* User is +Q, or forwarding disabled */
	if (is(source, umode::NOFORWARD) || !ConfigChannel.use_forward)
		return NULL;

	while (depth < 16)
	{
		if (next == NULL)
			return NULL;

		chptr = chan::get(next, std::nothrow);
		/* Can only forward to existing channels */
		if (chptr == NULL)
			return NULL;
		/* Already on there... but don't send the original reason for
		 * being unable to join. It isn't their fault they're already
		 * on the channel, and it looks hostile otherwise.
		 * --Elizafox
		 */
		if (is_member(chptr, &source))
		{
			*err = ERR_USERONCHANNEL; /* I'm borrowing this for now. --Elizafox */
			return NULL;
		}
		/* Juped. Sending a warning notice would be unfair */
		if (hash_find_resv(chptr->name.c_str()))
			return NULL;
		/* Don't forward to +Q channel */
		if (chptr->mode.mode & chan::mode::DISFORWARD)
			return NULL;

		i = can_join(&source, chptr, key, &next);
		if (i == 0)
			return chptr;
		depth++;
	}

	return NULL;
}

/*
 * m_join
 *      parv[1] = channel
 *      parv[2] = channel password (key)
 */
static void
m_join(struct MsgBuf *msgbuf_p, client::client &client, client::client &source, int parc, const char *parv[])
{
	static char jbuf[BUFSIZE];
	chan::chan *chptr = NULL, *chptr2 = NULL;
	struct ConfItem *aconf;
	char *name;
	char *key = NULL;
	const char *modes;
	int i, flags = 0;
	char *p = NULL, *p2 = NULL;
	char *chanlist;
	char *mykey;

	jbuf[0] = '\0';

	/* rebuild the list of channels theyre supposed to be joining.
	 * this code has a side effect of losing keys, but..
	 */
	chanlist = LOCAL_COPY(parv[1]);
	for(name = rb_strtok_r(chanlist, ",", &p); name; name = rb_strtok_r(NULL, ",", &p))
	{
		/* check the length and name of channel is ok */
		if(!check_channel_name_loc(source, name) || (strlen(name) > LOC_CHANNELLEN))
		{
			sendto_one_numeric(&source, ERR_BADCHANNAME,
					   form_str(ERR_BADCHANNAME), (unsigned char *) name);
			continue;
		}

		/* join 0 parts all channels */
		if(*name == '0' && (name[1] == ',' || name[1] == '\0') && name == chanlist)
		{
			rb_strlcpy(jbuf, "0", sizeof(jbuf));
			continue;
		}

		/* check it begins with # or &, and local chans are disabled */
		else if(!chan::has_prefix(name) ||
			( ConfigChannel.disable_local_channels && name[0] == '&'))
		{
			sendto_one_numeric(&source, ERR_NOSUCHCHANNEL,
					   form_str(ERR_NOSUCHCHANNEL), name);
			continue;
		}

		/* see if its resv'd */
		if(!is_exempt_resv(source) && (aconf = hash_find_resv(name)))
		{
			sendto_one_numeric(&source, ERR_BADCHANNAME,
					   form_str(ERR_BADCHANNAME), name);

			/* dont warn for opers */
			if(!is_exempt_jupe(source) && !is(source, umode::OPER))
				sendto_realops_snomask(sno::SPY, L_NETWIDE,
						     "User %s (%s@%s) is attempting to join locally juped channel %s (%s)",
						     source.name, source.username,
						     source.orighost, name, aconf->passwd);
			/* dont update tracking for jupe exempt users, these
			 * are likely to be spamtrap leaves
			 */
			else if(is_exempt_jupe(source))
				aconf->port--;

			continue;
		}

		if(splitmode && !is(source, umode::OPER) && (*name != '&') &&
		   ConfigChannel.no_join_on_split)
		{
			sendto_one(&source, form_str(ERR_UNAVAILRESOURCE),
				   me.name, source.name, name);
			continue;
		}

		if(*jbuf)
			(void) strcat(jbuf, ",");
		(void) rb_strlcat(jbuf, name, sizeof(jbuf));
	}

	if(parc > 2)
	{
		mykey = LOCAL_COPY(parv[2]);
		key = rb_strtok_r(mykey, ",", &p2);
	}

	for(name = rb_strtok_r(jbuf, ",", &p); name;
	    key = (key) ? rb_strtok_r(NULL, ",", &p2) : NULL, name = rb_strtok_r(NULL, ",", &p))
	{
		hook_data_channel_activity hook_info;

		/* JOIN 0 simply parts all channels the user is in */
		if(*name == '0' && !atoi(name))
		{
			if(chans(user(source)).empty())
				continue;

			do_join_0(me, source);
			continue;
		}

		/* look for the channel */
		if((chptr = chan::get(name, std::nothrow)) != NULL)
		{
			if(is_member(chptr, &source))
				continue;

			flags = 0;
		}
		else
		{
			hook_data_client_approval moduledata;

			moduledata.client = &source;
			moduledata.approved = 0;

			call_hook(h_can_create_channel, &moduledata);

			if(moduledata.approved != 0)
			{
				if(moduledata.approved != chan::mode::ERR_CUSTOM)
					send_join_error(source,
							moduledata.approved,
							name);
				continue;
			}

			if(splitmode && !is(source, umode::OPER) && (*name != '&') &&
			   ConfigChannel.no_create_on_split)
			{
				sendto_one(&source, form_str(ERR_UNAVAILRESOURCE),
					   me.name, source.name, name);
				continue;
			}

			flags = chan::CHANOP;
		}

		if(((chans(user(source)).size()) >=
		    (unsigned long) ConfigChannel.max_chans_per_user) &&
		   (!is_extend_chans(source) ||
		    (chans(user(source)).size() >=
		     (unsigned long) ConfigChannel.max_chans_per_user_large)))
		{
			sendto_one(&source, form_str(ERR_TOOMANYCHANNELS),
				   me.name, source.name, name);
			continue;
		}

		if(chptr == NULL) try  // If I already have a chptr, no point doing this
		{
			chptr = &chan::add(name, source);
		}
		catch(const chan::error &e)
		{
			sendto_one(&source, form_str(ERR_UNAVAILRESOURCE),
			           me.name,
			           source.name,
			           name);
			continue;
		}

		/* If check_forward returns NULL, they couldn't join and there wasn't a usable forward channel. */
		if((chptr2 = check_forward(source, chptr, key, &i)) == NULL)
		{
			/* might be wrong, but is there any other better location for such?
			 * see extensions/chm_operonly.c for other comments on this
			 * -- dwr
			 */
			if(i != chan::mode::ERR_CUSTOM)
				send_join_error(source, i, name);
			continue;
		}
		else if(chptr != chptr2)
			sendto_one_numeric(&source, ERR_LINKCHANNEL, form_str(ERR_LINKCHANNEL), name, chptr2->name.c_str());

		chptr = chptr2;

		if(flags == 0 &&
				!is(source, umode::OPER) && !is_exempt_spambot(source))
			chan::check_spambot_warning(&source, name);

		/* add the user to the channel */
		add(*chptr, source, flags);
		if (chptr->mode.join_num &&
			rb_current_time() - chptr->join_delta >= chptr->mode.join_time)
		{
			chptr->join_count = 0;
			chptr->join_delta = rb_current_time();
		}
		chptr->join_count++;

		/* credit user for join */
		credit_client_join(&source);

		/* we send the user their join here, because we could have to
		 * send a mode out next.
		 */
		chan::send_join(*chptr, source);

		/* its a new channel, set +nt and burst. */
		if(flags & chan::CHANOP)
		{
			chptr->channelts = rb_current_time();
			chptr->mode.mode |= ConfigChannel.autochanmodes;
			modes = channel_modes(chptr, &me);

			sendto_channel_local(chan::ONLY_CHANOPS, chptr, ":%s MODE %s %s",
					     me.name, chptr->name.c_str(), modes);

			sendto_server(&client, chptr, CAP_TS6, NOCAPS,
				      ":%s SJOIN %ld %s %s :@%s",
				      me.id, (long) chptr->channelts,
				      chptr->name.c_str(), modes, source.id);
		}
		else
		{
			sendto_server(&client, chptr, CAP_TS6, NOCAPS,
				      ":%s JOIN %ld %s +",
				      use_id(&source), (long) chptr->channelts,
				      chptr->name.c_str());
		}

		del_invite(*chptr, source);

		if(chptr->topic)
		{
			sendto_one(&source, form_str(RPL_TOPIC),
			           me.name,
			           source.name,
			           chptr->name.c_str(),
			           chptr->topic.text.c_str());

			sendto_one(&source, form_str(RPL_TOPICWHOTIME),
			           me.name,
			           source.name,
			           chptr->name.c_str(),
			           chptr->topic.info.c_str(),
			           ulong(chptr->topic.time));
		}

		channel_member_names(chptr, &source, 1);

		hook_info.client = &source;
		hook_info.chptr = chptr;
		hook_info.key = key;
		call_hook(h_channel_join, &hook_info);
	}
}

/*
 * ms_join
 *      parv[1] = channel TS
 *      parv[2] = channel
 *      parv[3] = "+", formerly channel modes but now unused
 * alternatively, a single "0" parameter parts all channels
 */
static void
ms_join(struct MsgBuf *msgbuf_p, client::client &client, client::client &source, int parc, const char *parv[])
{
	chan::chan *chptr;
	static chan::modes mode;
	time_t oldts;
	time_t newts;
	bool isnew;
	bool keep_our_modes = true;
	rb_dlink_node *ptr, *next_ptr;

	/* special case for join 0 */
	if((parv[1][0] == '0') && (parv[1][1] == '\0') && parc == 2)
	{
		do_join_0(client, source);
		return;
	}

	if(parc < 4)
		return;

	if(!chan::has_prefix(parv[2]) || !chan::valid_name(parv[2]))
		return;

	/* joins for local channels cant happen. */
	if(parv[2][0] == '&')
		return;

	mbuf = modebuf;
	mode.key[0] = mode.forward[0] = '\0';
	mode.mode = mode.limit = mode.join_num = mode.join_time = 0;

	try
	{
		chptr = &chan::add(parv[2], source);
		isnew = size(*chptr) == 0;
	}
	catch(chan::error &e)
	{
		return;
	}

	newts = atol(parv[1]);
	oldts = chptr->channelts;

	/* making a channel TS0 */
	if(!isnew && !newts && oldts)
	{
		sendto_channel_local(chan::ALL_MEMBERS, chptr,
				     ":%s NOTICE %s :*** Notice -- TS for %s changed from %ld to 0",
				     me.name, chptr->name.c_str(), chptr->name.c_str(), (long) oldts);
		sendto_realops_snomask(sno::GENERAL, L_ALL,
				     "Server %s changing TS on %s from %ld to 0",
				     source.name, chptr->name.c_str(), (long) oldts);
	}

	if(isnew)
		chptr->channelts = newts;
	else if(newts == 0 || oldts == 0)
		chptr->channelts = 0;
	else if(newts == oldts)
		;
	else if(newts < oldts)
	{
		keep_our_modes = false;
		chptr->channelts = newts;
	}

	/* Lost the TS, other side wins, so remove modes on this side */
	if(!keep_our_modes)
	{
		set_final_mode(&mode, &chptr->mode);
		chptr->mode = mode;
		remove_our_modes(chptr, source);
		clear_invites(*chptr);

		/* If setting -j, clear join throttle state -- jilles */
		chptr->join_count = chptr->join_delta = 0;
		sendto_channel_local(chan::ALL_MEMBERS, chptr,
		                     ":%s NOTICE %s :*** Notice -- TS for %s changed from %ld to %ld",
		                     me.name,
		                     chptr->name.c_str(),
		                     chptr->name.c_str(),
		                     long(oldts),
		                     long(newts));

		/* Update capitalization in channel name, this makes the
		 * capitalization timestamped like modes are -- jilles */
		chptr->name = parv[2];

		if(*modebuf != '\0')
			sendto_channel_local(chan::ALL_MEMBERS, chptr,
			                     ":%s MODE %s %s %s",
			                     source.servptr->name,
			                     chptr->name.c_str(),
			                     modebuf,
			                     parabuf);

		*modebuf = *parabuf = '\0';

		/* since we're dropping our modes, we want to clear the mlock as well. --nenolod */
		set_channel_mlock(&client, &source, chptr, NULL, false);
	}

	if(!is_member(chptr, &source))
	{
		add(*chptr, source, chan::PEON);
		if (chptr->mode.join_num &&
			rb_current_time() - chptr->join_delta >= chptr->mode.join_time)
		{
			chptr->join_count = 0;
			chptr->join_delta = rb_current_time();
		}
		chptr->join_count++;
		send_join(*chptr, source);
	}

	sendto_server(&client, chptr, CAP_TS6, NOCAPS,
		      ":%s JOIN %ld %s +",
		      source.id, (long) chptr->channelts, chptr->name.c_str());
}

static void
ms_sjoin(struct MsgBuf *msgbuf_p, client::client &client, client::client &source, int parc, const char *parv[])
{
	static char buf_uid[BUFSIZE];
	static const char empty_modes[] = "0";
	chan::chan *chptr;
	client::client *target_p, *fakesource;
	time_t newts;
	time_t oldts;
	static chan::modes mode, *oldmode;
	const char *modes;
	int args = 0;
	bool keep_our_modes = true;
	bool keep_new_modes = true;
	int fl;
	bool isnew;
	int mlen_uid;
	int len_uid;
	int len;
	int joins = 0;
	const char *s;
	char *ptr_uid;
	char *p;
	int i, joinc = 0, timeslice = 0;
	static char empty[] = "";
	rb_dlink_node *ptr, *next_ptr;

	if(parc < 5)
		return;

	if(!chan::has_prefix(parv[2]) || !chan::valid_name(parv[2]))
		return;

	/* SJOIN's for local channels can't happen. */
	if(*parv[2] == '&')
		return;

	modebuf[0] = parabuf[0] = mode.key[0] = mode.forward[0] = '\0';
	pargs = mode.mode = mode.limit = mode.join_num = mode.join_time = 0;

	/* Hide connecting server on netburst -- jilles */
	if (ConfigServerHide.flatten_links && !has_sent_eob(source))
		fakesource = &me;
	else
		fakesource = &source;

	mbuf = modebuf;
	newts = atol(parv[1]);

	s = parv[3];
	while (*s)
	{
		switch (*(s++))
		{
		case 'f':
			rb_strlcpy(mode.forward, parv[4 + args], sizeof(mode.forward));
			args++;
			if(parc < 5 + args)
				return;
			break;
		case 'j':
			sscanf(parv[4 + args], "%d:%d", &joinc, &timeslice);
			args++;
			mode.join_num = joinc;
			mode.join_time = timeslice;
			if(parc < 5 + args)
				return;
			break;
		case 'k':
			rb_strlcpy(mode.key, parv[4 + args], sizeof(mode.key));
			args++;
			if(parc < 5 + args)
				return;
			break;
		case 'l':
			mode.limit = atoi(parv[4 + args]);
			args++;
			if(parc < 5 + args)
				return;
			break;
		default:
			if(chan::mode::table[uint8_t(*s)].type != 0)
				mode.mode |= chan::mode::table[uint8_t(*s)].type;
		}
	}

	if(parv[args + 4])
	{
		s = parv[args + 4];

		/* remove any leading spaces */
		while (*s == ' ')
			s++;
	}
	else
		s = "";

	try
	{
		chptr = &chan::add(parv[2], source);
		isnew = size(*chptr) == 0;
	}
	catch(chan::error &e)
	{
		return;	/* channel name too long? */
	}

	oldts = chptr->channelts;
	oldmode = &chptr->mode;

	if(!isnew && !newts && oldts)
	{
		sendto_channel_local(chan::ALL_MEMBERS, chptr,
				     ":%s NOTICE %s :*** Notice -- TS for %s "
				     "changed from %ld to 0",
				     me.name, chptr->name.c_str(), chptr->name.c_str(), (long) oldts);
		sendto_realops_snomask(sno::GENERAL, L_ALL,
				     "Server %s changing TS on %s from %ld to 0",
				     source.name, chptr->name.c_str(), (long) oldts);
	}

	if(isnew)
		chptr->channelts = newts;

	else if(newts == 0 || oldts == 0)
		chptr->channelts = 0;
	else if(newts == oldts)
		;
	else if(newts < oldts)
	{
		/* If configured, kick people trying to join +i/+k
		 * channels by recreating them on split servers.
		 * If the source has sent EOB, assume this is some
		 * sort of hack by services. If cmode +i is set,
		 * services can send kicks if needed; if the key
		 * differs, services cannot kick in a race-free
		 * manner so do so here.
		 * -- jilles */
		if (ConfigChannel.kick_on_split_riding &&
				((!has_sent_eob(source) &&
				mode.mode & chan::mode::INVITEONLY) ||
		    (mode.key[0] != 0 && irccmp(mode.key, oldmode->key) != 0)))
		{
			chan::membership *msptr;
			client::client *who;
			int l = size(chptr->members);

			for(const auto &msptr : chptr->members.local)
			{
				who = &get_client(*msptr);
				sendto_one(who, ":%s KICK %s %s :Net Rider",
						     me.name, chptr->name.c_str(), who->name);

				sendto_server(NULL, chptr, CAP_TS6, NOCAPS,
					      ":%s KICK %s %s :Net Rider",
					      me.id, chptr->name.c_str(),
					      who->id);
				del(*chptr, *who);
				if (--l == 0)
					break;
			}
			if (l == 0)
			{
				/* Channel was emptied, create a new one */
				try
				{
					chptr = &chan::add(parv[2], source);
					isnew = size(*chptr) == 0;
				}
				catch(chan::error &e)
				{
					return;		/* oops! */
				}

				oldmode = &chptr->mode;
			}
		}
		keep_our_modes = false;
		chptr->channelts = newts;
	}
	else
		keep_new_modes = false;

	if(!keep_new_modes)
		mode = *oldmode;
	else if(keep_our_modes)
	{
		mode.mode |= oldmode->mode;
		if(oldmode->limit > mode.limit)
			mode.limit = oldmode->limit;
		if(strcmp(mode.key, oldmode->key) < 0)
			rb_strlcpy(mode.key, oldmode->key, sizeof(mode.key));
		if(oldmode->join_num > mode.join_num ||
				(oldmode->join_num == mode.join_num &&
				 oldmode->join_time > mode.join_time))
		{
			mode.join_num = oldmode->join_num;
			mode.join_time = oldmode->join_time;
		}
		if(irccmp(mode.forward, oldmode->forward) < 0)
			rb_strlcpy(mode.forward, oldmode->forward, sizeof(mode.forward));
	}
	else
	{
		/* If setting -j, clear join throttle state -- jilles */
		if (!mode.join_num)
			chptr->join_count = chptr->join_delta = 0;
	}

	set_final_mode(&mode, oldmode);
	chptr->mode = mode;

	/* Lost the TS, other side wins, so remove modes on this side */
	if(!keep_our_modes)
	{
		namespace mode = chan::mode;
		using chan::empty;

		remove_our_modes(chptr, *fakesource);
		clear_invites(*chptr);

		if (!empty(*chptr, chan::mode::BAN))
			remove_ban_list(*chptr, *fakesource, get(*chptr, mode::BAN), 'b', chan::ALL_MEMBERS);

		if (!empty(*chptr, mode::EXCEPTION))
			remove_ban_list(*chptr, *fakesource, get(*chptr, mode::EXCEPTION), 'e', chan::ONLY_CHANOPS);

		if (!empty(*chptr, mode::INVEX))
			remove_ban_list(*chptr, *fakesource, get(*chptr, mode::INVEX), 'I', chan::ONLY_CHANOPS);

		if (!empty(*chptr, mode::QUIET))
			remove_ban_list(*chptr, *fakesource, get(*chptr, mode::QUIET), 'q', chan::ALL_MEMBERS);

		chptr->bants++;

		sendto_channel_local(chan::ALL_MEMBERS, chptr,
				     ":%s NOTICE %s :*** Notice -- TS for %s changed from %ld to %ld",
				     me.name, chptr->name.c_str(), chptr->name.c_str(),
				     (long) oldts, (long) newts);
		/* Update capitalization in channel name, this makes the
		 * capitalization timestamped like modes are -- jilles */
		chptr->name = parv[2];

		/* since we're dropping our modes, we want to clear the mlock as well. --nenolod */
		set_channel_mlock(&client, &source, chptr, NULL, false);
	}

	if(*modebuf != '\0')
		sendto_channel_local(chan::ALL_MEMBERS, chptr, ":%s MODE %s %s %s",
				     fakesource->name, chptr->name.c_str(), modebuf, parabuf);

	*modebuf = *parabuf = '\0';

	if(parv[3][0] != '0' && keep_new_modes)
		modes = channel_modes(chptr, &source);
	else
		modes = empty_modes;

	mlen_uid = sprintf(buf_uid, ":%s SJOIN %ld %s %s :",
			      use_id(&source), (long) chptr->channelts, parv[2], modes);
	ptr_uid = buf_uid + mlen_uid;

	mbuf = modebuf;
	para[0] = para[1] = para[2] = para[3] = empty;
	pargs = 0;
	len_uid = 0;

	/* if theres a space, theres going to be more than one nick, change the
	 * first space to \0, so s is just the first nick, and point p to the
	 * second nick
	 */
	if((p = (char *)strchr(s, ' ')) != NULL)
	{
		*p++ = '\0';
	}

	*mbuf++ = '+';

	while (s)
	{
		fl = 0;

		for (i = 0; i < 2; i++)
		{
			if(*s == '@')
			{
				fl |= chan::CHANOP;
				s++;
			}
			else if(*s == '+')
			{
				fl |= chan::VOICE;
				s++;
			}
		}

		/* if the client doesnt exist or is fake direction, skip. */
		if(!(target_p = find_client(s)) ||
		   (target_p->from != &client) || !is_person(*target_p))
			goto nextnick;

		/* we assume for these we can fit at least one nick/uid in.. */

		/* check we can fit another status+nick+space into a buffer */
		if((mlen_uid + len_uid + client::IDLEN + 3) > (BUFSIZE - 3))
		{
			*(ptr_uid - 1) = '\0';
			sendto_server(client.from, NULL, CAP_TS6, NOCAPS, "%s", buf_uid);
			ptr_uid = buf_uid + mlen_uid;
			len_uid = 0;
		}

		if(keep_new_modes)
		{
			if(fl & chan::CHANOP)
			{
				*ptr_uid++ = '@';
				len_uid++;
			}
			if(fl & chan::VOICE)
			{
				*ptr_uid++ = '+';
				len_uid++;
			}
		}

		/* copy the nick to the two buffers */
		len = sprintf(ptr_uid, "%s ", use_id(target_p));
		ptr_uid += len;
		len_uid += len;

		if(!keep_new_modes)
			fl = 0;

		if(!is_member(chptr, target_p))
		{
			add(*chptr, *target_p, fl);
			send_join(*chptr, *target_p);
			joins++;
		}

		if(fl & chan::CHANOP)
		{
			*mbuf++ = 'o';
			para[pargs++] = target_p->name;

			/* a +ov user.. bleh */
			if(fl & chan::VOICE)
			{
				/* its possible the +o has filled up chan::mode::MAXPARAMS, if so, start
				 * a new buffer
				 */
				if(pargs >= chan::mode::MAXPARAMS)
				{
					*mbuf = '\0';
					sendto_channel_local(chan::ALL_MEMBERS, chptr,
							     ":%s MODE %s %s %s %s %s %s",
							     fakesource->name, chptr->name.c_str(),
							     modebuf,
							     para[0], para[1], para[2], para[3]);
					mbuf = modebuf;
					*mbuf++ = '+';
					para[0] = para[1] = para[2] = para[3] = NULL;
					pargs = 0;
				}

				*mbuf++ = 'v';
				para[pargs++] = target_p->name;
			}
		}
		else if(fl & chan::VOICE)
		{
			*mbuf++ = 'v';
			para[pargs++] = target_p->name;
		}

		if(pargs >= chan::mode::MAXPARAMS)
		{
			*mbuf = '\0';
			sendto_channel_local(chan::ALL_MEMBERS, chptr,
					     ":%s MODE %s %s %s %s %s %s",
					     fakesource->name,
					     chptr->name.c_str(),
					     modebuf, para[0], para[1], para[2], para[3]);
			mbuf = modebuf;
			*mbuf++ = '+';
			para[0] = para[1] = para[2] = para[3] = NULL;
			pargs = 0;
		}

	      nextnick:
		/* p points to the next nick */
		s = p;

		/* if there was a trailing space and p was pointing to it, then we
		 * need to exit.. this has the side effect of breaking double spaces
		 * in an sjoin.. but that shouldnt happen anyway
		 */
		if(s && (*s == '\0'))
			s = p = NULL;

		/* if p was NULL due to no spaces, s wont exist due to the above, so
		 * we cant check it for spaces.. if there are no spaces, then when
		 * we next get here, s will be NULL
		 */
		if(s && ((p = (char *)strchr(s, ' ')) != NULL))
		{
			*p++ = '\0';
		}
	}

	*mbuf = '\0';
	if(pargs)
	{
		static const auto check_empty([]
		(const char *const &str)
		{
			 return EmptyString(str)? "" : str;
		});

		sendto_channel_local(chan::ALL_MEMBERS, chptr,
		                     ":%s MODE %s %s %s %s %s %s",
		                     fakesource->name,
		                     chptr->name.c_str(),
		                     modebuf,
		                     para[0],
		                     check_empty(para[1]),
		                     check_empty(para[2]),
		                     check_empty(para[3]));
	}

	if(!joins && !(chptr->mode.mode & chan::mode::PERMANENT) && isnew)
	{
		delete chptr;
		return;
	}

	/* Keep the colon if we're sending an SJOIN without nicks -- jilles */
	if (joins)
	{
		*(ptr_uid - 1) = '\0';
	}

	sendto_server(client.from, NULL, CAP_TS6, NOCAPS, "%s", buf_uid);
}

/*
 * do_join_0
 *
 * inputs	- pointer to client doing join 0
 * output	- NONE
 * side effects	- Use has decided to join 0. This is legacy
 *		  from the days when channels were numbers not names. *sigh*
 */
static void
do_join_0(client::client &client, client::client &source)
{
	chan::membership *msptr;
	chan::chan *chptr = NULL;
	rb_dlink_node *ptr;

	/* Finish the flood grace period... */
	if(my(source) && !is_flood_done(source))
		flood_endgrace(&source);

	sendto_server(&client, NULL, CAP_TS6, NOCAPS, ":%s JOIN 0", use_id(&source));

	for(const auto &pit : chans(user(source)))
	{
		if(my_connect(source) &&
		   !is(source, umode::OPER) && !is_exempt_spambot(source))
			chan::check_spambot_warning(&source, NULL);

		auto &msptr(pit.second);
		auto &chptr(pit.first);
		sendto_channel_local(chan::ALL_MEMBERS, chptr, ":%s!%s@%s PART %s",
				     source.name,
				     source.username, source.host, chptr->name.c_str());

		del(*chptr, get_client(*msptr));
	}
}

static bool
check_channel_name_loc(client::client &source, const char *name)
{
	const char *p;

	s_assert(name != NULL);
	if(EmptyString(name))
		return false;

	if(ConfigFileEntry.disable_fake_channels && !is(source, umode::OPER))
	{
		for(p = name; *p; ++p)
			if(!rfc1459::is_chan(*p) || rfc1459::is_fake_chan(*p))
				return false;
	}
	else
		for(p = name; *p; ++p)
			if(!rfc1459::is_chan(*p))
				return false;

	if(ConfigChannel.only_ascii_channels)
		for(p = name; *p; ++p)
			if(*p < 33 || *p > 126)
				return false;

	return true;
}

/* send_join_error()
 *
 * input	- client to send to, reason, channel name
 * output	- none
 * side effects - error message sent to client
 */
static void
send_join_error(client::client &source, int numeric, const char *name)
{
	/* This stuff is necessary because the form_str macro only
	 * accepts constants.
	 */
	switch (numeric)
	{
#define NORMAL_NUMERIC(i)						\
		case i:							\
			sendto_one(&source, form_str(i),		\
					me.name, source.name, name);	\
			break

		NORMAL_NUMERIC(ERR_BANNEDFROMCHAN);
		NORMAL_NUMERIC(ERR_INVITEONLYCHAN);
		NORMAL_NUMERIC(ERR_BADCHANNELKEY);
		NORMAL_NUMERIC(ERR_CHANNELISFULL);
		NORMAL_NUMERIC(ERR_NEEDREGGEDNICK);
		NORMAL_NUMERIC(ERR_THROTTLE);

		default:
			sendto_one_numeric(&source, numeric,
					"%s :Cannot join channel", name);
			break;
	}
}

static void
set_final_mode(chan::modes *mode, chan::modes *oldmode)
{
	int dir = MODE_QUERY;
	char *pbuf = parabuf;
	int len;
	int i;

	/* ok, first get a list of modes we need to add */
	for (i = 0; i < 256; i++)
	{
		if((mode->mode & chan::mode::table[i].type) && !(oldmode->mode & chan::mode::table[i].type))
		{
			if(dir != MODE_ADD)
			{
				*mbuf++ = '+';
				dir = MODE_ADD;
			}
			*mbuf++ = i;
		}
	}

	/* now the ones we need to remove. */
	for (i = 0; i < 256; i++)
	{
		if((oldmode->mode & chan::mode::table[i].type) && !(mode->mode & chan::mode::table[i].type))
		{
			if(dir != MODE_DEL)
			{
				*mbuf++ = '-';
				dir = MODE_DEL;
			}
			*mbuf++ = i;
		}
	}

	if(oldmode->limit && !mode->limit)
	{
		if(dir != MODE_DEL)
		{
			*mbuf++ = '-';
			dir = MODE_DEL;
		}
		*mbuf++ = 'l';
	}
	if(oldmode->key[0] && !mode->key[0])
	{
		if(dir != MODE_DEL)
		{
			*mbuf++ = '-';
			dir = MODE_DEL;
		}
		*mbuf++ = 'k';
		len = sprintf(pbuf, "%s ", oldmode->key);
		pbuf += len;
	}
	if(oldmode->join_num && !mode->join_num)
	{
		if(dir != MODE_DEL)
		{
			*mbuf++ = '-';
			dir = MODE_DEL;
		}
		*mbuf++ = 'j';
	}
	if(oldmode->forward[0] && !mode->forward[0])
	{
		if(dir != MODE_DEL)
		{
			*mbuf++ = '-';
			dir = MODE_DEL;
		}
		*mbuf++ = 'f';
	}
	if(mode->limit && oldmode->limit != mode->limit)
	{
		if(dir != MODE_ADD)
		{
			*mbuf++ = '+';
			dir = MODE_ADD;
		}
		*mbuf++ = 'l';
		len = sprintf(pbuf, "%d ", mode->limit);
		pbuf += len;
	}
	if(mode->key[0] && strcmp(oldmode->key, mode->key))
	{
		if(dir != MODE_ADD)
		{
			*mbuf++ = '+';
			dir = MODE_ADD;
		}
		*mbuf++ = 'k';
		len = sprintf(pbuf, "%s ", mode->key);
		pbuf += len;
	}
	if(mode->join_num && (oldmode->join_num != mode->join_num || oldmode->join_time != mode->join_time))
	{
		if(dir != MODE_ADD)
		{
			*mbuf++ = '+';
			dir = MODE_ADD;
		}
		*mbuf++ = 'j';
		len = sprintf(pbuf, "%d:%d ", mode->join_num, mode->join_time);
		pbuf += len;
	}
	if(mode->forward[0] && strcmp(oldmode->forward, mode->forward) &&
			ConfigChannel.use_forward)
	{
		if(dir != MODE_ADD)
		{
			*mbuf++ = '+';
			dir = MODE_ADD;
		}
		*mbuf++ = 'f';
		len = sprintf(pbuf, "%s ", mode->forward);
		pbuf += len;
	}
	*mbuf = '\0';
}

/*
 * remove_our_modes
 *
 * inputs	-
 * output	-
 * side effects	-
 */
static void
remove_our_modes(chan::chan *chptr, client::client &source)
{
	rb_dlink_node *ptr;
	char lmodebuf[chan::mode::BUFLEN];
	char *lpara[chan::mode::MAXPARAMS];
	int count = 0;
	int i;

	mbuf = lmodebuf;
	*mbuf++ = '-';

	for(i = 0; i < chan::mode::MAXPARAMS; i++)
		lpara[i] = NULL;

	for(auto &pit : chptr->members.global)
	{
		auto &client(pit.first);
		auto &msptr(pit.second);

		if(is_chanop(msptr))
		{
			msptr.flags &= ~chan::CHANOP;
			lpara[count++] = client->name;
			*mbuf++ = 'o';

			/* +ov, might not fit so check. */
			if(is_voiced(msptr))
			{
				if(count >= chan::mode::MAXPARAMS)
				{
					*mbuf = '\0';
					sendto_channel_local(chan::ALL_MEMBERS, chptr,
							     ":%s MODE %s %s %s %s %s %s",
							     source.name, chptr->name.c_str(),
							     lmodebuf, lpara[0], lpara[1],
							     lpara[2], lpara[3]);

					/* preserve the initial '-' */
					mbuf = lmodebuf;
					*mbuf++ = '-';
					count = 0;

					for(i = 0; i < chan::mode::MAXPARAMS; i++)
						lpara[i] = NULL;
				}

				msptr.flags &= ~chan::VOICE;
				lpara[count++] = client->name;
				*mbuf++ = 'v';
			}
		}
		else if(is_voiced(msptr))
		{
			msptr.flags &= ~chan::VOICE;
			lpara[count++] = client->name;
			*mbuf++ = 'v';
		}
		else
			continue;

		if(count >= chan::mode::MAXPARAMS)
		{
			*mbuf = '\0';
			sendto_channel_local(chan::ALL_MEMBERS, chptr,
					     ":%s MODE %s %s %s %s %s %s",
					     source.name, chptr->name.c_str(), lmodebuf,
					     lpara[0], lpara[1], lpara[2], lpara[3]);
			mbuf = lmodebuf;
			*mbuf++ = '-';
			count = 0;

			for(i = 0; i < chan::mode::MAXPARAMS; i++)
				lpara[i] = NULL;
		}
	}

	if(count != 0)
	{
		*mbuf = '\0';
		sendto_channel_local(chan::ALL_MEMBERS, chptr,
				     ":%s MODE %s %s %s %s %s %s",
				     source.name, chptr->name.c_str(), lmodebuf,
				     EmptyString(lpara[0]) ? "" : lpara[0],
				     EmptyString(lpara[1]) ? "" : lpara[1],
				     EmptyString(lpara[2]) ? "" : lpara[2],
				     EmptyString(lpara[3]) ? "" : lpara[3]);

	}
}

/* remove_ban_list()
 *
 * inputs	- channel, source, list to remove, char of mode, caps needed
 * outputs	-
 * side effects - given list is removed, with modes issued to local clients
 */
static void
remove_ban_list(chan::chan &chan,
                client::client &source,
                chan::list &list,
                const char &c,
                const int &mems)
{
	static char lmodebuf[BUFSIZE], lparabuf[BUFSIZE];

	int count (0);
	char *pbuf(lparabuf);

	int cur_len, mlen;
	cur_len = mlen = sprintf(lmodebuf, ":%s MODE %s -", source.name, chan.name.c_str());
	char *mbuf(lmodebuf + mlen);

	for (const auto &ban : list)
	{
		const auto &mask(ban.banstr);
		const auto &fwd(ban.forward);

		//trailing space, and the mode letter itself
		const auto plen(mask.size() + 2 + (!fwd.empty()? fwd.size() + 1 : 0));
		if (count >= chan::mode::MAXPARAMS || (cur_len + plen) > BUFSIZE - 4)
		{
			// remove trailing space
			*mbuf = '\0';
			*(pbuf - 1) = '\0';

			sendto_channel_local(mems, &chan, "%s %s", lmodebuf, lparabuf);

			cur_len = mlen;
			mbuf = lmodebuf + mlen;
			pbuf = lparabuf;
			count = 0;
		}

		*mbuf++ = c;
		cur_len += plen;

		if (!fwd.empty())
			pbuf += sprintf(pbuf, "%s$%s ", mask.c_str(), fwd.c_str());
		else
			pbuf += sprintf(pbuf, "%s ", mask.c_str());

		count++;
	}

	*mbuf = '\0';
	*(pbuf - 1) = '\0';

	sendto_channel_local(mems, &chan, "%s %s", lmodebuf, lparabuf);
	list.clear();
}