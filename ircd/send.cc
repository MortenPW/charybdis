/*
 *  ircd-ratbox: A slightly useful ircd.
 *  send.c: Functions for sending messages.
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

/* send the message to the link the target is attached to */
#define send_linebuf(a,b) _send_linebuf((a->from ? a->from : a) ,b)

static void send_queued_write(rb_fde_t *F, void *data);

unsigned long current_serial = 0L;

client::client *remote_rehash_oper_p;

/* send_linebuf()
 *
 * inputs	- client to send to, linebuf to attach
 * outputs	-
 * side effects - linebuf is attached to client
 */
static int
_send_linebuf(client::client *to, buf_head_t *linebuf)
{
	if(is_me(*to))
	{
		sendto_realops_snomask(sno::GENERAL, L_ALL, "Trying to send message to myself!");
		return 0;
	}

	if(!my_connect(*to) || is_io_error(*to))
		return 0;

	if(rb_linebuf_len(&to->localClient->buf_sendq) > get_sendq(to))
	{
		if(is_server(*to))
		{
			sendto_realops_snomask(sno::GENERAL, L_ALL,
					     "Max SendQ limit exceeded for %s: %u > %lu",
					     to->name,
					     rb_linebuf_len(&to->localClient->buf_sendq),
					     get_sendq(to));

			ilog(L_SERVER, "Max SendQ limit exceeded for %s: %u > %lu",
			     log_client_name(to, SHOW_IP),
			     rb_linebuf_len(&to->localClient->buf_sendq),
			     get_sendq(to));
		}

		dead_link(to, 1);
		return -1;
	}
	else
	{
		/* just attach the linebuf to the sendq instead of
		 * generating a new one
		 */
		rb_linebuf_attach(&to->localClient->buf_sendq, linebuf);
	}

	/*
	 ** Update statistics. The following is slightly incorrect
	 ** because it counts messages even if queued, but bytes
	 ** only really sent. Queued bytes get updated in SendQueued.
	 */
	to->localClient->sendM += 1;
	me.localClient->sendM += 1;
	if(rb_linebuf_len(&to->localClient->buf_sendq) > 0)
		send_queued(to);
	return 0;
}

/* send_linebuf_remote()
 *
 * inputs	- client to attach to, sender, linebuf
 * outputs	-
 * side effects - client has linebuf attached
 */
static void
send_linebuf_remote(client::client *to, client::client *from, buf_head_t *linebuf)
{
	if(to->from)
		to = to->from;

	/* we assume the caller has already tested for fake direction */
	_send_linebuf(to, linebuf);
}

/* send_queued_write()
 *
 * inputs	- fd to have queue sent, client we're sending to
 * outputs	- contents of queue
 * side effects - write is rescheduled if queue isnt emptied
 */
void
send_queued(client::client *to)
{
	int retlen;

	rb_fde_t *F = to->localClient->F;
	if (!F)
		return;

	/* cant write anything to a dead socket. */
	if(is_io_error(*to))
		return;

	/* try to flush later when the write event resets this */
	if(IsFlush(to))
		return;

	if(rb_linebuf_len(&to->localClient->buf_sendq))
	{
		while ((retlen =
			rb_linebuf_flush(F, &to->localClient->buf_sendq)) > 0)
		{
			/* We have some data written .. update counters */
			ClearFlush(to);

			to->localClient->sendB += retlen;
			me.localClient->sendB += retlen;
			if(to->localClient->sendB > 1023)
			{
				to->localClient->sendK += (to->localClient->sendB >> 10);
				to->localClient->sendB &= 0x03ff;	/* 2^10 = 1024, 3ff = 1023 */
			}
			else if(me.localClient->sendB > 1023)
			{
				me.localClient->sendK += (me.localClient->sendB >> 10);
				me.localClient->sendB &= 0x03ff;
			}
		}

		if(retlen == 0 || (retlen < 0 && !rb_ignore_errno(errno)))
		{
			dead_link(to, 0);
			return;
		}
	}

	if(rb_linebuf_len(&to->localClient->buf_sendq))
	{
		SetFlush(to);
		rb_setselect(to->localClient->F, RB_SELECT_WRITE,
			       send_queued_write, to);
	}
	else
		ClearFlush(to);
}

void
send_pop_queue(client::client *to)
{
	if(to->from != NULL)
		to = to->from;
	if(!my_connect(*to) || is_io_error(*to))
		return;
	if(rb_linebuf_len(&to->localClient->buf_sendq) > 0)
		send_queued(to);
}

/* send_queued_write()
 *
 * inputs	- fd to have queue sent, client we're sending to
 * outputs	- contents of queue
 * side effects - write is scheduled if queue isnt emptied
 */
static void
send_queued_write(rb_fde_t *F, void *data)
{
	client::client *to = (client::client *)data;
	ClearFlush(to);
	send_queued(to);
}

/*
 * linebuf_put_msgvbuf
 *
 * inputs       - msgbuf header, linebuf object, capability mask, pattern, arguments
 * outputs      - none
 * side effects - the linebuf object is cleared, then populated using rb_linebuf_putmsg().
 */
