/*
 *  charybdis: A slightly useful ircd.
 *  chmode.c: channel mode management
 *
 * Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 * Copyright (C) 1996-2002 Hybrid Development Team
 * Copyright (C) 2002-2005 ircd-ratbox development team
 * Copyright (C) 2005-2006 charybdis development team
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

namespace mode = ircd::chan::mode;
using namespace ircd;

/* bitmasks for error returns, so we send once per call */
#define SM_ERR_NOTS             0x00000001	/* No TS on channel */
#define SM_ERR_NOOPS            0x00000002	/* No chan ops */
#define SM_ERR_UNKNOWN          0x00000004
#define SM_ERR_RPL_C            0x00000008
#define SM_ERR_RPL_B            0x00000010
#define SM_ERR_RPL_E            0x00000020
#define SM_ERR_NOTONCHANNEL     0x00000040	/* Not on channel */
#define SM_ERR_RPL_I            0x00000100
#define SM_ERR_RPL_D            0x00000200
#define SM_ERR_NOPRIVS          0x00000400
#define SM_ERR_RPL_Q            0x00000800
#define SM_ERR_RPL_F            0x00001000
#define SM_ERR_MLOCK            0x00002000

#define MAXMODES_SIMPLE 46 /* a-zA-Z except bqeIov */

static int mode_count;
static int mode_limit;
static int mode_limit_simple;
static int removed_mask_pos;
static int h_get_channel_access;
mode::change mode_changes[BUFSIZE];

mode::mode mode::table[256];
char mode::arity[2][256];          // RPL_MYINFO (note that [0] is for 0 OR MORE params)
char mode::categories[4][256];     // RPL_ISUPPORT classification

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wchar-subscripts"
static void
table_init()
{
	using namespace mode;
	using namespace functor;

	// Leading tab only please:
	// <tab>mode::table['X'] = {<sp>handler,<sp>class,<sp>flag<sp>};

	table['C'] = { type(0),      category::D,  nullptr   };
	table['F'] = { FREETARGET,   category::D,  simple    };
	table['I'] = { INVEX,        category::A,  ban       };
	table['L'] = { EXLIMIT,      category::D,  staff     };
	table['P'] = { PERMANENT,    category::D,  staff     };
	table['Q'] = { DISFORWARD,   category::D,  simple    };
	table['b'] = { BAN,          category::A,  ban       };
	table['e'] = { EXCEPTION,    category::A,  ban       };
	table['f'] = { type(0),      category::C,  forward   };
	table['g'] = { FREEINVITE,   category::D,  simple    };
	table['i'] = { INVITEONLY,   category::D,  simple    };
	table['j'] = { type(0),      category::C,  throttle  };
	table['k'] = { type(0),      category::B,  key       };
	table['l'] = { type(0),      category::C,  limit     };
	table['m'] = { MODERATED,    category::D,  simple    };
	table['n'] = { NOPRIVMSGS,   category::D,  simple    };
	table['o'] = { type(0),      category::B,  op        };
	table['p'] = { PRIVATE,      category::D,  simple    };
	table['q'] = { QUIET,        category::A,  ban       };
	table['r'] = { REGONLY,      category::D,  simple    };
	table['s'] = { SECRET,       category::D,  simple    };
	table['t'] = { TOPICLIMIT,   category::D,  simple    };
	table['v'] = { type(0),      category::B,  voice     };
	table['z'] = { OPMODERATE,   category::D,  simple    };
}
#pragma GCC diagnostic pop

/* OPTIMIZE ME! -- dwr
 * sure! --jzk */
void
chan::mode::init()
{
	table_init();

	categories[uint(category::A)][0] = '\0';
	categories[uint(category::B)][0] = '\0';
	categories[uint(category::C)][0] = '\0';
	categories[uint(category::D)][0] = '\0';

	arity[0][0] = '\0';
	arity[1][0] = '\0';

	/* Filter out anything disabled by the configuraton */
	unsigned long disabled = 0;
	if (!ConfigChannel.use_invex)    disabled |= INVEX;
	if (!ConfigChannel.use_except)   disabled |= EXCEPTION;
	if (!ConfigChannel.use_forward)  disabled |= FREETARGET | DISFORWARD;

	/* Construct the chmode data */
	for (size_t i = 0; i < 256; i++)
	{
		if (!table[i].set_func)
		{
			table[i].set_func = functor::nosuch;
			table[i].category = category::D;
			continue;
		}

		if (table[i].type & disabled || table[i].set_func == functor::nosuch)
			continue;

		const char ch[2] = { char(i), '\0' };
		const auto &cat(table[i].category);
		rb_strlcat(categories[uint(cat)], ch, 256);
		rb_strlcat(arity[0], ch, 256);
	}

	/* Any non-category::D mode has parameters, this sets up RPL_MYINFO */
	for (size_t i = 0; i < 3; i++)
		rb_strlcat(arity[1], categories[i], 256);
}

