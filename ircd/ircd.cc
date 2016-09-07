/*
 *  charybdis: A slightly useful ircd.
 *  ircd.c: Starts up and runs the ircd.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2008 ircd-ratbox development team
 *  Copyright (C) 2005-2013 charybdis development team
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

#include <boost/asio.hpp>

namespace ircd {

/* /quote set variables */
struct SetOptions GlobalSetOptions;

struct Counter Count;
struct ServerStatistics ServerStats;

struct ev_entry *check_splitmode_ev;

int maxconnections;
client::client me;                // That's me
struct LocalUser meLocalUser;     // That's also part of me

rb_dlink_list global_client_list;

/* unknown/client pointer lists */
rb_dlink_list unknown_list;        /* unknown clients ON this server only */
rb_dlink_list lclient_list;	/* local clients only ON this server */
rb_dlink_list serv_list;           /* local servers to this server ONLY */
rb_dlink_list global_serv_list;    /* global servers on the network */
rb_dlink_list local_oper_list;     /* our opers, duplicated in lclient_list */
rb_dlink_list oper_list;           /* network opers */

char * const *myargv;
volatile sig_atomic_t dorehash = false;
volatile sig_atomic_t dorehashbans = false;
volatile sig_atomic_t doremotd = false;
bool kline_queued = false;
bool server_state_foreground = true;
bool opers_see_all_users = false;
bool ircd_ssl_ok = false;
bool ircd_zlib_ok = true;

int testing_conf = 0;
//time_t startup_time;

int default_server_capabs;

int splitmode;
int splitchecking;
int split_users;
int split_servers;
int eob_count;

/*
static void
check_rehash(void *unused)
{
	if(dorehash)
	{
		rehash(true);
		dorehash = false;
	}

	if(dorehashbans)
	{
		rehash_bans();
		dorehashbans = false;
	}

	if(doremotd)
	{
		sendto_realops_snomask(sno::GENERAL, L_ALL,
				     "Got signal SIGUSR1, reloading ircd motd file");
		cache::motd::cache_user();
		doremotd = false;
	}
}
*/

/*
 * initalialize_global_set_options
 *
 * inputs       - none
 * output       - none
 * side effects - This sets all global set options needed
 */
static void
initialize_global_set_options(void)
{
/*
	memset(&GlobalSetOptions, 0, sizeof(GlobalSetOptions));
	// memset( &ConfigFileEntry, 0, sizeof(ConfigFileEntry));

	GlobalSetOptions.maxclients = ServerInfo.default_max_clients;

	if(GlobalSetOptions.maxclients > (maxconnections - MAX_BUFFER) || (GlobalSetOptions.maxclients <= 0))
		GlobalSetOptions.maxclients = maxconnections - MAX_BUFFER;

	GlobalSetOptions.autoconn = 1;

	GlobalSetOptions.spam_time = MIN_JOIN_LEAVE_TIME;
	GlobalSetOptions.spam_num = MAX_JOIN_LEAVE_COUNT;

	GlobalSetOptions.floodcount = ConfigFileEntry.default_floodcount;

	split_servers = ConfigChannel.default_split_server_count;
	split_users = ConfigChannel.default_split_user_count;

	if(split_users && split_servers
	   && (ConfigChannel.no_create_on_split || ConfigChannel.no_join_on_split))
	{
		splitmode = 1;
		splitchecking = 1;
	}

	GlobalSetOptions.ident_timeout = ConfigFileEntry.default_ident_timeout;

	rb_strlcpy(GlobalSetOptions.operstring,
		ConfigFileEntry.default_operstring,
		sizeof(GlobalSetOptions.operstring));
	rb_strlcpy(GlobalSetOptions.adminstring,
		ConfigFileEntry.default_adminstring,
		sizeof(GlobalSetOptions.adminstring));
*/
}


bool debugmode;
boost::asio::io_service *ios;

static void sigfd_handler(const boost::system::error_code &, int);
std::unique_ptr<boost::asio::signal_set> sigfd;

static void seed_random();
static void init_sigs();
static void init_sys();

} // namespace ircd

/*
 * This function sets up the IRCd, handlers, and then returns without blocking.
 * Pass your io_service instance, it will share it with the rest of your program.
 * An exception will be thrown on error.
 */