static void
linebuf_put_msgvbuf(struct MsgBuf *msgbuf, buf_head_t *linebuf, unsigned int capmask, const char *pattern, va_list *va)
{
	char buf[BUFSIZE];

	rb_linebuf_newbuf(linebuf);
	msgbuf_unparse_prefix(buf, sizeof buf, msgbuf, capmask);
	rb_linebuf_putprefix(linebuf, pattern, va, buf);
}

/* linebuf_put_msgbuf
 *
 * inputs       - msgbuf header, linebuf object, capability mask, pattern, arguments
 * outputs      - none
 * side effects - the linebuf object is cleared, then populated using rb_linebuf_putmsg().
 */
static void
linebuf_put_msgbuf(struct MsgBuf *msgbuf, buf_head_t *linebuf, unsigned int capmask, const char *pattern, ...)
{
	va_list va;

	va_start(va, pattern);
	linebuf_put_msgvbuf(msgbuf, linebuf, capmask, pattern, &va);
	va_end(va);
}

/* build_msgbuf_from
 *
 * inputs       - msgbuf object, client the message is from
 * outputs      - none
 * side effects - a msgbuf object is populated with an origin and relevant tags
 * notes        - to make this reentrant, find a solution for `buf` below
 */
static void
build_msgbuf_from(struct MsgBuf *msgbuf, client::client *from, const char *cmd)
{
	static char buf[BUFSIZE];
	hook_data hdata;

	msgbuf_init(msgbuf);

	msgbuf->origin = buf;
	msgbuf->cmd = cmd;

	if (from != NULL && is_person(*from))
		snprintf(buf, sizeof buf, "%s!%s@%s", from->name, from->username, from->host);
	else if (from != NULL)
		rb_strlcpy(buf, from->name, sizeof buf);
	else
		rb_strlcpy(buf, me.name, sizeof buf);

	hdata.client = from;
	hdata.arg1 = msgbuf;

	call_hook(h_outbound_msgbuf, &hdata);
}

/* sendto_one()
 *
 * inputs	- client to send to, va_args
 * outputs	- client has message put into its queue
 * side effects -
 */
void
sendto_one(client::client *target_p, const char *pattern, ...)
{
	va_list args;
	buf_head_t linebuf;

	/* send remote if to->from non NULL */
	if(target_p->from != NULL)
		target_p = target_p->from;

	if(is_io_error(*target_p))
		return;

	rb_linebuf_newbuf(&linebuf);

	va_start(args, pattern);
	rb_linebuf_putmsg(&linebuf, pattern, &args, NULL);
	va_end(args);

	_send_linebuf(target_p, &linebuf);

	rb_linebuf_donebuf(&linebuf);
}

/* sendto_one_prefix()
 *
 * inputs	- client to send to, va_args
 * outputs	- client has message put into its queue
 * side effects - source(us)/target is chosen based on TS6 capability
 */
void
sendto_one_prefix(client::client *target_p, client::client *source_p,
		  const char *command, const char *pattern, ...)
{
	client::client *dest_p;
	va_list args;
	buf_head_t linebuf;

	/* send remote if to->from non NULL */
	if(target_p->from != NULL)
		dest_p = target_p->from;
	else
		dest_p = target_p;

	if(is_io_error(*dest_p))
		return;

	if(is_me(*dest_p))
	{
		sendto_realops_snomask(sno::GENERAL, L_ALL, "Trying to send to myself!");
		return;
	}

	rb_linebuf_newbuf(&linebuf);
	va_start(args, pattern);
	rb_linebuf_putmsg(&linebuf, pattern, &args,
		       ":%s %s %s ",
		       get_id(source_p, target_p),
		       command, get_id(target_p, target_p));
	va_end(args);

	_send_linebuf(dest_p, &linebuf);
	rb_linebuf_donebuf(&linebuf);
}

/* sendto_one_notice()
 *
 * inputs	- client to send to, va_args
 * outputs	- client has a NOTICE put into its queue
 * side effects - source(us)/target is chosen based on TS6 capability
 */
void
sendto_one_notice(client::client *target_p, const char *pattern, ...)
{
	client::client *dest_p;
	va_list args;
	buf_head_t linebuf;
	char *to;

	/* send remote if to->from non NULL */
	if(target_p->from != NULL)
		dest_p = target_p->from;
	else
		dest_p = target_p;

	if(is_io_error(*dest_p))
		return;

	if(is_me(*dest_p))
	{
		sendto_realops_snomask(sno::GENERAL, L_ALL, "Trying to send to myself!");
		return;
	}

	rb_linebuf_newbuf(&linebuf);
	va_start(args, pattern);
	rb_linebuf_putmsg(&linebuf, pattern, &args,
		       ":%s NOTICE %s ",
		       get_id(&me, target_p), *(to = get_id(target_p, target_p)) != '\0' ? to : "*");
	va_end(args);

	_send_linebuf(dest_p, &linebuf);
	rb_linebuf_donebuf(&linebuf);
}