/*
 * find_umode_slot
 *
 * inputs       - NONE
 * outputs      - an available cflag bitmask or
 *                0 if no cflags are available
 * side effects - NONE
 */
static mode::type
find_cflag_slot(void)
{
	using namespace mode;

	unsigned int all_cflags = 0, my_cflag = 0, i;

	for (i = 0; i < 256; i++)
		all_cflags |= table[i].type;

	for (my_cflag = 1; my_cflag && (all_cflags & my_cflag);
		my_cflag <<= 1);

	return type(my_cflag);
}

mode::type
mode::add(const uint8_t &c,
          const category &category,
          const func &set_func)
{
	if(table[c].set_func &&
	   table[c].set_func != functor::nosuch &&
	   table[c].set_func != functor::orphaned)
		return type(0);

	if(table[c].set_func == functor::nosuch)
		table[c].type = find_cflag_slot();

	if(!table[c].type)
		return type(0);

	table[c].category = category;
	table[c].set_func = set_func;
	init();

	return table[c].type;
}

void
mode::orphan(const uint8_t &c)
{
	s_assert(table[c].type != 0);
	table[c].set_func = functor::orphaned;
	init();
}

namespace ircd {

int
chan::get_channel_access(client::client *source_p, chan *chptr, membership *msptr, int dir, const char *modestr)
{
	hook_data_channel_approval moduledata;

	if(!my(*source_p))
		return CHANOP;

	moduledata.client = source_p;
	moduledata.chptr = chptr;
	moduledata.msptr = msptr;
	moduledata.target = NULL;
	moduledata.approved = (msptr != NULL && is_chanop(msptr)) ? CHANOP : PEON;
	moduledata.dir = dir;
	moduledata.modestr = modestr;

	call_hook(h_get_channel_access, &moduledata);

	return moduledata.approved;
}

/* allow_mode_change()
 *
 * Checks if mlock and chanops permit a mode change.
 *
 * inputs	- client, channel, access level, errors pointer, mode char
 * outputs	- false on failure, true on success
 * side effects - error message sent on failure
 */
static bool
allow_mode_change(client::client *source_p, chan::chan *chptr, int alevel,
		int *errors, char c)
{
	/* If this mode char is locked, don't allow local users to change it. */
	if (my(*source_p) && !chptr->mode_lock.empty() && strchr(chptr->mode_lock.c_str(), c))
	{
		if (!(*errors & SM_ERR_MLOCK))
			sendto_one_numeric(source_p,
					ERR_MLOCKRESTRICTED,
					form_str(ERR_MLOCKRESTRICTED),
					chptr->name.c_str(),
					c,
					chptr->mode_lock.c_str());
		*errors |= SM_ERR_MLOCK;
		return false;
	}
	if(alevel < chan::CHANOP)
	{
		if(!(*errors & SM_ERR_NOOPS))
			sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
				   me.name, source_p->name, chptr->name.c_str());
		*errors |= SM_ERR_NOOPS;
		return false;
	}
	return true;
}

/* check_forward()
 *
 * input	- client, channel to set mode on, target channel name
 * output	- true if forwarding should be allowed
 * side effects - numeric sent if not allowed
 */
static bool
check_forward(client::client *source_p, chan::chan *chptr, const char *forward)
{
	chan::chan *targptr = NULL;
	chan::membership *msptr;

	if(!chan::valid_name(forward) ||
			(my(*source_p) && (strlen(forward) > LOC_CHANNELLEN || hash_find_resv(forward))))
	{
		sendto_one_numeric(source_p, ERR_BADCHANNAME, form_str(ERR_BADCHANNAME), forward);
		return false;
	}
	/* don't forward to inconsistent target -- jilles */
	if(chptr->name[0] == '#' && forward[0] == '&')
	{
		sendto_one_numeric(source_p, ERR_BADCHANNAME,
				   form_str(ERR_BADCHANNAME), forward);
		return false;
	}
	if(my(*source_p) && (targptr = chan::get(forward, std::nothrow)) == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL,
				   form_str(ERR_NOSUCHCHANNEL), forward);
		return false;
	}
	if(my(*source_p) && !(targptr->mode.mode & mode::FREETARGET))
	{
		auto *const msptr(get(targptr->members, *source_p, std::nothrow));
		if(!msptr || get_channel_access(source_p, targptr, msptr, MODE_QUERY, NULL) < chan::CHANOP)
		{
			sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
			           me.name,
			           source_p->name,
			           targptr->name.c_str());
			return false;
		}
	}
	return true;
}

/* fix_key()
 *
 * input	- key to fix
 * output	- the same key, fixed
 * side effects - anything below ascii 13 is discarded, ':' discarded,
 *                high ascii is dropped to lower half of ascii table
 */