void
ircd::init(boost::asio::io_service &io_service,
           const std::string &configfile)
{
	ircd::ios = &io_service;

	init_sys();
	init_sigs();

	// The log is available, but it is console-only until conf opens files.
	log::init();
	log::mark("log started");

	conf::init(configfile);

	// initialise operhash fairly early.
	//init_operhash();

	//me.localClient = &meLocalUser;
	//rb_dlinkAddTail(&me, &me.node, &global_client_list);

	//init_builtin_capabs();
	//default_server_capabs = CAP_MASK;
	//newconf_init();
	//init_s_conf();
	//init_s_newconf();
	//init_hash();
	//init_host_hash();
	//client::init();
	//init_hook();
	//chan::init();
	//initclass();
	//whowas::init();
	//init_reject();
	//cache::init();
	//init_monitor();
	//chan::mode::init();
	//init_authd();		/* Start up authd. */
	//init_dns();		/* Start up DNS query system */

	//privilegeset_set_new("default", "", 0);

	//read_conf_files(true);	/* cold start init conf files */

	//mods::autoload();
	//supported::init();
	//init_bandb();
	//init_ssld();
	//init_wsockd();
	//rehash_bans();

	//default_server_capabs &= ~CAP_ZIP;  // Set up default_server_capabs
	//initialize_global_set_options();
/*
	if(ServerInfo.name == NULL)
		throw error("no server name specified in serverinfo block.");

	rb_strlcpy(me.name, ServerInfo.name, sizeof(me.name));

	if(ServerInfo.sid[0] == '\0')
		throw error("no server sid specified in serverinfo block.");

	rb_strlcpy(me.id, ServerInfo.sid, sizeof(me.id));
	//client::init_uid();

	// serverinfo{} description must exist.  If not, error out.
	if(ServerInfo.description == NULL)
		throw error("no server description specified in serverinfo block.");

	rb_strlcpy(me.info, ServerInfo.description, sizeof(me.info));
*/
	/*
	if(ServerInfo.ssl_cert != NULL)
	{
		// just do the rb_setup_ssl_server to validate the config
		if(!rb_setup_ssl_server(ServerInfo.ssl_cert, ServerInfo.ssl_private_key, ServerInfo.ssl_dh_params, ServerInfo.ssl_cipher_list))
		{
			ilog(L_MAIN, "WARNING: Unable to setup SSL.");
			ircd_ssl_ok = false;
		}
		else
			ircd_ssl_ok = true;
	}
	*/

	//me.from = &me;
	//me.servptr = &me;
	//set_me(me);
	//make_serv(me);
	//add_to_client_hash(me.name, &me);
	//add_to_id_hash(me.id, &me);
	//nameinfo(serv(me)) = cache::serv::connect(me.name, me.info);

	//rb_dlinkAddAlloc(&me, &global_serv_list);

	//check_class();
	//cache::help::load();

	//configure_authd();

	/* We want try_connections to be called as soon as possible now! -- adrian */
	/* No, 'cause after a restart it would cause all sorts of nick collides */
	/* um.  by waiting even longer, that just means we have even *more*
	 * nick collisions.  what a stupid idea. set an event for the IO loop --fl
	 */
	// rb_event_addish("try_connections", try_connections, NULL, STARTUP_CONNECTIONS_TIME);
	// rb_event_addonce("try_connections_startup", try_connections, NULL, 2);
	// rb_event_add("check_rehash", check_rehash, NULL, 3);
	// rb_event_addish("reseed_srand", seed_random, NULL, 300); /* reseed every 10 minutes */

	// if(splitmode)
	//	check_splitmode_ev = rb_event_add("check_splitmode", chan::check_splitmode, NULL, 5);

	log::info("IRCd initialization completed successfully.");
}

/*
 * init_sys
 *
 * inputs	- boot_daemon flag
 * output	- none
 * side effects	- if boot_daemon flag is not set, don't daemonize
 */
void
ircd::init_sys()
{

	/*
	 * Setup corefile size immediately after boot -kre
	 */
	#ifdef HAVE_SYS_RESOURCE_H
		struct rlimit rlim;	/* resource limits */

		/* Set corefilesize to maximum */
		if(!getrlimit(RLIMIT_CORE, &rlim))
		{
			rlim.rlim_cur = rlim.rlim_max;
			setrlimit(RLIMIT_CORE, &rlim);
		}
	#endif

	#if !defined(_WIN32) && defined(RLIMIT_NOFILE) && defined(HAVE_SYS_RESOURCE_H)
		struct rlimit limit;

		if(!getrlimit(RLIMIT_NOFILE, &limit))
			maxconnections = limit.rlim_cur;
		else
			maxconnections = MAXCONNECTIONS;
	#else
		maxconnections = MAXCONNECTIONS;
	#endif

	rb_set_time();
	rb_init_prng(NULL, RB_PRNG_DEFAULT);
	seed_random();
}

static bool
seed_with_urandom()
{
	unsigned int seed;
	int fd;

	fd = open("/dev/urandom", O_RDONLY);
	if(fd >= 0)
	{
		if(read(fd, &seed, sizeof(seed)) == sizeof(seed))
		{
			close(fd);
			srand(seed);
			return true;
		}
		close(fd);
	}
	return false;
}

static void
seed_with_clock()
{
 	const struct timeval *tv;
	rb_set_time();
	tv = rb_current_time_tv();
	srand(tv->tv_sec ^ (tv->tv_usec | (getpid() << 20)));
}

void
ircd::seed_random()
{
	unsigned int seed;
	if(rb_get_random(&seed, sizeof(seed)) == -1)
	{
		if(!seed_with_urandom())
			seed_with_clock();
		return;
	}
	srand(seed);
}

static void
sigfd_handle_sigusr2()
{
	// dorehashbans
	// doremotd
}

static void
sigfd_handle_sigusr1()
{
	// dorehash (no longer hangup!)
}

static void
sigfd_handle_sigquit()
{

}

static void
sigfd_handle_sigterm()
{

}

void
ircd::sigfd_handler(const boost::system::error_code &ec,
                    int signum)
{
	switch(ec.value())
	{
		using namespace boost::system::errc;

		case success:             break;
		case operation_canceled:  return;
		default:
			throw error("Signal handler (%d): %s",
			            signum,
			            ec.message().c_str());
	}

	switch(signum)
	{
		case SIGTERM:  sigfd_handle_sigterm();  return;
		case SIGQUIT:  sigfd_handle_sigquit();  return;
		case SIGUSR1:  sigfd_handle_sigusr1();  break;
		case SIGUSR2:  sigfd_handle_sigusr2();  break;
		default:                                break;
	}

	if(sigfd)
		sigfd->async_wait(sigfd_handler);
}

void
ircd::init_sigs()
{
	sigfd.reset(new boost::asio::signal_set(*ios));
	sigfd->add(SIGTERM);
	sigfd->add(SIGQUIT);
	sigfd->add(SIGUSR1);
	sigfd->add(SIGUSR2);
	sigfd->async_wait(sigfd_handler);
}