/* sendto_one_numeric()
 *
 * inputs	- client to send to, va_args
 * outputs	- client has message put into its queue
 * side effects - source/target is chosen based on TS6 capability
 */
void
sendto_one_numeric(client::client *target_p, int numeric, const char *pattern, ...)
{
	client::client *dest_p;
	va_list args;
	buf_head_t linebuf;
	char *to;

	/* send remote if to->from non NULL */
	if(target_p->from != NULL)
		dest_p = target_p->from;
	else
		dest_p = target_p;

	if(is_io_error(*dest_p))
		return;

	if(is_me(*dest_p))
	{
		sendto_realops_snomask(sno::GENERAL, L_ALL, "Trying to send to myself!");
		return;
	}

	rb_linebuf_newbuf(&linebuf);
	va_start(args, pattern);
	rb_linebuf_putmsg(&linebuf, pattern, &args,
		       ":%s %03d %s ",
		       get_id(&me, target_p),
		       numeric, *(to = get_id(target_p, target_p)) != '\0' ? to : "*");
	va_end(args);

	_send_linebuf(dest_p, &linebuf);
	rb_linebuf_donebuf(&linebuf);
}

/*
 * sendto_server
 *
 * inputs       - pointer to client to NOT send to
 *              - caps or'd together which must ALL be present
 *              - caps or'd together which must ALL NOT be present
 *              - printf style format string
 *              - args to format string
 * output       - NONE
 * side effects - Send a message to all connected servers, except the
 *                client 'one' (if non-NULL), as long as the servers
 *                support ALL capabs in 'caps', and NO capabs in 'nocaps'.
 *
 * This function was written in an attempt to merge together the other
 * billion sendto_*serv*() functions, which sprung up with capabs, uids etc
 * -davidt
 */
void
sendto_server(client::client *one, chan::chan *chptr, unsigned long caps,
	      unsigned long nocaps, const char *format, ...)
{
	va_list args;
	client::client *target_p;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	buf_head_t linebuf;

	/* noone to send to.. */
	if(rb_dlink_list_length(&serv_list) == 0)
		return;

	if(chptr != NULL && chptr->name[0] != '#')
			return;

	rb_linebuf_newbuf(&linebuf);
	va_start(args, format);
	rb_linebuf_putmsg(&linebuf, format, &args, NULL);
	va_end(args);

	RB_DLINK_FOREACH_SAFE(ptr, next_ptr, serv_list.head)
	{
		target_p = (client::client *)ptr->data;

		/* check against 'one' */
		if(one != NULL && (target_p == one->from))
			continue;

		/* check we have required capabs */
		if(!IsCapable(target_p, caps))
			continue;

		/* check we don't have any forbidden capabs */
		if(!NotCapable(target_p, nocaps))
			continue;

		_send_linebuf(target_p, &linebuf);
	}

	rb_linebuf_donebuf(&linebuf);
}

/* sendto_channel_flags()
 *
 * inputs	- server not to send to, flags needed, source, channel, va_args
 * outputs	- message is sent to channel members
 * side effects -
 */
void
sendto_channel_flags(client::client *one, int type, client::client *source_p,
		     chan::chan *chptr, const char *pattern, ...)
{
	char buf[BUFSIZE];
	va_list args;
	buf_head_t rb_linebuf_local;
	buf_head_t rb_linebuf_id;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	int current_capmask = 0;
	struct MsgBuf msgbuf;

	rb_linebuf_newbuf(&rb_linebuf_local);
	rb_linebuf_newbuf(&rb_linebuf_id);

	current_serial++;

	build_msgbuf_from(&msgbuf, source_p, NULL);

	va_start(args, pattern);
	vsnprintf(buf, sizeof buf, pattern, args);
	va_end(args);

	linebuf_put_msgbuf(&msgbuf, &rb_linebuf_local, NOCAPS, "%s", buf);
	rb_linebuf_putmsg(&rb_linebuf_id, NULL, NULL, ":%s %s", use_id(source_p), buf);

	for(const auto &pair : chptr->members.global)
	{
		auto *const target_p(pair.first);
		const auto &member(pair.second);

		if(!my(*source_p) && (is_io_error(*target_p->from) || target_p->from == one))
			continue;

		if(my(*source_p) && !IsCapable(source_p, CLICAP_ECHO_MESSAGE) && target_p == one)
			continue;

		if(type && ((member.flags & type) == 0))
			continue;

		if(is(*target_p, umode::DEAF))
			continue;

		if(!my(*target_p))
		{
			/* if we've got a specific type, target must support
			 * CHW.. --fl
			 */
			if(type && NotCapable(target_p->from, CAP_CHW))
				continue;

			if(target_p->from->serial != current_serial)
			{
				send_linebuf_remote(target_p, source_p, &rb_linebuf_id);
				target_p->from->serial = current_serial;
			}
		}
		else
		{
			if (target_p->localClient->caps != current_capmask)
			{
				/* reset the linebuf */
				rb_linebuf_donebuf(&rb_linebuf_local);
				rb_linebuf_newbuf(&rb_linebuf_local);

				/* render the new linebuf and attach it */
				linebuf_put_msgbuf(&msgbuf, &rb_linebuf_local, target_p->localClient->caps, "%s", buf);
				current_capmask = target_p->localClient->caps;
			}

			_send_linebuf(target_p, &rb_linebuf_local);
		}
	}

	rb_linebuf_donebuf(&rb_linebuf_local);
	rb_linebuf_donebuf(&rb_linebuf_id);
}