static char *
fix_key(char *arg)
{
	unsigned char *s, *t, c;

	for(s = t = (unsigned char *) arg; (c = *s); s++)
	{
		c &= 0x7f;
		if(c != ':' && c != ',' && c > ' ')
			*t++ = c;
	}

	*t = '\0';
	return arg;
}

/* fix_key_remote()
 *
 * input	- key to fix
 * ouput	- the same key, fixed
 * side effects - high ascii dropped to lower half of table,
 *                CR/LF/':' are dropped
 */
static char *
fix_key_remote(char *arg)
{
	unsigned char *s, *t, c;

	for(s = t = (unsigned char *) arg; (c = *s); s++)
	{
		c &= 0x7f;
		if((c != 0x0a) && (c != ':') && (c != ',') && (c != 0x0d) && (c != ' '))
			*t++ = c;
	}

	*t = '\0';
	return arg;
}

/* chm_*()
 *
 * The handlers for each specific mode.
 */
void
mode::functor::nosuch(client::client *source_p, chan *chptr,
	   int alevel, int parc, int *parn,
	   const char **parv, int *errors, int dir, char c, type mode_type)
{
	if(*errors & SM_ERR_UNKNOWN)
		return;
	*errors |= SM_ERR_UNKNOWN;
	sendto_one(source_p, form_str(ERR_UNKNOWNMODE), me.name, source_p->name, c);
}

void
mode::functor::simple(client::client *source_p, chan *chptr,
	   int alevel, int parc, int *parn,
	   const char **parv, int *errors, int dir, char c, type mode_type)
{
	if(!allow_mode_change(source_p, chptr, alevel, errors, c))
		return;

	if(my(*source_p) && (++mode_limit_simple > MAXMODES_SIMPLE))
		return;

	/* setting + */
	if((dir == MODE_ADD) && !(chptr->mode.mode & mode_type))
	{
		/* if +f is disabled, ignore an attempt to set +QF locally */
		if(!ConfigChannel.use_forward && my(*source_p) &&
				(c == 'Q' || c == 'F'))
			return;

		chptr->mode.mode |= mode_type;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count++].arg = NULL;
	}
	else if((dir == MODE_DEL) && (chptr->mode.mode & mode_type))
	{
		chptr->mode.mode &= ~mode_type;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = NULL;
	}
}

void
mode::functor::orphaned(client::client *source_p, chan *chptr,
	   int alevel, int parc, int *parn,
	   const char **parv, int *errors, int dir, char c, type mode_type)
{
	if(my(*source_p))
		return;

	if((dir == MODE_ADD) && !(chptr->mode.mode & mode_type))
	{
		chptr->mode.mode |= mode_type;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count++].arg = NULL;
	}
	else if((dir == MODE_DEL) && (chptr->mode.mode & mode_type))
	{
		chptr->mode.mode &= ~mode_type;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = NULL;
	}
}

void
mode::functor::hidden(client::client *source_p, chan *chptr,
	  int alevel, int parc, int *parn,
	  const char **parv, int *errors, int dir, char c, type mode_type)
{
	if(!is(*source_p, umode::OPER) && !is_server(*source_p))
	{
		if(!(*errors & SM_ERR_NOPRIVS))
			sendto_one_numeric(source_p, ERR_NOPRIVILEGES, form_str(ERR_NOPRIVILEGES));
		*errors |= SM_ERR_NOPRIVS;
		return;
	}
	if(my(*source_p) && !IsOperAdmin(source_p))
	{
		if(!(*errors & SM_ERR_NOPRIVS))
			sendto_one(source_p, form_str(ERR_NOPRIVS), me.name,
					source_p->name, "admin");
		*errors |= SM_ERR_NOPRIVS;
		return;
	}

	if(my(*source_p) && (++mode_limit_simple > MAXMODES_SIMPLE))
		return;

	/* setting + */
	if((dir == MODE_ADD) && !(chptr->mode.mode & mode_type))
	{
		chptr->mode.mode |= mode_type;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count].mems = ONLY_OPERS;
		mode_changes[mode_count++].arg = NULL;
	}
	else if((dir == MODE_DEL) && (chptr->mode.mode & mode_type))
	{
		chptr->mode.mode &= ~mode_type;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].mems = ONLY_OPERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = NULL;
	}
}

void
mode::functor::staff(client::client *source_p, chan *chptr,
	  int alevel, int parc, int *parn,
	  const char **parv, int *errors, int dir, char c, type mode_type)
{
	if(!is(*source_p, umode::OPER) && !is_server(*source_p))
	{
		if(!(*errors & SM_ERR_NOPRIVS))
			sendto_one_numeric(source_p, ERR_NOPRIVILEGES, form_str(ERR_NOPRIVILEGES));
		*errors |= SM_ERR_NOPRIVS;
		return;
	}
	if(my(*source_p) && !IsOperResv(source_p))
	{
		if(!(*errors & SM_ERR_NOPRIVS))
			sendto_one(source_p, form_str(ERR_NOPRIVS), me.name,
					source_p->name, "resv");
		*errors |= SM_ERR_NOPRIVS;
		return;
	}

	if(!allow_mode_change(source_p, chptr, CHANOP, errors, c))
		return;

	if(my(*source_p) && (++mode_limit_simple > MAXMODES_SIMPLE))
		return;

	/* setting + */
	if((dir == MODE_ADD) && !(chptr->mode.mode & mode_type))
	{
		chptr->mode.mode |= mode_type;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count++].arg = NULL;
	}
	else if((dir == MODE_DEL) && (chptr->mode.mode & mode_type))
	{
		chptr->mode.mode &= ~mode_type;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = NULL;
	}
}

void
mode::functor::ban(client::client *source_p, chan *chptr,
	int alevel, int parc, int *parn,
	const char **parv, int *errors, int dir, char c, type mode_type)
{
	namespace chan = ircd::chan;

	const char *mask, *raw_mask;
	char *forward;
	chan::list *list;
	int errorval;
	const char *rpl_list_p;
	const char *rpl_endlist_p;
	int mems;

	switch (mode_type)
	{
	case BAN:
		list = &get(*chptr, mode_type);
		errorval = SM_ERR_RPL_B;
		rpl_list_p = form_str(RPL_BANLIST);
		rpl_endlist_p = form_str(RPL_ENDOFBANLIST);
		mems = ALL_MEMBERS;
		break;

	case EXCEPTION:
		/* if +e is disabled, allow all but +e locally */
		if(!ConfigChannel.use_except && my(*source_p) &&
		   ((dir == MODE_ADD) && (parc > *parn)))
			return;

		list = &get(*chptr, mode_type);
		errorval = SM_ERR_RPL_E;
		rpl_list_p = form_str(RPL_EXCEPTLIST);
		rpl_endlist_p = form_str(RPL_ENDOFEXCEPTLIST);

		if(ConfigChannel.use_except || (dir == MODE_DEL))
			mems = ONLY_CHANOPS;
		else
			mems = ONLY_SERVERS;
		break;

	case INVEX:
		/* if +I is disabled, allow all but +I locally */
		if(!ConfigChannel.use_invex && my(*source_p) &&
		   (dir == MODE_ADD) && (parc > *parn))
			return;

		list = &get(*chptr, mode_type);
		errorval = SM_ERR_RPL_I;
		rpl_list_p = form_str(RPL_INVITELIST);
		rpl_endlist_p = form_str(RPL_ENDOFINVITELIST);

		if(ConfigChannel.use_invex || (dir == MODE_DEL))
			mems = ONLY_CHANOPS;
		else
			mems = ONLY_SERVERS;
		break;

	case QUIET:
		list = &get(*chptr, mode_type);
		errorval = SM_ERR_RPL_Q;
		rpl_list_p = form_str(RPL_QUIETLIST);
		rpl_endlist_p = form_str(RPL_ENDOFQUIETLIST);
		mems = ALL_MEMBERS;
		break;

	default:
		sendto_realops_snomask(sno::GENERAL, L_ALL, "chm_ban() called with unknown type!");
		return;
	}

	if(dir == 0 || parc <= *parn)
	{
		if((*errors & errorval) != 0)
			return;
		*errors |= errorval;

		/* non-ops cant see +eI lists.. */
		/* note that this is still permitted if +e/+I are mlocked. */
		if(alevel < CHANOP && mode_type != BAN &&
				mode_type != QUIET)
		{
			if(!(*errors & SM_ERR_NOOPS))
				sendto_one(source_p, form_str(ERR_CHANOPRIVSNEEDED),
					   me.name, source_p->name, chptr->name.c_str());
			*errors |= SM_ERR_NOOPS;
			return;
		}

		for (const auto &ban : *list)
		{
			char buf[ban::LEN];
			if(!ban.forward.empty())
				snprintf(buf, sizeof(buf), "%s$%s", ban.banstr.c_str(), ban.forward.c_str());
			else
				rb_strlcpy(buf, ban.banstr.c_str(), sizeof(buf));

			sendto_one(source_p, rpl_list_p,
			           me.name,
			           source_p->name,
			           chptr->name.c_str(),
			           buf,
			           ban.who.c_str(),
			           ban.when);
		}

		sendto_one(source_p, rpl_endlist_p, me.name, source_p->name, chptr->name.c_str());
		return;
	}

	if(!allow_mode_change(source_p, chptr, alevel, errors, c))
		return;


	if(my(*source_p) && (++mode_limit > MAXPARAMS))
		return;

	raw_mask = parv[(*parn)];
	(*parn)++;

	/* empty ban, or starts with ':' which messes up s2s, ignore it */
	if(EmptyString(raw_mask) || *raw_mask == ':')
		return;

	if(!my(*source_p))
	{
		if(strchr(raw_mask, ' '))
			return;

		mask = raw_mask;
	}
	else
		mask = pretty_mask(raw_mask);

	/* we'd have problems parsing this, hyb6 does it too
	 * also make sure it will always fit on a line with channel
	 * name etc.
	 */
	if(strlen(mask) > MIN(ban::LEN, BUFLEN - 5))
	{
		sendto_one_numeric(source_p, ERR_INVALIDBAN,
				form_str(ERR_INVALIDBAN),
				chptr->name.c_str(), c, raw_mask);
		return;
	}

	/* Look for a $ after the first character.
	 * As the first character, it marks an extban; afterwards
	 * it delimits a forward channel.
	 */
	if ((forward = (char *)strchr(mask+1, '$')) != NULL)
	{
		*forward++ = '\0';
		if (*forward == '\0')
			forward = NULL;
	}

	/* if we're adding a NEW id */
	if(dir == MODE_ADD)
	{
		if (*mask == '$' && my(*source_p))
		{
			if (!valid_extban(mask, source_p, chptr, mode_type))
			{
				sendto_one_numeric(source_p, ERR_INVALIDBAN,
						form_str(ERR_INVALIDBAN),
						chptr->name.c_str(), c, raw_mask);
				return;
			}
		}

		/* For compatibility, only check the forward channel from
		 * local clients. Accept any forward channel from servers.
		 */
		if(forward != NULL && my(*source_p))
		{
			/* For simplicity and future flexibility, do not
			 * allow '$' in forwarding targets.
			 */
			if(!ConfigChannel.use_forward ||
					strchr(forward, '$') != NULL)
			{
				sendto_one_numeric(source_p, ERR_INVALIDBAN,
						form_str(ERR_INVALIDBAN),
						chptr->name.c_str(), c, raw_mask);
				return;
			}
			/* check_forward() sends its own error message */
			if(!check_forward(source_p, chptr, forward))
				return;
			/* Forwards only make sense for bans. */
			if(mode_type != BAN)
			{
				sendto_one_numeric(source_p, ERR_INVALIDBAN,
						form_str(ERR_INVALIDBAN),
						chptr->name.c_str(), c, raw_mask);
				return;
			}
		}

		/* dont allow local clients to overflow the banlist, dont
		 * let remote servers set duplicate bans
		 */
		if(!add(*chptr, mode_type, mask, *source_p, forward?: std::string{}))
			return;

		if(forward)
			forward[-1]= '$';

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].mems = mems;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = mask;
	}
	else if(dir == MODE_DEL)
	{
		// When this whole function gets hosed all of this will be better

		static char buf[ban::LEN * MAXPARAMS];
		const int old_removed_mask_pos(removed_mask_pos);
		std::string _mask(mask);

		auto it(list->find(_mask));
		if (it == end(*list))
		{
			_mask = raw_mask;
			it = list->find(_mask);
		}

		if (it != end(*list))
		{
			const auto &ban(*it);
			if (!ban.forward.empty())
				removed_mask_pos += snprintf(buf + old_removed_mask_pos, sizeof(buf), "%s$%s", ban.banstr.c_str(), ban.forward.c_str()) + 1;
			else
				removed_mask_pos += rb_strlcpy(buf + old_removed_mask_pos, mask, sizeof(buf)) + 1;

			del(*chptr, mode_type, _mask);
		}

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].mems = mems;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = buf + old_removed_mask_pos;
	}
}