/* sendto_channel_flags()
 *
 * inputs	- server not to send to, flags needed, source, channel, va_args
 * outputs	- message is sent to channel members
 * side effects -
 */
void
sendto_channel_opmod(client::client *one, client::client *source_p,
		     chan::chan *chptr, const char *command,
		     const char *text)
{
	buf_head_t rb_linebuf_local;
	buf_head_t rb_linebuf_old;
	buf_head_t rb_linebuf_new;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;

	rb_linebuf_newbuf(&rb_linebuf_local);
	rb_linebuf_newbuf(&rb_linebuf_old);
	rb_linebuf_newbuf(&rb_linebuf_new);

	current_serial++;

	if(is_server(*source_p))
		rb_linebuf_putmsg(&rb_linebuf_local, NULL, NULL,
			       ":%s %s %s :%s",
			       source_p->name, command, chptr->name.c_str(), text);
	else
		rb_linebuf_putmsg(&rb_linebuf_local, NULL, NULL,
			       ":%s!%s@%s %s %s :%s",
			       source_p->name, source_p->username,
			       source_p->host, command, chptr->name.c_str(), text);

	if (chptr->mode.mode & chan::mode::MODERATED)
		rb_linebuf_putmsg(&rb_linebuf_old, NULL, NULL,
			       ":%s %s %s :%s",
			       use_id(source_p), command, chptr->name.c_str(), text);
	else
		rb_linebuf_putmsg(&rb_linebuf_old, NULL, NULL,
			       ":%s NOTICE @%s :<%s:%s> %s",
			       use_id(source_p->servptr), chptr->name.c_str(),
			       source_p->name, chptr->name.c_str(), text);
	rb_linebuf_putmsg(&rb_linebuf_new, NULL, NULL,
		       ":%s %s =%s :%s",
		       use_id(source_p), command, chptr->name.c_str(), text);

	for(const auto &pair : chptr->members.global)
	{
		auto *const target_p(pair.first);
		const auto &member(pair.second);

		if(!my(*source_p) && (is_io_error(*target_p->from) || target_p->from == one))
			continue;

		if(my(*source_p) && !IsCapable(source_p, CLICAP_ECHO_MESSAGE) && target_p == one)
			continue;

		if((member.flags & chan::CHANOP) == 0)
			continue;

		if(is(*target_p, umode::DEAF))
			continue;

		if(!my(*target_p))
		{
			/* if we've got a specific type, target must support
			 * CHW.. --fl
			 */
			if(NotCapable(target_p->from, CAP_CHW))
				continue;

			if(target_p->from->serial != current_serial)
			{
				if (IsCapable(target_p->from, CAP_EOPMOD))
					send_linebuf_remote(target_p, source_p, &rb_linebuf_new);
				else
					send_linebuf_remote(target_p, source_p, &rb_linebuf_old);
				target_p->from->serial = current_serial;
			}
		}
		else
			_send_linebuf(target_p, &rb_linebuf_local);
	}

	rb_linebuf_donebuf(&rb_linebuf_local);
	rb_linebuf_donebuf(&rb_linebuf_old);
	rb_linebuf_donebuf(&rb_linebuf_new);
}

/* sendto_channel_local()
 *
 * inputs	- flags to send to, channel to send to, va_args
 * outputs	- message to local channel members
 * side effects -
 */
void
sendto_channel_local(int type, chan::chan *chptr, const char *pattern, ...)
{
	va_list args;
	buf_head_t linebuf;

	rb_linebuf_newbuf(&linebuf);

	va_start(args, pattern);
	rb_linebuf_putmsg(&linebuf, pattern, &args, NULL);
	va_end(args);

	for (const auto &pair : chptr->members.global)
	{
		auto *const target_p(pair.first);
		const auto &member(pair.second);

		if(is_io_error(*target_p))
			continue;

		if(type == chan::ONLY_OPERS)
		{
			if (!is(*target_p, umode::OPER))
				continue;
		}
		else if(type && ((member.flags & type) == 0))
			continue;

		_send_linebuf(target_p, &linebuf);
	}

	rb_linebuf_donebuf(&linebuf);
}

/*
 * _sendto_channel_local_with_capability_butone()
 *
 * Shared implementation of sendto_channel_local_with_capability and sendto_channel_local_with_capability_butone
 */