void
mode::functor::op(client::client *source_p, chan *chptr,
       int alevel, int parc, int *parn,
       const char **parv, int *errors, int dir, char c, type mode_type)
{
	membership *mstptr;
	const char *opnick;
	client::client *targ_p;

	if(!allow_mode_change(source_p, chptr, alevel, errors, c))
		return;

	if((dir == MODE_QUERY) || (parc <= *parn))
		return;

	opnick = parv[(*parn)];
	(*parn)++;

	/* empty nick */
	if(EmptyString(opnick))
	{
		sendto_one_numeric(source_p, ERR_NOSUCHNICK, form_str(ERR_NOSUCHNICK), "*");
		return;
	}

	if((targ_p = find_chasing(source_p, opnick, NULL)) == NULL)
	{
		return;
	}

	mstptr = get(chptr->members, *targ_p, std::nothrow);

	if(mstptr == NULL)
	{
		if(!(*errors & SM_ERR_NOTONCHANNEL) && my(*source_p))
			sendto_one_numeric(source_p, ERR_USERNOTINCHANNEL,
					   form_str(ERR_USERNOTINCHANNEL), opnick, chptr->name.c_str());
		*errors |= SM_ERR_NOTONCHANNEL;
		return;
	}

	if(my(*source_p) && (++mode_limit > MAXPARAMS))
		return;

	if(dir == MODE_ADD)
	{
		if(targ_p == source_p && mstptr->flags & CHANOP)
			return;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = targ_p->id;
		mode_changes[mode_count++].arg = targ_p->name;

		mstptr->flags |= CHANOP;
	}
	else
	{
		if(my(*source_p) && is(*targ_p, umode::SERVICE))
		{
			sendto_one(source_p, form_str(ERR_ISCHANSERVICE),
				   me.name, source_p->name, targ_p->name, chptr->name.c_str());
			return;
		}

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = targ_p->id;
		mode_changes[mode_count++].arg = targ_p->name;

		mstptr->flags &= ~CHANOP;
	}
}