static void
_sendto_channel_local_with_capability_butone(client::client *one, int type, int caps, int negcaps, chan::chan *chptr,
	const char *pattern, va_list * args)
{
	buf_head_t linebuf;

	rb_linebuf_newbuf(&linebuf);
	rb_linebuf_putmsg(&linebuf, pattern, args, NULL);

	for(const auto &member : chptr->members.local)
	{
		const auto target_p(member->git->first);

		if (target_p == one)
			continue;

		if(is_io_error(*target_p) ||
		   !IsCapable(target_p, caps) ||
 		   !NotCapable(target_p, negcaps))
			continue;

		if(type && ((member->flags & type) == 0))
			continue;

		_send_linebuf(target_p, &linebuf);
	}

	rb_linebuf_donebuf(&linebuf);
}

/* sendto_channel_local_with_capability()
 *
 * inputs	- flags to send to, caps, negate caps, channel to send to, va_args
 * outputs	- message to local channel members
 * side effects -
 */
void
sendto_channel_local_with_capability(int type, int caps, int negcaps, chan::chan *chptr, const char *pattern, ...)
{
	va_list args;

	va_start(args, pattern);
	_sendto_channel_local_with_capability_butone(NULL, type, caps, negcaps, chptr, pattern, &args);
	va_end(args);
}


/* sendto_channel_local_with_capability()
 *
 * inputs	- flags to send to, caps, negate caps, channel to send to, va_args
 * outputs	- message to local channel members
 * side effects -
 */
void
sendto_channel_local_with_capability_butone(client::client *one, int type, int caps, int negcaps, chan::chan *chptr,
		const char *pattern, ...)
{
	va_list args;

	va_start(args, pattern);
	_sendto_channel_local_with_capability_butone(one, type, caps, negcaps, chptr, pattern, &args);
	va_end(args);
}


/* sendto_channel_local_butone()
 *
 * inputs	- flags to send to, channel to send to, va_args
 *		- user to ignore when sending
 * outputs	- message to local channel members
 * side effects -
 */
void
sendto_channel_local_butone(client::client *one, int type, chan::chan *chptr, const char *pattern, ...)
{
	va_list args;
	buf_head_t linebuf;
	struct MsgBuf msgbuf;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;

	rb_linebuf_newbuf(&linebuf);

	build_msgbuf_from(&msgbuf, one, NULL);

	va_start(args, pattern);
	rb_linebuf_putmsg(&linebuf, pattern, &args, NULL);
	va_end(args);

	for(const auto &member : chptr->members.local)
	{
		const auto target_p(member->git->first);

		if(target_p == one)
			continue;

		if(is_io_error(*target_p))
			continue;

		if(type && ((member->flags & type) == 0))
			continue;

		/* attach the present linebuf to the target */
		_send_linebuf(target_p, &linebuf);
	}

	rb_linebuf_donebuf(&linebuf);
}

/*
 * sendto_common_channels_local()
 *
 * inputs	- pointer to client
 *              - capability mask
 * 		- negated capability mask
 *		- pattern to send
 * output	- NONE
 * side effects	- Sends a message to all people on local server who are
 * 		  in same channel with user.
 *		  used by m_nick.c and exit_one_client.
 */
void
sendto_common_channels_local(client::client *client, int cap, int negcap, const char *pattern, ...)
{
	va_list args;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	rb_dlink_node *uptr;
	rb_dlink_node *next_uptr;
	chan::chan *chptr;
	client::client *target_p;
	chan::membership *msptr;
	chan::membership *mscptr;
	buf_head_t linebuf;

	rb_linebuf_newbuf(&linebuf);
	va_start(args, pattern);
	rb_linebuf_putmsg(&linebuf, pattern, &args, NULL);
	va_end(args);

	++current_serial;

	for(const auto &pit : chans(user(*client)))
	{
		auto &chptr(pit.first);
		auto &mscptr(pit.second);

		for(const auto &msptr : chptr->members.local)
		{
			const auto target_p(msptr->git->first);

			if(is_io_error(*target_p) ||
			   target_p->serial == current_serial ||
			   !IsCapable(target_p, cap) ||
			   !NotCapable(target_p, negcap))
				continue;

			target_p->serial = current_serial;
			send_linebuf(target_p, &linebuf);
		}
	}

	/* this can happen when the user isnt in any channels, but we still
	 * need to send them the data, ie a nick change
	 */
	if(my_connect(*client) && (client->serial != current_serial))
		send_linebuf(client, &linebuf);

	rb_linebuf_donebuf(&linebuf);
}

/*
 * sendto_common_channels_local_butone()
 *
 * inputs	- pointer to client
 *              - capability mask
 * 		- negated capability mask
 *		- pattern to send
 * output	- NONE
 * side effects	- Sends a message to all people on local server who are
 * 		  in same channel with user, except for user itself.
 */
void
sendto_common_channels_local_butone(client::client *client, int cap, int negcap, const char *pattern, ...)
{
	using chan::membership;

	va_list args;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	rb_dlink_node *uptr;
	rb_dlink_node *next_uptr;
	chan::chan *chptr;
	client::client *target_p;
	membership *msptr;
	membership *mscptr;
	buf_head_t linebuf;

	rb_linebuf_newbuf(&linebuf);

	va_start(args, pattern);
	rb_linebuf_putmsg(&linebuf, pattern, &args, NULL);
	va_end(args);

	++current_serial;
	/* Skip them -- jilles */
	client->serial = current_serial;

	for(const auto &pit : chans(user(*client)))
	{
		auto &chptr(pit.first);
		auto &mscptr(pit.second);

		for(const auto &msptr : chptr->members.local)
		{
			const auto target_p(msptr->git->first);

			if(is_io_error(*target_p) ||
			   target_p->serial == current_serial ||
			   !IsCapable(target_p, cap) ||
			   !NotCapable(target_p, negcap))
				continue;

			target_p->serial = current_serial;
			send_linebuf(target_p, &linebuf);
		}
	}

	rb_linebuf_donebuf(&linebuf);
}

/* sendto_match_butone()
 *
 * inputs	- server not to send to, source, mask, type of mask, va_args
 * output	-
 * side effects - message is sent to matching clients
 */
void
sendto_match_butone(client::client *one, client::client *source_p,
		    const char *mask, int what, const char *pattern, ...)
{
	static char buf[BUFSIZE];
	va_list args;
	client::client *target_p;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	buf_head_t rb_linebuf_local;
	buf_head_t rb_linebuf_id;

	rb_linebuf_newbuf(&rb_linebuf_local);
	rb_linebuf_newbuf(&rb_linebuf_id);

	va_start(args, pattern);
	vsnprintf(buf, sizeof(buf), pattern, args);
	va_end(args);

	if(is_server(*source_p))
		rb_linebuf_putmsg(&rb_linebuf_local, NULL, NULL,
			       ":%s %s", source_p->name, buf);
	else
		rb_linebuf_putmsg(&rb_linebuf_local, NULL, NULL,
			       ":%s!%s@%s %s",
			       source_p->name, source_p->username,
			       source_p->host, buf);

	rb_linebuf_putmsg(&rb_linebuf_id, NULL, NULL, ":%s %s", use_id(source_p), buf);

	if(what == MATCH_HOST)
	{
		RB_DLINK_FOREACH_SAFE(ptr, next_ptr, lclient_list.head)
		{
			target_p = (client::client *)ptr->data;

			if(match(mask, target_p->host))
				_send_linebuf(target_p, &rb_linebuf_local);
		}
	}
	/* what = MATCH_SERVER, if it doesnt match us, just send remote */
	else if(match(mask, me.name))
	{
		RB_DLINK_FOREACH_SAFE(ptr, next_ptr, lclient_list.head)
		{
			target_p = (client::client *)ptr->data;
			_send_linebuf(target_p, &rb_linebuf_local);
		}
	}

	RB_DLINK_FOREACH(ptr, serv_list.head)
	{
		target_p = (client::client *)ptr->data;

		if(target_p == one)
			continue;

		send_linebuf_remote(target_p, source_p, &rb_linebuf_id);
	}

	rb_linebuf_donebuf(&rb_linebuf_local);
	rb_linebuf_donebuf(&rb_linebuf_id);
}

/* sendto_match_servs()
 *
 * inputs       - source, mask to send to, caps needed, va_args
 * outputs      -
 * side effects - message is sent to matching servers with caps.
 */
void
sendto_match_servs(client::client *source_p, const char *mask, int cap,
			int nocap, const char *pattern, ...)
{
	static char buf[BUFSIZE];
	va_list args;
	rb_dlink_node *ptr;
	client::client *target_p;
	buf_head_t rb_linebuf_id;

	if(EmptyString(mask))
		return;

	rb_linebuf_newbuf(&rb_linebuf_id);

	va_start(args, pattern);
	vsnprintf(buf, sizeof(buf), pattern, args);
	va_end(args);

	rb_linebuf_putmsg(&rb_linebuf_id, NULL, NULL,
			":%s %s", use_id(source_p), buf);

	current_serial++;

	RB_DLINK_FOREACH(ptr, global_serv_list.head)
	{
		target_p = (client::client *)ptr->data;

		/* dont send to ourselves, or back to where it came from.. */
		if(is_me(*target_p) || target_p->from == source_p->from)
			continue;

		if(target_p->from->serial == current_serial)
			continue;

		if(match(mask, target_p->name))
		{
			/* if we set the serial here, then we'll never do
			 * a match() again if !IsCapable()
			 */
			target_p->from->serial = current_serial;

			if(cap && !IsCapable(target_p->from, cap))
				continue;

			if(nocap && !NotCapable(target_p->from, nocap))
				continue;

			_send_linebuf(target_p->from, &rb_linebuf_id);
		}
	}

	rb_linebuf_donebuf(&rb_linebuf_id);
}

/* sendto_local_clients_with_capability()
 *
 * inputs       - caps needed, pattern, va_args
 * outputs      -
 * side effects - message is sent to matching local clients with caps.
 */