void
mode::functor::voice(client::client *source_p, chan *chptr,
	  int alevel, int parc, int *parn,
	  const char **parv, int *errors, int dir, char c, type mode_type)
{
	membership *mstptr;
	const char *opnick;
	client::client *targ_p;

	if(!allow_mode_change(source_p, chptr, alevel, errors, c))
		return;

	if((dir == MODE_QUERY) || parc <= *parn)
		return;

	opnick = parv[(*parn)];
	(*parn)++;

	/* empty nick */
	if(EmptyString(opnick))
	{
		sendto_one_numeric(source_p, ERR_NOSUCHNICK, form_str(ERR_NOSUCHNICK), "*");
		return;
	}

	if((targ_p = find_chasing(source_p, opnick, NULL)) == NULL)
	{
		return;
	}

	mstptr = get(chptr->members, *targ_p, std::nothrow);

	if(mstptr == NULL)
	{
		if(!(*errors & SM_ERR_NOTONCHANNEL) && my(*source_p))
			sendto_one_numeric(source_p, ERR_USERNOTINCHANNEL,
					   form_str(ERR_USERNOTINCHANNEL), opnick, chptr->name.c_str());
		*errors |= SM_ERR_NOTONCHANNEL;
		return;
	}

	if(my(*source_p) && (++mode_limit > MAXPARAMS))
		return;

	if(dir == MODE_ADD)
	{
		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = targ_p->id;
		mode_changes[mode_count++].arg = targ_p->name;

		mstptr->flags |= VOICE;
	}
	else
	{
		mode_changes[mode_count].letter = 'v';
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = targ_p->id;
		mode_changes[mode_count++].arg = targ_p->name;

		mstptr->flags &= ~VOICE;
	}
}

void
mode::functor::limit(client::client *source_p, chan *chptr,
	  int alevel, int parc, int *parn,
	  const char **parv, int *errors, int dir, char c, type mode_type)
{
	const char *lstr;
	static char limitstr[30];
	int limit;

	if(!allow_mode_change(source_p, chptr, alevel, errors, c))
		return;

	if(dir == MODE_QUERY)
		return;

	if(my(*source_p) && (++mode_limit_simple > MAXMODES_SIMPLE))
		return;

	if((dir == MODE_ADD) && parc > *parn)
	{
		lstr = parv[(*parn)];
		(*parn)++;

		if(EmptyString(lstr) || (limit = atoi(lstr)) <= 0)
			return;

		sprintf(limitstr, "%d", limit);

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = limitstr;

		chptr->mode.limit = limit;
	}
	else if(dir == MODE_DEL)
	{
		if(!chptr->mode.limit)
			return;

		chptr->mode.limit = 0;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = NULL;
	}
}

void
mode::functor::throttle(client::client *source_p, chan *chptr,
	     int alevel, int parc, int *parn,
	     const char **parv, int *errors, int dir, char c, type mode_type)
{
	int joins = 0, timeslice = 0;

	if(!allow_mode_change(source_p, chptr, alevel, errors, c))
		return;

	if(dir == MODE_QUERY)
		return;

	if(my(*source_p) && (++mode_limit_simple > MAXMODES_SIMPLE))
		return;

	if((dir == MODE_ADD) && parc > *parn)
	{
		if (sscanf(parv[(*parn)], "%d:%d", &joins, &timeslice) < 2)
			return;

		if(joins <= 0 || timeslice <= 0)
			return;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = parv[(*parn)];

		(*parn)++;

		chptr->mode.join_num = joins;
		chptr->mode.join_time = timeslice;
	}
	else if(dir == MODE_DEL)
	{
		if(!chptr->mode.join_num)
			return;

		chptr->mode.join_num = 0;
		chptr->mode.join_time = 0;
		chptr->join_count = 0;
		chptr->join_delta = 0;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = NULL;
	}
}