void
sendto_local_clients_with_capability(int cap, const char *pattern, ...)
{
	va_list args;
	rb_dlink_node *ptr;
	client::client *target_p;
	buf_head_t linebuf;

	rb_linebuf_newbuf(&linebuf);

	va_start(args, pattern);
	rb_linebuf_putmsg(&linebuf, pattern, &args, NULL);
	va_end(args);

	RB_DLINK_FOREACH(ptr, lclient_list.head)
	{
		target_p = (client::client *)ptr->data;

		if(is_io_error(*target_p) || !IsCapable(target_p, cap))
			continue;

		send_linebuf(target_p, &linebuf);
	}

	rb_linebuf_donebuf(&linebuf);
}

/* sendto_monitor()
 *
 * inputs	- monitor nick to send to, format, va_args
 * outputs	- message to local users monitoring the given nick
 * side effects -
 */
void
sendto_monitor(struct monitor *monptr, const char *pattern, ...)
{
	va_list args;
	buf_head_t linebuf;
	client::client *target_p;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;

	rb_linebuf_newbuf(&linebuf);

	va_start(args, pattern);
	rb_linebuf_putmsg(&linebuf, pattern, &args, NULL);
	va_end(args);

	RB_DLINK_FOREACH_SAFE(ptr, next_ptr, monptr->users.head)
	{
		target_p = (client::client *)ptr->data;

		if(is_io_error(*target_p))
			continue;

		_send_linebuf(target_p, &linebuf);
	}

	rb_linebuf_donebuf(&linebuf);
}

/* sendto_anywhere()
 *
 * inputs	- target, source, va_args
 * outputs	-
 * side effects - client is sent message with correct prefix.
 */
void
sendto_anywhere(client::client *target_p, client::client *source_p,
		const char *command, const char *pattern, ...)
{
	va_list args;
	buf_head_t linebuf;

	rb_linebuf_newbuf(&linebuf);

	va_start(args, pattern);

	if(my(*target_p))
	{
		if(is_server(*source_p))
			rb_linebuf_putmsg(&linebuf, pattern, &args, ":%s %s %s ",
				       source_p->name, command,
				       target_p->name);
		else
		{
			struct MsgBuf msgbuf;

			build_msgbuf_from(&msgbuf, source_p, command);
			msgbuf.target = target_p->name;

			linebuf_put_msgvbuf(&msgbuf, &linebuf, target_p->localClient->caps, pattern, &args);
		}
	}
	else
		rb_linebuf_putmsg(&linebuf, pattern, &args, ":%s %s %s ",
			       get_id(source_p, target_p), command,
			       get_id(target_p, target_p));
	va_end(args);

	if(my(*target_p))
		_send_linebuf(target_p, &linebuf);
	else
		send_linebuf_remote(target_p, source_p, &linebuf);

	rb_linebuf_donebuf(&linebuf);
}

/* sendto_realops_snomask()
 *
 * inputs	- snomask needed, level (opers/admin), va_args
 * output	-
 * side effects - message is sent to opers with matching snomasks
 */
void
sendto_realops_snomask(int flags, int level, const char *pattern, ...)
{
	static char buf[BUFSIZE];
	client::client *client_p;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	va_list args;
	buf_head_t linebuf;

	rb_linebuf_newbuf(&linebuf);

	/* Be very sure not to do things like "Trying to send to myself"
	 * L_NETWIDE, otherwise infinite recursion may result! -- jilles */
	if (level & L_NETWIDE && ConfigFileEntry.global_snotices)
	{
		/* rather a lot of copying around, oh well -- jilles */
		va_start(args, pattern);
		vsnprintf(buf, sizeof(buf), pattern, args);
		va_end(args);
		rb_linebuf_putmsg(&linebuf, pattern, NULL,
				":%s NOTICE * :%s", me.name, buf);

		static char snobuf[128];
		if(*delta(sno::table, 0, flags, snobuf))
			sendto_server(NULL, NULL, CAP_ENCAP|CAP_TS6, NOCAPS,
			              ":%s ENCAP * SNOTE %c :%s",
			              me.id,
			              snobuf[1],
			              buf);
	}
	else if (remote_rehash_oper_p != NULL)
	{
		/* rather a lot of copying around, oh well -- jilles */
		va_start(args, pattern);
		vsnprintf(buf, sizeof(buf), pattern, args);
		va_end(args);
		rb_linebuf_putmsg(&linebuf, pattern, NULL,
				":%s NOTICE * :%s", me.name, buf);
		sendto_one_notice(remote_rehash_oper_p, ":%s", buf);
	}
	else
	{
		va_start(args, pattern);
		rb_linebuf_putmsg(&linebuf, pattern, &args,
				":%s NOTICE * :", me.name);
		va_end(args);
	}
	level &= ~L_NETWIDE;

	RB_DLINK_FOREACH_SAFE(ptr, next_ptr, local_oper_list.head)
	{
		client_p = (client::client *)ptr->data;

		/* If we're sending it to opers and theyre an admin, skip.
		 * If we're sending it to admins, and theyre not, skip.
		 */
		if(((level == L_ADMIN) && !IsOperAdmin(client_p)) ||
		   ((level == L_OPER) && IsOperAdmin(client_p)))
			continue;

		if(client_p->snomask & flags)
			_send_linebuf(client_p, &linebuf);
	}

	rb_linebuf_donebuf(&linebuf);
}
/* sendto_realops_snomask_from()
 *
 * inputs	- snomask needed, level (opers/admin), source server, va_args
 * output	-
 * side effects - message is sent to opers with matching snomask
 */