void
mode::functor::forward(client::client *source_p, chan *chptr,
	int alevel, int parc, int *parn,
	const char **parv, int *errors, int dir, char c, type mode_type)
{
	const char *forward;

	/* if +f is disabled, ignore local attempts to set it */
	if(!ConfigChannel.use_forward && my(*source_p) &&
	   (dir == MODE_ADD) && (parc > *parn))
		return;

	if(dir == MODE_QUERY || (dir == MODE_ADD && parc <= *parn))
	{
		if (!(*errors & SM_ERR_RPL_F))
		{
			if (*chptr->mode.forward == '\0')
				sendto_one_notice(source_p, ":%s has no forward channel", chptr->name.c_str());
			else
				sendto_one_notice(source_p, ":%s forward channel is %s", chptr->name.c_str(), chptr->mode.forward);
			*errors |= SM_ERR_RPL_F;
		}
		return;
	}

#ifndef FORWARD_OPERONLY
	if(!allow_mode_change(source_p, chptr, alevel, errors, c))
		return;
#else
	if(!is(*source_p, umode::OPER) && !is_server(*source_p))
	{
		if(!(*errors & SM_ERR_NOPRIVS))
			sendto_one_numeric(source_p, ERR_NOPRIVILEGES, form_str(ERR_NOPRIVILEGES));
		*errors |= SM_ERR_NOPRIVS;
		return;
	}
#endif

	if(my(*source_p) && (++mode_limit_simple > MAXMODES_SIMPLE))
		return;

	if(dir == MODE_ADD && parc > *parn)
	{
		forward = parv[(*parn)];
		(*parn)++;

		if(EmptyString(forward))
			return;

		if(!check_forward(source_p, chptr, forward))
			return;

		rb_strlcpy(chptr->mode.forward, forward, sizeof(chptr->mode.forward));

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].mems =
			ConfigChannel.use_forward ? ALL_MEMBERS : ONLY_SERVERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = forward;
	}
	else if(dir == MODE_DEL)
	{
		if(!(*chptr->mode.forward))
			return;

		*chptr->mode.forward = '\0';

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = NULL;
	}
}

void
mode::functor::key(client::client *source_p, chan *chptr,
	int alevel, int parc, int *parn,
	const char **parv, int *errors, int dir, char c, type mode_type)
{
	char *key;

	if(!allow_mode_change(source_p, chptr, alevel, errors, c))
		return;

	if(dir == MODE_QUERY)
		return;

	if(my(*source_p) && (++mode_limit_simple > MAXMODES_SIMPLE))
		return;

	if((dir == MODE_ADD) && parc > *parn)
	{
		key = LOCAL_COPY(parv[(*parn)]);
		(*parn)++;

		if(my(*source_p))
			fix_key(key);
		else
			fix_key_remote(key);

		if(EmptyString(key))
			return;

		s_assert(key[0] != ' ');
		rb_strlcpy(chptr->mode.key, key, sizeof(chptr->mode.key));

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = chptr->mode.key;
	}
	else if(dir == MODE_DEL)
	{
		static char splat[] = "*";
		int i;

		if(parc > *parn)
			(*parn)++;

		if(!(*chptr->mode.key))
			return;

		/* hack time.  when we get a +k-k mode, the +k arg is
		 * chptr->mode.key, which the -k sets to \0, so hunt for a
		 * +k when we get a -k, and set the arg to splat. --anfl
		 */
		for(i = 0; i < mode_count; i++)
		{
			if(mode_changes[i].letter == 'k' && mode_changes[i].dir == MODE_ADD)
				mode_changes[i].arg = splat;
		}

		*chptr->mode.key = 0;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = "*";
	}
}

/* set_channel_mode()
 *
 * inputs	- client, source, channel, membership pointer, params
 * output	-
 * side effects - channel modes/memberships are changed, MODE is issued
 *
 * Extensively modified to be hotpluggable, 03/09/06 -- nenolod
 */
void
chan::set_channel_mode(client::client *client_p, client::client *source_p,
                 chan *chptr, membership *msptr, int parc, const char *parv[])
{
	static char modebuf[BUFSIZE];
	static char parabuf[BUFSIZE];
	char *mbuf;
	char *pbuf;
	int cur_len, mlen, paralen, paracount, arglen, len;
	int i, j, flags;
	int dir = MODE_QUERY;
	int parn = 1;
	int errors = 0;
	int alevel;
	const char *ml = parv[0];
	char c;
	client::client *fakesource_p;
	int reauthorized = 0;	/* if we change from MODE_QUERY to MODE_ADD/MODE_DEL, then reauth once, ugly but it works */
	int flags_list[3] = { ALL_MEMBERS, ONLY_CHANOPS, ONLY_OPERS };

	removed_mask_pos = 0;
	mode_count = 0;
	mode_limit = 0;
	mode_limit_simple = 0;

	/* Hide connecting server on netburst -- jilles */
	if (ConfigServerHide.flatten_links && is_server(*source_p) && !has_id(source_p) && !has_sent_eob(*source_p))
		fakesource_p = &me;
	else
		fakesource_p = source_p;

	alevel = get_channel_access(source_p, chptr, msptr, dir, reconstruct_parv(parc, parv));

	for(; (c = *ml) != 0; ml++)
	{
		switch (c)
		{
		case '+':
			dir = MODE_ADD;
			if (!reauthorized)
			{
				alevel = get_channel_access(source_p, chptr, msptr, dir, reconstruct_parv(parc, parv));
				reauthorized = 1;
			}
			break;
		case '-':
			dir = MODE_DEL;
			if (!reauthorized)
			{
				alevel = get_channel_access(source_p, chptr, msptr, dir, reconstruct_parv(parc, parv));
				reauthorized = 1;
			}
			break;
		case '=':
			dir = MODE_QUERY;
			break;
		default:
			mode::table[uint8_t(c)].set_func(fakesource_p, chptr, alevel, parc, &parn, parv, &errors, dir, c, mode::table[uint8_t(c)].type);
			break;
		}
	}

	/* bail out if we have nothing to do... */
	if(!mode_count)
		return;

	if(is_server(*source_p))
		mlen = sprintf(modebuf, ":%s MODE %s ", fakesource_p->name, chptr->name.c_str());
	else
		mlen = sprintf(modebuf, ":%s!%s@%s MODE %s ",
				  source_p->name, source_p->username,
				  source_p->host, chptr->name.c_str());

	for(j = 0; j < 3; j++)
	{
		flags = flags_list[j];
		cur_len = mlen;
		mbuf = modebuf + mlen;
		pbuf = parabuf;
		parabuf[0] = '\0';
		paracount = paralen = 0;
		dir = MODE_QUERY;

		for(i = 0; i < mode_count; i++)
		{
			if(mode_changes[i].letter == 0 || mode_changes[i].mems != flags)
				continue;

			if(mode_changes[i].arg != NULL)
			{
				arglen = strlen(mode_changes[i].arg);

				if(arglen > mode::BUFLEN - 5)
					continue;
			}
			else
				arglen = 0;

			/* if we're creeping over mode::MAXPARAMSSERV, or over
			 * bufsize (4 == +/-,modechar,two spaces) send now.
			 */
			if(mode_changes[i].arg != NULL &&
			   ((paracount == mode::MAXPARAMSSERV) ||
			    ((cur_len + paralen + arglen + 4) > (BUFSIZE - 3))))
			{
				*mbuf = '\0';

				if(cur_len > mlen)
					sendto_channel_local(flags, chptr, "%s %s", modebuf,
							     parabuf);
				else
					continue;

				paracount = paralen = 0;
				cur_len = mlen;
				mbuf = modebuf + mlen;
				pbuf = parabuf;
				parabuf[0] = '\0';
				dir = MODE_QUERY;
			}

			if(dir != mode_changes[i].dir)
			{
				*mbuf++ = (mode_changes[i].dir == MODE_ADD) ? '+' : '-';
				cur_len++;
				dir = mode_changes[i].dir;
			}

			*mbuf++ = mode_changes[i].letter;
			cur_len++;

			if(mode_changes[i].arg != NULL)
			{
				paracount++;
				len = sprintf(pbuf, "%s ", mode_changes[i].arg);
				pbuf += len;
				paralen += len;
			}
		}

		if(paralen && parabuf[paralen - 1] == ' ')
			parabuf[paralen - 1] = '\0';

		*mbuf = '\0';
		if(cur_len > mlen)
			sendto_channel_local(flags, chptr, "%s %s", modebuf, parabuf);
	}

	/* only propagate modes originating locally, or if we're hubbing */
	if(my(*source_p) || rb_dlink_list_length(&serv_list) > 1)
		send_cap_mode_changes(client_p, source_p, chptr, mode_changes, mode_count);
}

/* set_channel_mlock()
 *
 * inputs	- client, source, channel, params
 * output	-
 * side effects - channel mlock is changed / MLOCK is propagated
 */
void
chan::set_channel_mlock(client::client *client_p, client::client *source_p,
		  chan *chptr, const char *newmlock, bool propagate)
{
	chptr->mode_lock = newmlock?: std::string{};

	if (propagate)
	{
		sendto_server(client_p, NULL, CAP_TS6 | CAP_MLOCK, NOCAPS, ":%s MLOCK %ld %s :%s",
			      source_p->id, (long) chptr->channelts, chptr->name.c_str(),
			      chptr->mode_lock.c_str());
	}
}

} // namespace ircd