void
sendto_realops_snomask_from(int flags, int level, client::client *source_p,
		const char *pattern, ...)
{
	client::client *client_p;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	va_list args;
	buf_head_t linebuf;

	rb_linebuf_newbuf(&linebuf);

	va_start(args, pattern);
	rb_linebuf_putmsg(&linebuf, pattern, &args,
		       ":%s NOTICE * :", source_p->name);
	va_end(args);

	RB_DLINK_FOREACH_SAFE(ptr, next_ptr, local_oper_list.head)
	{
		client_p = (client::client *)ptr->data;

		/* If we're sending it to opers and theyre an admin, skip.
		 * If we're sending it to admins, and theyre not, skip.
		 */
		if(((level == L_ADMIN) && !IsOperAdmin(client_p)) ||
		   ((level == L_OPER) && IsOperAdmin(client_p)))
			continue;

		if(client_p->snomask & flags)
			_send_linebuf(client_p, &linebuf);
	}

	rb_linebuf_donebuf(&linebuf);
}

/*
 * sendto_wallops_flags
 *
 * inputs       - flag types of messages to show to real opers
 *              - client sending request
 *              - var args input message
 * output       - NONE
 * side effects - Send a wallops to local opers
 */
void
sendto_wallops_flags(int flags, client::client *source_p, const char *pattern, ...)
{
	client::client *client_p;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	va_list args;
	buf_head_t linebuf;

	rb_linebuf_newbuf(&linebuf);

	va_start(args, pattern);

	if(is_person(*source_p))
		rb_linebuf_putmsg(&linebuf, pattern, &args,
			       ":%s!%s@%s WALLOPS :", source_p->name,
			       source_p->username, source_p->host);
	else
		rb_linebuf_putmsg(&linebuf, pattern, &args, ":%s WALLOPS :", source_p->name);

	va_end(args);

	RB_DLINK_FOREACH_SAFE(ptr, next_ptr, is_person(*source_p) && flags == umode::WALLOP ? lclient_list.head : local_oper_list.head)
	{
		client_p = (client::client *)ptr->data;

		if (is(*client_p, flags))
			_send_linebuf(client_p, &linebuf);
	}

	rb_linebuf_donebuf(&linebuf);
}

/* kill_client()
 *
 * input	- client to send kill to, client to kill, va_args
 * output	-
 * side effects - we issue a kill for the client
 */
void
kill_client(client::client *target_p, client::client *diedie, const char *pattern, ...)
{
	va_list args;
	buf_head_t linebuf;

	rb_linebuf_newbuf(&linebuf);

	va_start(args, pattern);
	rb_linebuf_putmsg(&linebuf, pattern, &args, ":%s KILL %s :",
		      get_id(&me, target_p), get_id(diedie, target_p));
	va_end(args);

	send_linebuf(target_p, &linebuf);
	rb_linebuf_donebuf(&linebuf);
}


/*
 * kill_client_serv_butone
 *
 * inputs	- pointer to client to not send to
 *		- pointer to client to kill
 * output	- NONE
 * side effects	- Send a KILL for the given client
 *		  message to all connected servers
 *                except the client 'one'. Also deal with
 *		  client being unknown to leaf, as in lazylink...
 */
void
kill_client_serv_butone(client::client *one, client::client *target_p, const char *pattern, ...)
{
	static char buf[BUFSIZE];
	va_list args;
	client::client *client_p;
	rb_dlink_node *ptr;
	rb_dlink_node *next_ptr;
	buf_head_t rb_linebuf_id;

	rb_linebuf_newbuf(&rb_linebuf_id);

	va_start(args, pattern);
	vsnprintf(buf, sizeof(buf), pattern, args);
	va_end(args);

	rb_linebuf_putmsg(&rb_linebuf_id, NULL, NULL, ":%s KILL %s :%s",
		       use_id(&me), use_id(target_p), buf);

	RB_DLINK_FOREACH_SAFE(ptr, next_ptr, serv_list.head)
	{
		client_p = (client::client *)ptr->data;

		/* ok, if the client we're supposed to not send to has an
		 * ID, then we still want to issue the kill there..
		 */
		if(one != NULL && (client_p == one->from) &&
			(!has_id(client_p) || !has_id(target_p)))
			continue;

		_send_linebuf(client_p, &rb_linebuf_id);
	}

	rb_linebuf_donebuf(&rb_linebuf_id);
}

} // namespace ircd