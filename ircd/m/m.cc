// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

decltype(ircd::m::log)
ircd::m::log
{
	"matrix", 'm'
};

decltype(ircd::m::listeners)
ircd::m::listeners
{};

//
// init
//

ircd::conf::item<std::string>
me_online_status_msg
{
	{ "name",     "ircd.me.online.status_msg"          },
	{ "default",  "Wanna chat? IRCd at your service!"  }
};

ircd::conf::item<std::string>
me_offline_status_msg
{
	{ "name",     "ircd.me.offline.status_msg"     },
	{ "default",  "Catch ya on the flip side..."   }
};

struct ircd::m::init::modules
{
	modules(const json::object &);
	~modules() noexcept;
};

struct ircd::m::init::listeners
{
	listeners(const json::object &);
	~listeners() noexcept;
};

//
// init::init
//

ircd::m::init::init()
try
:config
{
	ircd::conf::config
}
,_self
{
	this->config
}
,_keys
{
	this->config
}
,modules{[this]
{
	auto ret{std::make_unique<struct modules>(config)};
	if(db::sequence(*dbs::events) == 0)
		bootstrap();

	return ret;
}()}
,listeners
{
	std::make_unique<struct listeners>(config)
}
{
	presence::set(me, "online", me_online_status_msg);
}
catch(const m::error &e)
{
	log.error("%s %s", e.what(), e.content);
}
catch(const std::exception &e)
{
	log.error("%s", e.what());
}

ircd::m::init::~init()
noexcept try
{
	listeners.reset(nullptr);
	presence::set(me, "offline", me_offline_status_msg);
}
catch(const m::error &e)
{
	log.critical("%s %s", e.what(), e.content);
	ircd::terminate();
}

void
ircd::m::init::close()
{
	listeners.reset(nullptr);
}

ircd::m::init::modules::modules(const json::object &config)
try
{
	if(ircd::noautomod)
	{
		log::warning
		{
			"Not loading modules because noautomod flag is set. "
			"You may still load modules manually."
		};

		return;
	}

	static const string_view prefixes[]
	{
		"s_", "m_", "client_", "key_", "federation_", "media_"
	};

	m::modules.emplace("vm"s, "vm"s);

	for(const auto &name : mods::available())
		if(startswith_any(name, std::begin(prefixes), std::end(prefixes)))
			m::modules.emplace(name, name);

	m::modules.emplace("root"s, "root"s);
}
catch(...)
{
	this->~modules();
}

ircd::m::init::modules::~modules()
noexcept
{
	m::modules.clear();
}

namespace ircd::m
{
	static void init_listener(const json::object &config, const string_view &name, const json::object &conf);
}

ircd::m::init::listeners::listeners(const json::object &config)
try
{
	if(ircd::nolisten)
	{
		log::warning
		{
			"Not listening on any addresses because nolisten flag is set."
		};

		return;
	}

	const json::array listeners
	{
		config[{"ircd", "listen"}]
	};

	for(const auto &name : listeners) try
	{
		const json::object &opts
		{
			config.at({"listen", unquote(name)})
		};

		if(!opts.has("tmp_dh_path"))
			throw user_error
			{
				"Listener %s requires a 'tmp_dh_path' in the config. We do not"
				" create this yet. Try `openssl dhparam -outform PEM -out dh512.pem 512`",
				name
			};

		init_listener(config, unquote(name), opts);
	}
	catch(const json::not_found &e)
	{
		throw ircd::user_error
		{
			"Failed to find configuration block for listener %s", name
		};
	}
}
catch(...)
{
	this->~listeners();
}

ircd::m::init::listeners::~listeners()
noexcept
{
	m::listeners.clear();
}

static void
ircd::m::init_listener(const json::object &config,
                       const string_view &name,
                       const json::object &opts)
{
	m::listeners.emplace_back(name, opts);
}

void
ircd::m::init::bootstrap()
{
	assert(dbs::events);
	assert(db::sequence(*dbs::events) == 0);

	ircd::log::notice
	(
		"This appears to be your first time running IRCd because the events "
		"database is empty. I will be bootstrapping it with initial events now..."
	);

	if(me.user_id.hostname() == "localhost")
		log::warning
		{
			"The ircd.origin is configured to localhost. This is probably not"
			" what you want. To fix this now, you will have to remove the "
			" database and start over."
		};

	create(user::users, me.user_id);

	create(my_room, me.user_id);
	create(me.user_id);
	me.activate();

	join(my_room, me.user_id);

	send(my_room, me.user_id, "m.room.name", "",
	{
		{ "name", "IRCd's Room" }
	});

	send(my_room, me.user_id, "m.room.topic", "",
	{
		{ "topic", "The daemon's den." }
	});

	send(user::users, me.user_id, "m.room.name", "",
	{
		{ "name", "Users" }
	});

	create(user::tokens, me.user_id);
	send(user::tokens, me.user_id, "m.room.name", "",
	{
		{ "name", "User Tokens" }
	});
}

///////////////////////////////////////////////////////////////////////////////
//
// m/self.h
//

//
// my user
//

ircd::m::user::id::buf
ircd_user_id
{
	"ircd", "localhost"  // gets replaced after conf init
};

ircd::m::user
ircd::m::me
{
	ircd_user_id
};

//
// my room
//

ircd::m::room::id::buf
ircd_room_id
{
	"ircd", "localhost" // replaced after conf init
};

ircd::m::room
ircd::m::my_room
{
	ircd_room_id
};

//
// my node
//

ircd::m::node::id::buf
ircd_node_id
{
	"", "localhost" // replaced after conf init
};

ircd::m::node
ircd::m::my_node
{
	ircd_node_id
};

bool
ircd::m::self::host(const string_view &s)
{
	return s == host();
}

ircd::string_view
ircd::m::self::host()
{
	return me.user_id.host();
}

//
// init
//

ircd::m::self::init::init(const json::object &config)
{
	const string_view &origin_name
	{
		unquote(config.get({"ircd", "origin"}, "localhost"))
	};

	ircd_user_id = {"ircd", origin_name};
	m::me = {ircd_user_id};

	ircd_room_id = {"ircd", origin_name};
	m::my_room = {ircd_room_id};

	ircd_node_id = {"", origin_name};
	m::my_node = {ircd_node_id};

	if(origin_name == "localhost")
		log::warning
		{
			"The ircd.origin is configured or has defaulted to 'localhost'"
		};
}

ircd::m::self::init::~init()
noexcept
{
}

///////////////////////////////////////////////////////////////////////////////
//
// m/vm.h
//

decltype(ircd::m::vm::accept)
ircd::m::vm::accept
{};

decltype(ircd::m::vm::current_sequence)
ircd::m::vm::current_sequence
{};

decltype(ircd::m::vm::default_opts)
ircd::m::vm::default_opts
{};

decltype(ircd::m::vm::default_copts)
ircd::m::vm::default_copts
{};

/// Instance list linkage for all of the evaluations.
template<>
decltype(ircd::util::instance_list<ircd::m::vm::eval>::list)
ircd::util::instance_list<ircd::m::vm::eval>::list
{};

decltype(ircd::m::vm::eval::id_ctr)
ircd::m::vm::eval::id_ctr
{};

//
// Eval
//
// Processes any event from any place from any time and does whatever is
// necessary to validate, reject, learn from new information, ignore old
// information and advance the state of IRCd as best as possible.

//
// eval::eval
//

ircd::m::vm::eval::eval(const room &room,
                        json::iov &event,
                        const json::iov &content)
:eval{}
{
	operator()(room, event, content);
}

ircd::m::vm::eval::eval(json::iov &event,
                        const json::iov &content,
                        const vm::copts &opts)
:eval{opts}
{
	operator()(event, content);
}

ircd::m::vm::eval::eval(const event &event,
                        const vm::opts &opts)
:eval{opts}
{
	operator()(event);
}

ircd::m::vm::eval::eval(const vm::copts &opts)
:opts{&opts}
,copts{&opts}
{
}

ircd::m::vm::eval::eval(const vm::opts &opts)
:opts{&opts}
{
}

ircd::m::vm::eval::~eval()
noexcept
{
}

ircd::m::vm::eval::operator
const event::id::buf &()
const
{
	return event_id;
}

///
/// Figure 1:
///          in     .  <-- injection
///    ===:::::::==//
///    |  ||||||| //   <-- these functions
///    |   \\|// //|
///    |    ||| // |   |  acceleration
///    |    |||//  |   |
///    |    |||/   |   |
///    |    |||    |   V
///    |    !!!    |
///    |     *     |   <----- nozzle
///    | ///|||\\\ |
///    |/|/|/|\|\|\|   <---- propagation cone
///  _/|/|/|/|\|\|\|\_
///         out
///

/// Inject a new event in a room originating from this server.
///
enum ircd::m::vm::fault
ircd::m::vm::eval::operator()(const room &room,
                              json::iov &event,
                              const json::iov &contents)
{
	using prototype = fault (eval &, const m::room &, json::iov &, const json::iov &);

	static import<prototype> function
	{
		"vm", "eval__commit_room"
	};

	// This eval entry point is only used for commits. We try to find the
	// commit opts the user supplied directly to this eval or with the room.
	if(!copts)
		copts = room.opts;

	if(!copts)
		copts = &vm::default_copts;

	// Note that the regular opts is unconditionally overridden because the
	// user should have provided copts instead.
	this->opts = copts;

	// Set a member pointer to the json::iov currently being composed. This
	// allows other parallel evals to have deep access to exactly what this
	// eval is attempting to do.
	issue = &event;
	room_id = room.room_id;
	phase = phase::ENTER;
	const unwind deissue{[this]
	{
		phase = phase::ACCEPT;
		room_id = {};
		issue = nullptr;
	}};

	return function(*this, room, event, contents);
}

/// Inject a new event originating from this server.
///
enum ircd::m::vm::fault
ircd::m::vm::eval::operator()(json::iov &event,
                              const json::iov &contents)
{
	using prototype = fault (eval &, json::iov &, const json::iov &);

	static import<prototype> function
	{
		"vm", "eval__commit"
	};

	// This eval entry point is only used for commits. If the user did not
	// supply commit opts we supply the default ones here.
	if(!copts)
		copts = &vm::default_copts;

	// Note that the regular opts is unconditionally overridden because the
	// user should have provided copts instead.
	this->opts = copts;

	// Set a member pointer to the json::iov currently being composed. This
	// allows other parallel evals to have deep access to exactly what this
	// eval is attempting to do.
	assert(!room_id || issue == &event);
	if(!room_id)
	{
		issue = &event;
		phase = phase::ENTER;
	}

	const unwind deissue{[this]
	{
		// issue is untouched when room_id is set; that indicates it was set
		// and will be unset by another eval function (i.e above).
		if(!room_id)
		{
			phase = phase::ACCEPT;
			issue = nullptr;
		}
	}};

	return function(*this, event, contents);
}

enum ircd::m::vm::fault
ircd::m::vm::eval::operator()(const event &event)
{
	using prototype = fault (eval &, const m::event &);

	static import<prototype> function
	{
		"vm", "eval__event"
	};

	// Set a member pointer to the event currently being evaluated. This
	// allows other parallel evals to have deep access to exactly what this
	// eval is working on. The pointer must be nulled on the way out.
	this->event_= &event;
	if(!issue)
		phase = phase::ENTER;

	const unwind null_event{[this]
	{
		if(!issue)
			phase = phase::ACCEPT;

		this->event_ = nullptr;
	}};

	return function(*this, event);
}

const uint64_t &
ircd::m::vm::sequence(const eval &eval)
{
	return eval.sequence;
}

uint64_t
ircd::m::vm::retired_sequence()
{
	event::id::buf event_id;
	return retired_sequence(event_id);
}

uint64_t
ircd::m::vm::retired_sequence(event::id::buf &event_id)
{
	static constexpr auto column_idx
	{
		json::indexof<event, "event_id"_>()
	};

	auto &column
	{
		dbs::event_column.at(column_idx)
	};

	const auto it
	{
		column.rbegin()
	};

	if(!it)
	{
		// If this iterator is invalid the events db should
		// be completely fresh.
		assert(db::sequence(*dbs::events) == 0);
		return 0;
	}

	const auto &ret
	{
		byte_view<uint64_t>(it->first)
	};

	event_id = it->second;
	return ret;
}

ircd::string_view
ircd::m::vm::reflect(const enum fault &code)
{
	switch(code)
	{
		case fault::ACCEPT:       return "ACCEPT";
		case fault::EXISTS:       return "EXISTS";
		case fault::INVALID:      return "INVALID";
		case fault::DEBUGSTEP:    return "DEBUGSTEP";
		case fault::BREAKPOINT:   return "BREAKPOINT";
		case fault::GENERAL:      return "GENERAL";
		case fault::EVENT:        return "EVENT";
		case fault::STATE:        return "STATE";
		case fault::INTERRUPT:    return "INTERRUPT";
	}

	return "??????";
}

ircd::string_view
ircd::m::vm::reflect(const enum eval::phase &phase)
{
	switch(phase)
	{
		case eval::phase::ACCEPT:      return "ACCEPT";
		case eval::phase::ENTER:       return "ENTER";
	}

	return "??????";
}

//
// accepted
//

ircd::m::vm::accepted::accepted(const m::event &event,
                                const vm::opts *const &opts,
                                const event::conforms *const &report)
:m::event{event}
,context{ctx::current}
,opts{opts}
,report{report}
{
}

///////////////////////////////////////////////////////////////////////////////
//
// m/keys.h
//

bool
ircd::m::verify(const m::keys &keys)
{
	using prototype = bool (const m::keys &) noexcept;

	static import<prototype> function
	{
		"key_keys", "verify__keys"
	};

	return function(keys);
}

//
// m::self
//

ircd::ed25519::sk
ircd::m::self::secret_key
{};

ircd::ed25519::pk
ircd::m::self::public_key
{};

std::string
ircd::m::self::public_key_b64
{};

std::string
ircd::m::self::public_key_id
{};

std::string
ircd::m::self::tls_cert_der
{};

std::string
ircd::m::self::tls_cert_der_sha256_b64
{};

//
// keys
//

void
ircd::m::keys::get(const string_view &server_name,
                   const closure &closure)
{
	return get(server_name, "", closure);
}

void
ircd::m::keys::get(const string_view &server_name,
                   const string_view &key_id,
                   const closure &closure_)
{
	using prototype = void (const string_view &, const string_view &, const closure &);

	static import<prototype> function
	{
		"key_keys", "get__keys"
	};

	return function(server_name, key_id, closure_);
}

bool
ircd::m::keys::query(const string_view &query_server,
                     const queries &queries_,
                     const closure_bool &closure)
{
	using prototype = bool (const string_view &, const queries &, const closure_bool &);

	static import<prototype> function
	{
		"key_keys", "query__keys"
	};

	return function(query_server, queries_, closure);
}

//
// init
//

ircd::m::keys::init::init(const json::object &config)
:config{config}
{
	certificate();
	signing();
}

ircd::m::keys::init::~init()
noexcept
{
}

void
ircd::m::keys::init::certificate()
{
	const string_view origin
	{
		unquote(this->config.at({"ircd", "origin"}))
	};

	const json::object config
	{
		this->config.at({"origin", origin})
	};

	const std::string private_key_file
	{
		unquote(config.get("ssl_private_key_pem_path"))
	};

	const std::string public_key_file
	{
		unquote(config.get("ssl_public_key_pem_path", private_key_file + ".pub"))
	};

	if(!private_key_file)
		throw user_error
		{
			"You must specify an SSL private key file at"
			" origin.[%s].ssl_private_pem_path even if you do not have one;"
			" it will be created there.",
			origin
		};

	if(!fs::exists(private_key_file))
	{
		log::warning
		{
			"Failed to find certificate private key @ `%s'; creating...",
			private_key_file
		};

		openssl::genrsa(private_key_file, public_key_file);
	}

	const std::string cert_file
	{
		unquote(config.get("ssl_certificate_pem_path"))
	};

	if(!cert_file)
		throw user_error
		{
			"You must specify an SSL certificate file path in the config at"
			" origin.[%s].ssl_certificate_pem_path even if you do not have one;"
			" it will be created there.",
			origin
		};

	if(!fs::exists(cert_file))
	{
		if(!this->config.has({"certificate", origin, "subject"}))
			throw user_error
			{
				"Failed to find SSL certificate @ `%s'. Additionally, no"
				" certificate.[%s].subject was found in the conf to generate one.",
				cert_file,
				origin
			};

		log::warning
		{
			"Failed to find SSL certificate @ `%s'; creating for '%s'...",
			cert_file,
			origin
		};

		const unique_buffer<mutable_buffer> buf
		{
			1_MiB
		};

		const json::strung opts{json::members
		{
			{ "private_key_pem_path",  private_key_file  },
			{ "public_key_pem_path",   public_key_file   },
			{ "subject", this->config.get({"certificate", origin, "subject"}) }
		}};

		const auto cert
		{
			openssl::genX509_rsa(buf, opts)
		};

		fs::overwrite(cert_file, cert);
	}

	const auto cert_pem
	{
		fs::read(cert_file)
	};

	const unique_buffer<mutable_buffer> der_buf
	{
		8_KiB
	};

	const auto cert_der
	{
		openssl::cert2d(der_buf, cert_pem)
	};

	const fixed_buffer<const_buffer, crh::sha256::digest_size> hash
	{
		sha256{cert_der}
	};

	self::tls_cert_der_sha256_b64 =
	{
		b64encode_unpadded(hash)
	};

	log.info("Certificate `%s' :PEM %zu bytes; DER %zu bytes; sha256b64 %s",
	         cert_file,
	         cert_pem.size(),
	         ircd::size(cert_der),
	         self::tls_cert_der_sha256_b64);

	thread_local char print_buf[8_KiB];
	log.info("Certificate `%s' :%s",
	         cert_file,
	         openssl::print_subject(print_buf, cert_pem));
}

void
ircd::m::keys::init::signing()
{
	const string_view origin
	{
		unquote(this->config.at({"ircd", "origin"}))
	};

	const json::object config
	{
		this->config.at({"origin", unquote(origin)})
	};

	const std::string sk_file
	{
		unquote(config.get("ed25519_private_key_path"))
	};

	if(!sk_file)
		throw user_error
		{
			"Failed to find ed25519 secret key path at"
			" origin.[%s].ed25519_private_key_path in config. If you do not"
			" have a private key, specify a path for one to be created.",
			origin
		};

	if(fs::exists(sk_file))
		log.info("Using ed25519 secret key @ `%s'", sk_file);
	else
		log.notice("Creating ed25519 secret key @ `%s'", sk_file);

	self::secret_key = ed25519::sk
	{
		sk_file, &self::public_key
	};

	self::public_key_b64 = b64encode_unpadded(self::public_key);
	const fixed_buffer<const_buffer, sha256::digest_size> hash
	{
		sha256{self::public_key}
	};

	const auto public_key_hash_b58
	{
		b58encode(hash)
	};

	static const auto trunc_size{8};
	self::public_key_id = fmt::snstringf
	{
		BUFSIZE, "ed25519:%s", trunc(public_key_hash_b58, trunc_size)
	};

	log.info("Current key is '%s' and the public key is: %s",
	         self::public_key_id,
	         self::public_key_b64);
}

///////////////////////////////////////////////////////////////////////////////
//
// m/receipt.h
//

ircd::m::event::id::buf
ircd::m::receipt::read(const id::room &room_id,
                       const id::user &user_id,
                       const id::event &event_id)
{
	return read(room_id, user_id, event_id, ircd::time<milliseconds>());
}

ircd::m::event::id::buf
ircd::m::receipt::read(const id::room &room_id,
                       const id::user &user_id,
                       const id::event &event_id,
                       const time_t &ms)
{
	using prototype = event::id::buf (const id::room &, const id::user &, const id::event &, const time_t &);

	static import<prototype> function
	{
		"client_rooms", "commit__m_receipt_m_read"
	};

	return function(room_id, user_id, event_id, ms);
}

///////////////////////////////////////////////////////////////////////////////
//
// m/typing.h
//

ircd::m::event::id::buf
ircd::m::typing::set(const m::typing &object)
{
	using prototype = event::id::buf (const m::typing &);

	static import<prototype> function
	{
		"client_rooms", "commit__m_typing"
	};

	return function(object);
}

///////////////////////////////////////////////////////////////////////////////
//
// m/presence.h
//

ircd::m::presence::presence(const user &user,
                            const mutable_buffer &buf)
:presence
{
	get(user, buf)
}
{
}

ircd::m::event::id::buf
ircd::m::presence::set(const user &user,
                       const string_view &presence,
                       const string_view &status_msg)
{
	return set(m::presence
	{
		{ "user_id",     user.user_id  },
		{ "presence",    presence      },
		{ "status_msg",  status_msg    },
	});
}

ircd::m::event::id::buf
ircd::m::presence::set(const presence &object)
{
	using prototype = event::id::buf (const presence &);

	static import<prototype> function
	{
		"client_presence", "commit__m_presence"
	};

	return function(object);
}

ircd::json::object
ircd::m::presence::get(const user &user,
                       const mutable_buffer &buffer)
{
	json::object ret;
	get(std::nothrow, user, [&ret, &buffer]
	(const json::object &object)
	{
		ret = { data(buffer), copy(buffer, object) };
	});

	return ret;
}

void
ircd::m::presence::get(const user &user,
                       const closure &closure)
{
	get(user, [&closure]
	(const m::event &event, const json::object &content)
	{
		closure(content);
	});
}

void
ircd::m::presence::get(const user &user,
                       const event_closure &closure)
{
	if(!get(std::nothrow, user, closure))
		throw m::NOT_FOUND
		{
			"No presence found for %s",
			string_view{user.user_id}
		};
}

bool
ircd::m::presence::get(std::nothrow_t,
                       const user &user,
                       const closure &closure)
{
	return get(std::nothrow, user, [&closure]
	(const m::event &event, const json::object &content)
	{
		closure(content);
	});
}

bool
ircd::m::presence::get(std::nothrow_t,
                       const user &user,
                       const event_closure &closure)
{
	using prototype = bool (std::nothrow_t, const m::user &, const event_closure &);

	static import<prototype> function
	{
		"client_presence", "m_presence_get"
	};

	return function(std::nothrow, user, closure);
}

bool
ircd::m::presence::valid_state(const string_view &state)
{
	using prototype = bool (const string_view &);

	static import<prototype> function
	{
		"m_presence", "presence_valid_state"
	};

	return function(state);
}

///////////////////////////////////////////////////////////////////////////////
//
// m/node.h
//

/// ID of the room which indexes all nodes (an instance of the room is
/// provided below).
const ircd::m::room::id::buf
nodes_room_id
{
	"nodes", ircd::my_host()
};

/// The nodes room is the database of all nodes. It primarily serves as an
/// indexing mechanism and for top-level node related keys.
///
const ircd::m::room
ircd::m::nodes
{
	nodes_room_id
};

ircd::m::node
ircd::m::create(const id::node &node_id,
                const json::members &args)
{
	using prototype = node (const id::node &, const json::members &);

	static import<prototype> function
	{
		"s_node", "create_node"
	};

	assert(node_id);
	return function(node_id, args);
}

bool
ircd::m::exists(const node::id &node_id)
{
	using prototype = bool (const node::id &);

	static import<prototype> function
	{
		"s_node", "exists__nodeid"
	};

	return function(node_id);
}

bool
ircd::m::my(const node &node)
{
	return my(node.node_id);
}

//
// node
//

void
ircd::m::node::key(const string_view &key_id,
                   const ed25519_closure &closure)
const
{
	key(key_id, key_closure{[&closure]
	(const string_view &keyb64)
	{
		const ed25519::pk pk
		{
			[&keyb64](auto &buf)
			{
				b64decode(buf, unquote(keyb64));
			}
		};

		closure(pk);
	}});
}

void
ircd::m::node::key(const string_view &key_id,
                   const key_closure &closure)
const
{
	const auto &server_name
	{
		node_id.hostname()
	};

	m::keys::get(server_name, key_id, [&closure, &key_id]
	(const json::object &keys)
	{
		const json::object &vks
		{
			keys.at("verify_keys")
		};

		const json::object &vkk
		{
			vks.at(key_id)
		};

		const string_view &key
		{
			vkk.at("key")
		};

		closure(key);
	});
}

/// Generates a node-room ID into buffer; see room_id() overload.
ircd::m::id::room::buf
ircd::m::node::room_id()
const
{
	ircd::m::id::room::buf buf;
	return buf.assigned(room_id(buf));
}

/// This generates a room mxid for the "node's room" essentially serving as
/// a database mechanism for this specific node. This room_id is a hash of
/// the node's full mxid.
///
ircd::m::id::room
ircd::m::node::room_id(const mutable_buffer &buf)
const
{
	assert(!empty(node_id));
	const sha256::buf hash
	{
		sha256{node_id}
	};

	char b58[size(hash) * 2];
	return
	{
		buf, b58encode(b58, hash), my_host()
	};
}

//
// node::room
//

ircd::m::node::room::room(const m::node::id &node_id)
:room{m::node{node_id}}
{}

ircd::m::node::room::room(const m::node &node)
:node{node}
,room_id{node.room_id()}
{
	static_cast<m::room &>(*this) = room_id;
}

///////////////////////////////////////////////////////////////////////////////
//
// m/events.h
//

bool
ircd::m::events::rfor_each(const event::idx &start,
                           const event_filter &filter,
                           const closure_bool &closure)
{
	auto limit
	{
		json::get<"limit"_>(filter)?: 32L
	};

	return rfor_each(start, [&filter, &closure, &limit]
	(const event::idx &event_idx, const m::event &event)
	-> bool
	{
		if(!match(filter, event))
			return true;

		if(!closure(event_idx, event))
			return false;

		return --limit;
	});
}

bool
ircd::m::events::rfor_each(const event::idx &start,
                           const closure_bool &closure)
{
	event::fetch event;
	return rfor_each(start, id_closure_bool{[&event, &closure]
	(const event::idx &event_idx, const event::id &event_id)
	{
		if(!seek(event, event_idx, std::nothrow))
			return true;

		return closure(event_idx, event);
	}});
}

bool
ircd::m::events::rfor_each(const event::idx &start,
                           const id_closure_bool &closure)
{
	static constexpr auto column_idx
	{
		json::indexof<event, "event_id"_>()
	};

	auto &column
	{
		dbs::event_column.at(column_idx)
	};

	if(start == uint64_t(-1))
	{
		for(auto it(column.rbegin()); it; ++it)
			if(!closure(byte_view<event::idx>(it->first), it->second))
				return false;

		return true;
	}

	auto it
	{
		column.lower_bound(byte_view<string_view>(start))
	};

	for(; it; ++it)
		if(!closure(byte_view<event::idx>(it->first), it->second))
			return false;

	return true;
}

bool
ircd::m::events::for_each(const event::idx &start,
                          const event_filter &filter,
                          const closure_bool &closure)
{
	auto limit
	{
		json::get<"limit"_>(filter)?: 32L
	};

	return for_each(start, [&filter, &closure, &limit]
	(const event::idx &event_idx, const m::event &event)
	-> bool
	{
		if(!match(filter, event))
			return true;

		if(!closure(event_idx, event))
			return false;

		return --limit;
	});
}

bool
ircd::m::events::for_each(const event::idx &start,
                          const closure_bool &closure)
{
	event::fetch event;
	return for_each(start, id_closure_bool{[&event, &closure]
	(const event::idx &event_idx, const event::id &event_id)
	{
		if(!seek(event, event_idx, std::nothrow))
			return true;

		return closure(event_idx, event);
	}});
}

bool
ircd::m::events::for_each(const event::idx &start,
                          const id_closure_bool &closure)
{
	static constexpr auto column_idx
	{
		json::indexof<event, "event_id"_>()
	};

	auto &column
	{
		dbs::event_column.at(column_idx)
	};

	auto it
	{
		start > 0?
			column.lower_bound(byte_view<string_view>(start)):
			column.begin()
	};

	for(; it; ++it)
		if(!closure(byte_view<event::idx>(it->first), it->second))
			return false;

	return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// m/filter.h
//

//TODO: globular expression
//TODO: tribool for contains_url; we currently ignore the false value.
bool
ircd::m::match(const room_event_filter &filter,
               const event &event)
{
	if(json::get<"contains_url"_>(filter) == true)
		if(!at<"content"_>(event).has("url"))
			return false;

	for(const auto &room_id : json::get<"not_rooms"_>(filter))
		if(at<"room_id"_>(event) == unquote(room_id))
			return false;

	if(empty(json::get<"rooms"_>(filter)))
		return match(event_filter{filter}, event);

	for(const auto &room_id : json::get<"rooms"_>(filter))
		if(at<"room_id"_>(event) == unquote(room_id))
			return match(event_filter{filter}, event);

	return false;
}

//TODO: globular expression
bool
ircd::m::match(const event_filter &filter,
               const event &event)
{
	for(const auto &type : json::get<"not_types"_>(filter))
		if(at<"type"_>(event) == unquote(type))
			return false;

	for(const auto &sender : json::get<"not_senders"_>(filter))
		if(at<"sender"_>(event) == unquote(sender))
			return false;

	if(empty(json::get<"senders"_>(filter)) && empty(json::get<"types"_>(filter)))
		return true;

	if(empty(json::get<"senders"_>(filter)))
	{
		for(const auto &type : json::get<"types"_>(filter))
			if(at<"type"_>(event) == unquote(type))
				return true;

		return false;
	}

	if(empty(json::get<"types"_>(filter)))
	{
		for(const auto &sender : json::get<"senders"_>(filter))
			if(at<"sender"_>(event) == unquote(sender))
				return true;

		return false;
	}

	return true;
}

//
// filter
//

ircd::m::filter::filter(const user &user,
                        const string_view &filter_id,
                        const mutable_buffer &buf)
{
	get(user, filter_id, [this, &buf]
	(const json::object &filter)
	{
		const size_t len
		{
			copy(buf, string_view{filter})
		};

		new (this) m::filter
		{
			json::object
			{
				data(buf), len
			}
		};
	});
}

//
// room_filter
//

ircd::m::room_filter::room_filter(const mutable_buffer &buf,
                                  const json::members &members)
:super_type::tuple
{
	json::stringify(mutable_buffer{buf}, members)
}
{
}

//
// room_event_filter
//

ircd::m::room_event_filter::room_event_filter(const mutable_buffer &buf,
                                              const json::members &members)
:super_type::tuple
{
	json::stringify(mutable_buffer{buf}, members)
}
{
}

//
// event_filter
//

ircd::m::event_filter::event_filter(const mutable_buffer &buf,
                                    const json::members &members)
:super_type::tuple
{
	json::stringify(mutable_buffer{buf}, members)
}
{
}

///////////////////////////////////////////////////////////////////////////////
//
// m/rooms.h
//

void
ircd::m::rooms::for_each(const user &user,
                         const user::rooms::closure &closure)
{
	const m::user::rooms rooms{user};
	rooms.for_each(closure);
}

void
ircd::m::rooms::for_each(const user &user,
                         const user::rooms::closure_bool &closure)
{
	const m::user::rooms rooms{user};
	rooms.for_each(closure);
}

void
ircd::m::rooms::for_each(const user &user,
                         const string_view &membership,
                         const user::rooms::closure &closure)
{
	const m::user::rooms rooms{user};
	rooms.for_each(membership, closure);
}

void
ircd::m::rooms::for_each(const user &user,
                         const string_view &membership,
                         const user::rooms::closure_bool &closure)
{
	const m::user::rooms rooms{user};
	rooms.for_each(membership, closure);
}

void
ircd::m::rooms::for_each(const room::closure &closure)
{
	for_each(room::closure_bool{[&closure]
	(const room &room)
	{
		closure(room);
		return true;
	}});
}

void
ircd::m::rooms::for_each(const room::closure_bool &closure)
{
	for_each(room::id::closure_bool{[&closure]
	(const room::id &room_id)
	{
		return closure(room_id);
	}});
}

void
ircd::m::rooms::for_each(const room::id::closure &closure)
{
	for_each(room::id::closure_bool{[&closure]
	(const room::id &room_id)
	{
		closure(room_id);
		return true;
	}});
}

void
ircd::m::rooms::for_each(const room::id::closure_bool &closure)
{
	const room::state state
	{
		my_room
	};

	state.test("ircd.room", room::state::keys_bool{[&closure]
	(const string_view &key)
	{
		return !closure(key);
	}});
}

///////////////////////////////////////////////////////////////////////////////
//
// m/user.h
//

/// ID of the room which indexes all users (an instance of the room is
/// provided below).
const ircd::m::room::id::buf
users_room_id
{
	"users", ircd::my_host()
};

/// The users room is the database of all users. It primarily serves as an
/// indexing mechanism and for top-level user related keys. Accounts
/// registered on this server will be among state events in this room.
/// Users do not have access to this room, it is used internally.
///
ircd::m::room
ircd::m::user::users
{
	users_room_id
};

/// ID of the room which stores ephemeral tokens (an instance of the room is
/// provided below).
const ircd::m::room::id::buf
tokens_room_id
{
	"tokens", ircd::my_host()
};

/// The tokens room serves as a key-value lookup for various tokens to
/// users, etc. It primarily serves to store access tokens for users. This
/// is a separate room from the users room because in the future it may
/// have an optimized configuration as well as being more easily cleared.
///
ircd::m::room
ircd::m::user::tokens
{
	tokens_room_id
};

ircd::m::user
ircd::m::create(const id::user &user_id,
                const json::members &contents)
{
	using prototype = user (const id::user &, const json::members &);

	static import<prototype> function
	{
		"client_user", "user_create"
	};

	return function(user_id, contents);
}

bool
ircd::m::exists(const user::id &user_id)
{
	return user::users.has("ircd.user", user_id);
}

bool
ircd::m::exists(const user &user)
{
	return exists(user.user_id);
}

bool
ircd::m::my(const user &user)
{
	return my(user.user_id);
}

//
// user
//

/// Generates a user-room ID into buffer; see room_id() overload.
ircd::m::id::room::buf
ircd::m::user::room_id()
const
{
	ircd::m::id::room::buf buf;
	return buf.assigned(room_id(buf));
}

/// This generates a room mxid for the "user's room" essentially serving as
/// a database mechanism for this specific user. This room_id is a hash of
/// the user's full mxid.
///
ircd::m::id::room
ircd::m::user::room_id(const mutable_buffer &buf)
const
{
	assert(!empty(user_id));
	const ripemd160::buf hash
	{
		ripemd160{user_id}
	};

	char b58[size(hash) * 2];
	return
	{
		buf, b58encode(b58, hash), my_host()
	};
}

ircd::string_view
ircd::m::user::gen_access_token(const mutable_buffer &buf)
{
	static const size_t token_max{32};
	static const auto &token_dict{rand::dict::alpha};

	const mutable_buffer out
	{
		data(buf), std::min(token_max, size(buf))
	};

	return rand::string(token_dict, out);
}

ircd::m::event::id::buf
ircd::m::user::activate()
{
	using prototype = event::id::buf (const m::user &);

	static import<prototype> function
	{
		"client_account", "activate__user"
	};

	return function(*this);
}

ircd::m::event::id::buf
ircd::m::user::deactivate()
{
	using prototype = event::id::buf (const m::user &);

	static import<prototype> function
	{
		"client_account", "deactivate__user"
	};

	return function(*this);
}

bool
ircd::m::user::is_active()
const
{
	using prototype = bool (const m::user &);

	static import<prototype> function
	{
		"client_account", "is_active__user"
	};

	return function(*this);
}

ircd::m::event::id::buf
ircd::m::user::filter(const json::object &filter,
                      const mutable_buffer &idbuf)
{
	using prototype = event::id::buf (const m::user &, const json::object &, const mutable_buffer &);

	static import<prototype> function
	{
		"client_user", "filter_set"
	};

	return function(*this, filter, idbuf);
}

std::string
ircd::m::user::filter(const string_view &filter_id)
const
{
	std::string ret;
	filter(filter_id, [&ret]
	(const json::object &filter)
	{
		ret.assign(data(filter), size(filter));
	});

	return ret;
}

std::string
ircd::m::user::filter(std::nothrow_t,
                      const string_view &filter_id)
const
{
	std::string ret;
	filter(std::nothrow, filter_id, [&ret]
	(const json::object &filter)
	{
		ret.assign(data(filter), size(filter));
	});

	return ret;
}

void
ircd::m::user::filter(const string_view &filter_id,
                      const filter_closure &closure)
const
{
	if(!filter(std::nothrow, filter_id, closure))
		throw m::NOT_FOUND
		{
			"Filter '%s' not found", filter_id
		};
}

bool
ircd::m::user::filter(std::nothrow_t,
                      const string_view &filter_id,
                      const filter_closure &closure)
const
{
	using prototype = bool (std::nothrow_t, const m::user &, const string_view &, const filter_closure &);

	static import<prototype> function
	{
		"client_user", "filter_get"
	};

	return function(std::nothrow, *this, filter_id, closure);
}

ircd::m::event::id::buf
ircd::m::user::account_data(const m::user &sender,
                            const string_view &type,
                            const json::object &val)
{
	using prototype = event::id::buf (const m::user &, const m::user &, const string_view &, const json::object &);

	static import<prototype> function
	{
		"client_user", "account_data_set"
	};

	return function(*this, sender, type, val);
}

ircd::json::object
ircd::m::user::account_data(const mutable_buffer &out,
                            const string_view &type)
const
{
	json::object ret;
	account_data(std::nothrow, type, [&out, &ret]
	(const json::object &val)
	{
		ret = string_view { data(out), copy(out, val) };
	});

	return ret;
}

bool
ircd::m::user::account_data(std::nothrow_t,
                            const string_view &type,
                            const account_data_closure &closure)
const try
{
	account_data(type, closure);
	return true;
}
catch(const std::exception &e)
{
	return false;
}

void
ircd::m::user::account_data(const string_view &type,
                            const account_data_closure &closure)
const
{
	using prototype = void (const m::user &, const string_view &, const account_data_closure &);

	static import<prototype> function
	{
		"client_user", "account_data_get"
	};

	return function(*this, type, closure);
}

ircd::m::event::id::buf
ircd::m::user::profile(const m::user &sender,
                       const string_view &key,
                       const string_view &val)
{
	using prototype = event::id::buf (const m::user &, const m::user &, const string_view &, const string_view &);

	static import<prototype> function
	{
		"client_profile", "profile_set"
	};

	return function(*this, sender, key, val);
}

ircd::string_view
ircd::m::user::profile(const mutable_buffer &out,
                       const string_view &key)
const
{
	string_view ret;
	profile(std::nothrow, key, [&out, &ret]
	(const string_view &val)
	{
		ret = { data(out), copy(out, val) };
	});

	return ret;
}

bool
ircd::m::user::profile(std::nothrow_t,
                       const string_view &key,
                       const profile_closure &closure)
const try
{
	profile(key, closure);
	return true;
}
catch(const std::exception &e)
{
	return false;
}

void
ircd::m::user::profile(const string_view &key,
                       const profile_closure &closure)
const
{
	using prototype = void (const m::user &, const string_view &, const profile_closure &);

	static import<prototype> function
	{
		"client_profile", "profile_get"
	};

	return function(*this, key, closure);
}

ircd::m::event::id::buf
ircd::m::user::password(const string_view &password)
{
	using prototype = event::id::buf (const m::user::id &, const string_view &) noexcept;

	static import<prototype> function
	{
		"client_account", "set_password"
	};

	return function(user_id, password);
}

bool
ircd::m::user::is_password(const string_view &password)
const noexcept try
{
	using prototype = bool (const m::user::id &, const string_view &) noexcept;

	static import<prototype> function
	{
		"client_account", "is_password"
	};

	return function(user_id, password);
}
catch(const std::exception &e)
{
	log::critical
	{
		"user::is_password(): %s %s",
		string_view{user_id},
		e.what()
	};

	return false;
}

//
// user::room
//

ircd::m::user::room::room(const m::user::id &user_id)
:room{m::user{user_id}}
{}

ircd::m::user::room::room(const m::user &user)
:user{user}
,room_id{user.room_id()}
{
	static_cast<m::room &>(*this) = room_id;
}

//
// user::rooms
//

ircd::m::user::rooms::rooms(const m::user &user)
:user_room{user}
{
}

size_t
ircd::m::user::rooms::count()
const
{
	size_t ret{0};
	for_each([&ret]
	(const m::room &, const string_view &membership)
	{
		++ret;
	});

	return ret;
}

size_t
ircd::m::user::rooms::count(const string_view &membership)
const
{
	size_t ret{0};
	for_each(membership, [&ret]
	(const m::room &, const string_view &membership)
	{
		++ret;
	});

	return ret;
}

void
ircd::m::user::rooms::for_each(const closure &closure)
const
{
	for_each(closure_bool{[&closure]
	(const m::room &room, const string_view &membership)
	{
		closure(room, membership);
		return true;
	}});
}

void
ircd::m::user::rooms::for_each(const closure_bool &closure)
const
{
	const m::room::state state{user_room};
	state.test("ircd.member", [&closure]
	(const m::event &event)
	{
		const m::room::id &room_id
		{
			at<"state_key"_>(event)
		};

		const string_view &membership
		{
			unquote(at<"content"_>(event).at("membership"))
		};

		return !closure(room_id, membership);
	});
}

void
ircd::m::user::rooms::for_each(const string_view &membership,
                               const closure &closure)
const
{
	for_each(membership, closure_bool{[&closure]
	(const m::room &room, const string_view &membership)
	{
		closure(room, membership);
		return true;
	}});
}

void
ircd::m::user::rooms::for_each(const string_view &membership,
                               const closure_bool &closure)
const
{
	if(empty(membership))
		return for_each(closure);

	const m::room::state state{user_room};
	state.test("ircd.member", [&membership, &closure]
	(const m::event &event)
	{
		const string_view &membership_
		{
			unquote(at<"content"_>(event).at("membership"))
		};

		if(membership_ != membership)
			return false;

		const m::room::id &room_id
		{
			at<"state_key"_>(event)
		};

		return !closure(room_id, membership);
	});
}

//
// user::rooms::origins
//

ircd::m::user::rooms::origins::origins(const m::user &user)
:user{user}
{
}

void
ircd::m::user::rooms::origins::for_each(const closure &closure)
const
{
	for_each(string_view{}, closure);
}

void
ircd::m::user::rooms::origins::for_each(const closure_bool &closure)
const
{
	for_each(string_view{}, closure);
}

void
ircd::m::user::rooms::origins::for_each(const string_view &membership,
                                        const closure &closure)
const
{
	for_each(membership, closure_bool{[&closure]
	(const string_view &origin)
	{
		closure(origin);
		return true;
	}});
}

void
ircd::m::user::rooms::origins::for_each(const string_view &membership,
                                        const closure_bool &closure)
const
{
	const m::user::rooms rooms
	{
		user
	};

	std::set<std::string, std::less<>> seen;
	rooms.for_each(membership, rooms::closure_bool{[&closure, &seen]
	(const m::room &room, const string_view &membership)
	{
		const m::room::origins origins{room};
		return !origins.test([&closure, &seen]
		(const string_view &origin)
		{
			const auto it
			{
				seen.lower_bound(origin)
			};

			if(it != end(seen) && *it == origin)
				return false;

			seen.emplace_hint(it, std::string{origin});
			return !closure(origin);
		});
	}});
}

//
// user::mitsein
//

ircd::m::user::mitsein::mitsein(const m::user &user)
:user{user}
{
}

size_t
ircd::m::user::mitsein::count(const string_view &membership)
const
{
	size_t ret{0};
	for_each(membership, [&ret](const m::user &)
	{
		++ret;
	});

	return ret;
}

size_t
ircd::m::user::mitsein::count(const m::user &user,
                              const string_view &membership)
const
{
	size_t ret{0};
	for_each(user, membership, [&ret](const m::room &, const string_view &)
	{
		++ret;
	});

	return ret;
}

void
ircd::m::user::mitsein::for_each(const closure &closure)
const
{
	for_each(string_view{}, closure);
}

void
ircd::m::user::mitsein::for_each(const closure_bool &closure)
const
{
	for_each(string_view{}, closure);
}

void
ircd::m::user::mitsein::for_each(const string_view &membership,
                                 const closure &closure)
const
{
	for_each(membership, closure_bool{[&closure]
	(const m::user &user)
	{
		closure(user);
		return true;
	}});
}

void
ircd::m::user::mitsein::for_each(const string_view &membership,
                                 const closure_bool &closure)
const
{
	const m::user::rooms rooms
	{
		user
	};

	// here we gooooooo :/
	///TODO: ideal: db schema
	///TODO: minimally: custom alloc?
	std::set<std::string, std::less<>> seen;
	rooms.for_each(membership, rooms::closure_bool{[&membership, &closure, &seen]
	(const m::room &room, const string_view &)
	{
		const m::room::members members{room};
		return !members.test(membership, [&seen, &closure]
		(const m::event &event)
		{
			const auto &other
			{
				at<"state_key"_>(event)
			};

			const auto it
			{
				seen.lower_bound(other)
			};

			if(it != end(seen) && *it == other)
				return false;

			seen.emplace_hint(it, std::string{other});
			return !closure(m::user{other});
		});
	}});
}

void
ircd::m::user::mitsein::for_each(const m::user &user,
                                 const rooms::closure &closure)
const
{
	for_each(user, string_view{}, closure);
}

void
ircd::m::user::mitsein::for_each(const m::user &user,
                                 const rooms::closure_bool &closure)
const
{
	for_each(user, string_view{}, closure);
}

void
ircd::m::user::mitsein::for_each(const m::user &user,
                                 const string_view &membership,
                                 const rooms::closure &closure)
const
{
	for_each(user, membership, rooms::closure_bool{[&membership, &closure]
	(const m::room &room, const string_view &)
	{
		closure(room, membership);
		return true;
	}});
}

void
ircd::m::user::mitsein::for_each(const m::user &user,
                                 const string_view &membership,
                                 const rooms::closure_bool &closure)
const
{
	const m::user::rooms our_rooms{this->user};
	const m::user::rooms their_rooms{user};
	const bool use_our
	{
		our_rooms.count() <= their_rooms.count()
	};

	const m::user::rooms &rooms
	{
		use_our? our_rooms : their_rooms
	};

	const string_view &test_key
	{
		use_our? user.user_id : this->user.user_id
	};

	rooms.for_each(membership, rooms::closure_bool{[&membership, &closure, &test_key]
	(const m::room &room, const string_view &)
	{
		if(!room.has("m.room.member", test_key))
			return true;

		return closure(room, membership);
	}});
}

///////////////////////////////////////////////////////////////////////////////
//
// m/room.h
//

ircd::m::room
ircd::m::create(const id::room &room_id,
                const id::user &creator,
                const string_view &type)
{
	using prototype = room (const id::room &, const id::user &, const string_view &);

	static import<prototype> function
	{
		"client_createroom", "createroom__type"
	};

	return function(room_id, creator, type);
}

ircd::m::room
ircd::m::create(const id::room &room_id,
                const id::user &creator,
                const id::room &parent,
                const string_view &type)
{
	using prototype = room (const id::room &, const id::user &, const id::room &, const string_view &);

	static import<prototype> function
	{
		"client_createroom", "createroom__parent_type"
	};

	return function(room_id, creator, parent, type);
}

ircd::m::event::id::buf
ircd::m::join(const id::room_alias &room_alias,
              const id::user &user_id)
{
	using prototype = event::id::buf (const id::room_alias &, const id::user &);

	static import<prototype> function
	{
		"client_rooms", "join__alias_user"
	};

	return function(room_alias, user_id);
}

ircd::m::event::id::buf
ircd::m::join(const room &room,
              const id::user &user_id)
{
	using prototype = event::id::buf (const m::room &, const id::user &);

	static import<prototype> function
	{
		"client_rooms", "join__room_user"
	};

	return function(room, user_id);
}

ircd::m::event::id::buf
ircd::m::leave(const room &room,
               const id::user &user_id)
{
	using prototype = event::id::buf (const m::room &, const id::user &);

	static import<prototype> function
	{
		"client_rooms", "leave__room_user"
	};

	return function(room, user_id);
}

ircd::m::event::id::buf
ircd::m::invite(const room &room,
                const id::user &target,
                const id::user &sender)
{
	using prototype = event::id::buf (const m::room &, const id::user &, const id::user &);

	static import<prototype> function
	{
		"client_rooms", "invite__room_user"
	};

	return function(room, target, sender);
}

ircd::m::event::id::buf
ircd::m::redact(const room &room,
                const id::user &sender,
                const id::event &event_id,
                const string_view &reason)
{
	using prototype = event::id::buf (const m::room &, const id::user &, const id::event &, const string_view &);

	static import<prototype> function
	{
		"client_rooms", "redact__"
	};

	return function(room, sender, event_id, reason);
}

ircd::m::event::id::buf
ircd::m::notice(const room &room,
                const string_view &body)
{
	return message(room, me.user_id, body, "m.notice");
}

ircd::m::event::id::buf
ircd::m::notice(const room &room,
                const m::id::user &sender,
                const string_view &body)
{
	return message(room, sender, body, "m.notice");
}

ircd::m::event::id::buf
ircd::m::msghtml(const room &room,
                 const m::id::user &sender,
                 const string_view &html,
                 const string_view &alt,
                 const string_view &msgtype)
{
	return message(room, sender,
	{
		{ "msgtype",         msgtype                       },
		{ "format",          "org.matrix.custom.html"      },
		{ "body",            { alt?: html, json::STRING }  },
		{ "formatted_body",  { html, json::STRING }        },
	});
}

ircd::m::event::id::buf
ircd::m::message(const room &room,
                 const m::id::user &sender,
                 const string_view &body,
                 const string_view &msgtype)
{
	return message(room, sender,
	{
		{ "body",     { body,    json::STRING } },
		{ "msgtype",  { msgtype, json::STRING } },
	});
}

ircd::m::event::id::buf
ircd::m::message(const room &room,
                 const m::id::user &sender,
                 const json::members &contents)
{
	return send(room, sender, "m.room.message", contents);
}

ircd::m::event::id::buf
ircd::m::send(const room &room,
              const m::id::user &sender,
              const string_view &type,
              const string_view &state_key,
              const json::members &contents)
{
	json::iov _content;
	json::iov::push content[contents.size()];
	return send(room, sender, type, state_key, make_iov(_content, content, contents.size(), contents));
}

ircd::m::event::id::buf
ircd::m::send(const room &room,
              const m::id::user &sender,
              const string_view &type,
              const string_view &state_key,
              const json::object &contents)
{
	json::iov _content;
	json::iov::push content[contents.size()];
	return send(room, sender, type, state_key, make_iov(_content, content, contents.size(), contents));
}

ircd::m::event::id::buf
ircd::m::send(const room &room,
              const m::id::user &sender,
              const string_view &type,
              const string_view &state_key,
              const json::iov &content)
{
	using prototype = event::id::buf (const m::room &, const id::user &, const string_view &, const string_view &, const json::iov &);

	static import<prototype> function
	{
		"client_rooms", "state__iov"
	};

	return function(room, sender, type, state_key, content);
}

ircd::m::event::id::buf
ircd::m::send(const room &room,
              const m::id::user &sender,
              const string_view &type,
              const json::members &contents)
{
	json::iov _content;
	json::iov::push content[contents.size()];
	return send(room, sender, type, make_iov(_content, content, contents.size(), contents));
}

ircd::m::event::id::buf
ircd::m::send(const room &room,
              const m::id::user &sender,
              const string_view &type,
              const json::object &contents)
{
	json::iov _content;
	json::iov::push content[contents.size()];
	return send(room, sender, type, make_iov(_content, content, contents.size(), contents));
}

ircd::m::event::id::buf
ircd::m::send(const room &room,
              const m::id::user &sender,
              const string_view &type,
              const json::iov &content)
{
	using prototype = event::id::buf (const m::room &, const id::user &, const string_view &, const json::iov &);

	static import<prototype> function
	{
		"client_rooms", "send__iov"
	};

	return function(room, sender, type, content);
}

ircd::m::event::id::buf
ircd::m::commit(const room &room,
                json::iov &event,
                const json::iov &contents)
{
	vm::copts opts
	{
		room.opts? *room.opts : vm::default_copts
	};

	// Some functionality on this server may create an event on behalf
	// of remote users. It's safe for us to mask this here, but eval'ing
	// this event in any replay later will require special casing.
	opts.non_conform |= event::conforms::MISMATCH_ORIGIN_SENDER;

	 // Stupid protocol workaround
	opts.non_conform |= event::conforms::MISSING_PREV_STATE;

	// Don't need this here
	opts.verify = false;

	vm::eval eval
	{
		opts
	};

	eval(room, event, contents);
	return eval.event_id;
}

ircd::m::id::room::buf
ircd::m::room_id(const id::room_alias &room_alias)
{
	char buf[256];
	return room_id(buf, room_alias);
}

ircd::m::id::room::buf
ircd::m::room_id(const string_view &room_id_or_alias)
{
	char buf[256];
	return room_id(buf, room_id_or_alias);
}

ircd::m::id::room
ircd::m::room_id(const mutable_buffer &out,
                 const string_view &room_id_or_alias)
{
	switch(m::sigil(room_id_or_alias))
	{
		case id::ROOM:
			return id::room{out, room_id_or_alias};

		default:
			return room_id(out, id::room_alias{room_id_or_alias});
	}
}

ircd::m::id::room
ircd::m::room_id(const mutable_buffer &out,
                 const id::room_alias &room_alias)
{
	using prototype = id::room (const mutable_buffer &, const id::room_alias &);

	static import<prototype> function
	{
		"client_directory_room", "room_id__room_alias"
	};

	return function(out, room_alias);
}

bool
ircd::m::exists(const id::room_alias &room_alias,
                const bool &remote_query)
{
	using prototype = bool (const id::room_alias, const bool &);

	static import<prototype> function
	{
		"client_directory_room", "room_alias_exists"
	};

	return function(room_alias, remote_query);
}

///////////////////////////////////////////////////////////////////////////////
//
// m/event.h
//

bool
ircd::m::visible(const event::id &event_id,
                 const node::id &origin)
{
	m::room::id::buf room_id
	{
		get(event_id, "room_id", room_id)
	};

	const m::event event
	{
		{ "event_id",  event_id  },
		{ "room_id",   room_id   }
	};

	return visible(event, origin);
}

bool
ircd::m::visible(const event::id &event_id,
                 const user::id &user_id)
{
	m::room::id::buf room_id
	{
		get(event_id, "room_id", room_id)
	};

	const m::event event
	{
		{ "event_id",  event_id  },
		{ "room_id",   room_id   }
	};

	return visible(event, user_id);
}

bool
ircd::m::visible(const event &event,
                 const node::id &origin)
{
	const m::room room
	{
		at<"room_id"_>(event), at<"event_id"_>(event)
	};

	return room.visible(origin);
}

bool
ircd::m::visible(const event &event,
                 const user::id &user_id)
{
	const m::room room
	{
		at<"room_id"_>(event), at<"event_id"_>(event)
	};

	return room.visible(user_id);
}

///////////////////////////////////////////////////////////////////////////////
//
// m/txn.h
//

/// Returns the serial size of the JSON this txn would consume. Note: this
/// creates a json::iov involving a timestamp to figure out the total size
/// of the txn. When the user creates the actual txn a different timestamp
/// is created which may be a different size. Consider using the lower-level
/// create(closure) or add some pad to be sure.
///
size_t
ircd::m::txn::serialized(const array &pdu,
                         const array &edu,
                         const array &pdu_failure)
{
	size_t ret;
	const auto closure{[&ret]
	(const json::iov &iov)
	{
		ret = json::serialized(iov);
	}};

	create(closure, pdu, edu, pdu_failure);
	return ret;
}

/// Stringifies a txn from the inputs into the returned std::string
///
std::string
ircd::m::txn::create(const array &pdu,
                     const array &edu,
                     const array &pdu_failure)
{
	std::string ret;
	const auto closure{[&ret]
	(const json::iov &iov)
	{
		ret = json::strung(iov);
	}};

	create(closure, pdu, edu, pdu_failure);
	return ret;
}

/// Stringifies a txn from the inputs into the buffer
///
ircd::string_view
ircd::m::txn::create(const mutable_buffer &buf,
                     const array &pdu,
                     const array &edu,
                     const array &pdu_failure)
{
	string_view ret;
	const auto closure{[&buf, &ret]
	(const json::iov &iov)
	{
		ret = json::stringify(mutable_buffer{buf}, iov);
	}};

	create(closure, pdu, edu, pdu_failure);
	return ret;
}

/// Forms a txn from the inputs into a json::iov and presents that iov
/// to the user's closure.
///
void
ircd::m::txn::create(const closure &closure,
                     const array &pdu,
                     const array &edu,
                     const array &pdu_failure)
{
	using ircd::size;

	json::iov iov;
	const json::iov::push push[]
	{
		{ iov, { "origin",            my_host()                   }},
		{ iov, { "origin_server_ts",  ircd::time<milliseconds>()  }},
	};

	const json::iov::add_if _pdus
	{
		iov, !empty(pdu),
		{
			"pdus", { data(pdu), size(pdu) }
		}
	};

	const json::iov::add_if _edus
	{
		iov, !empty(edu),
		{
			"edus", { data(edu), size(edu) }
		}
	};

	const json::iov::add_if _pdu_failures
	{
		iov, !empty(pdu_failure),
		{
			"pdu_failures", { data(pdu_failure), size(pdu_failure) }
		}
	};

	closure(iov);
}

ircd::string_view
ircd::m::txn::create_id(const mutable_buffer &out,
                        const string_view &txn)
{
	const sha256::buf hash
	{
		sha256{txn}
	};

	const string_view txnid
	{
		b58encode(out, hash)
	};

	return txnid;
}

///////////////////////////////////////////////////////////////////////////////
//
// m/hook.h
//

// Internal utils
namespace ircd::m
{
	static void _hook_fix_state_key(const json::members &, json::member &);
	static void _hook_fix_room_id(const json::members &, json::member &);
	static void _hook_fix_sender(const json::members &, json::member &);
	static json::strung _hook_make_feature(const json::members &);
}

/// Instance list linkage for all hooks
template<>
decltype(ircd::util::instance_list<ircd::m::hook<>>::list)
ircd::util::instance_list<ircd::m::hook<>>::list
{};

/// Alternative hook ctor simply allowing the the function argument
/// first and description after.
ircd::m::hook<>::hook(decltype(function) function,
                      const json::members &members)
:hook
{
	members, std::move(function)
}
{
}

/// Primary hook ctor
ircd::m::hook<>::hook(const json::members &members,
                      decltype(function) function)
try
:_feature
{
	_hook_make_feature(members)
}
,feature
{
	_feature
}
,matching
{
	feature
}
,function
{
	std::move(function)
}
{
	site *site;
	if((site = find_site()))
		site->add(*this);
}
catch(...)
{
	if(!registered)
		throw;

	auto *const site(find_site());
	assert(site != nullptr);
	site->del(*this);
}

/// Internal interface which manipulates the initializer supplied by the
/// developer to the hook to create the proper JSON output. i.e They supply
/// a "room_id" of "!config" which has no hostname, that is added here
/// depending on my_host() in the deployment runtime...
///
ircd::json::strung
ircd::m::_hook_make_feature(const json::members &members)
{
	const ctx::critical_assertion ca;
	std::vector<json::member> copy
	{
		begin(members), end(members)
	};

	for(auto &member : copy) switch(hash(member.first))
	{
		case hash("room_id"):
			_hook_fix_room_id(members, member);
			continue;

		case hash("sender"):
			_hook_fix_sender(members, member);
			continue;

		case hash("state_key"):
			_hook_fix_state_key(members, member);
			continue;
	}

	return { copy.data(), copy.data() + copy.size() };
}

void
ircd::m::_hook_fix_sender(const json::members &members,
                          json::member &member)
{
	// Rewrite the sender if the supplied input has no hostname
	if(valid_local_only(id::USER, member.second))
	{
		assert(my_host());
		thread_local char buf[256];
		member.second = id::user { buf, member.second, my_host() };
	}

	validate(id::USER, member.second);
}

void
ircd::m::_hook_fix_room_id(const json::members &members,
                           json::member &member)
{
	// Rewrite the room_id if the supplied input has no hostname
	if(valid_local_only(id::ROOM, member.second))
	{
		assert(my_host());
		thread_local char buf[256];
		member.second = id::room { buf, member.second, my_host() };
	}

	validate(id::ROOM, member.second);
}

void
ircd::m::_hook_fix_state_key(const json::members &members,
                             json::member &member)
{
	const bool is_member_event
	{
		end(members) != std::find_if(begin(members), end(members), []
		(const auto &member)
		{
			return member.first == "type" && member.second == "m.room.member";
		})
	};

	// Rewrite the sender if the supplied input has no hostname
	if(valid_local_only(id::USER, member.second))
	{
		assert(my_host());
		thread_local char buf[256];
		member.second = id::user { buf, member.second, my_host() };
	}

	validate(id::USER, member.second);
}

ircd::m::hook<>::~hook()
noexcept
{
	if(!registered)
		return;

	auto *const site(find_site());
	assert(site != nullptr);
	site->del(*this);
}

ircd::m::hook<>::site *
ircd::m::hook<>::find_site()
const
{
	const auto &site_name
	{
		this->site_name()
	};

	if(!site_name)
		return nullptr;

	for(auto *const &site : m::hook<>::site::list)
		if(site->name() == site_name)
			return site;

	return nullptr;
}

ircd::string_view
ircd::m::hook<>::site_name()
const try
{
	return unquote(feature.at("_site"));
}
catch(const std::out_of_range &e)
{
	throw assertive
	{
		"Hook %p must name a '_site' to register with.", this
	};
}

bool
ircd::m::hook<>::match(const m::event &event)
const
{
	if(json::get<"origin"_>(matching))
		if(at<"origin"_>(matching) != json::get<"origin"_>(event))
			return false;

	if(json::get<"room_id"_>(matching))
		if(at<"room_id"_>(matching) != json::get<"room_id"_>(event))
			return false;

	if(json::get<"sender"_>(matching))
		if(at<"sender"_>(matching) != json::get<"sender"_>(event))
			return false;

	if(json::get<"type"_>(matching))
		if(at<"type"_>(matching) != json::get<"type"_>(event))
			return false;

	if(json::get<"state_key"_>(matching))
		if(at<"state_key"_>(matching) != json::get<"state_key"_>(event))
			return false;

	if(json::get<"membership"_>(matching))
		if(at<"membership"_>(matching) != json::get<"membership"_>(event))
			return false;

	if(json::get<"content"_>(matching))
		if(json::get<"type"_>(event) == "m.room.message")
			if(at<"content"_>(matching).has("msgtype"))
				if(at<"content"_>(matching).get("msgtype") != json::get<"content"_>(event).get("msgtype"))
					return false;

	return true;
}

//
// hook::site
//

/// Instance list linkage for all hook sites
template<>
decltype(ircd::util::instance_list<ircd::m::hook<>::site>::list)
ircd::util::instance_list<ircd::m::hook<>::site>::list
{};

ircd::m::hook<>::site::site(const json::members &members)
:_feature
{
	members
}
,feature
{
	_feature
}
{
	for(const auto &site : list)
		if(site->name() == name() && site != this)
			throw error
			{
				"Hook site '%s' already registered at %p",
				name(),
				site
			};

	// Find and register all of the orphan hooks which were constructed before
	// this site was constructed.
	for(auto *const &hook : m::hook<>::list)
		if(hook->site_name() == name())
			add(*hook);
}

ircd::m::hook<>::site::~site()
noexcept
{
	const std::vector<hook *> hooks
	{
		begin(this->hooks), end(this->hooks)
	};

	for(auto *const hook : hooks)
		del(*hook);
}

void
ircd::m::hook<>::site::operator()(const event &event)
{
	std::set<hook *> matching //TODO: allocator
	{
		begin(always), end(always)
	};

	const auto site_match{[&matching]
	(auto &map, const string_view &key)
	{
		auto pit{map.equal_range(key)};
		for(; pit.first != pit.second; ++pit.first)
			matching.emplace(pit.first->second);
	}};

	if(json::get<"origin"_>(event))
		site_match(origin, at<"origin"_>(event));

	if(json::get<"room_id"_>(event))
		site_match(room_id, at<"room_id"_>(event));

	if(json::get<"sender"_>(event))
		site_match(sender, at<"sender"_>(event));

	if(json::get<"type"_>(event))
		site_match(type, at<"type"_>(event));

	if(json::get<"state_key"_>(event))
		site_match(state_key, at<"state_key"_>(event));

	auto it(begin(matching));
	while(it != end(matching))
	{
		const hook &hook(**it);
		if(!hook.match(event))
			it = matching.erase(it);
		else
			++it;
	}

	for(const auto &hook : matching)
		call(*hook, event);
}

void
ircd::m::hook<>::site::call(hook &hook,
                            const event &event)
try
{
	++hook.calls;
	hook.function(event);
}
catch(const std::exception &e)
{
	log::critical
	{
		"Unhandled hookfn(%p) %s error :%s",
		&hook,
		string_view{hook.feature},
		e.what()
	};
}

bool
ircd::m::hook<>::site::add(hook &hook)
{
	assert(!hook.registered);
	assert(hook.site_name() == name());
	assert(hook.matchers == 0);

	if(!hooks.emplace(&hook).second)
	{
		log::warning
		{
			"Hook %p already registered to site %s", &hook, name()
		};

		return false;
	}

	const auto map{[&hook]
	(auto &map, const string_view &value)
	{
		map.emplace(value, &hook);
		++hook.matchers;
	}};

	if(json::get<"origin"_>(hook.matching))
		map(origin, at<"origin"_>(hook.matching));

	if(json::get<"room_id"_>(hook.matching))
		map(room_id, at<"room_id"_>(hook.matching));

	if(json::get<"sender"_>(hook.matching))
		map(sender, at<"sender"_>(hook.matching));

	if(json::get<"state_key"_>(hook.matching))
		map(state_key, at<"state_key"_>(hook.matching));

	if(json::get<"type"_>(hook.matching))
		map(type, at<"type"_>(hook.matching));

	// Hook had no mappings which means it will match everything.
	// We don't increment the matcher count for this case.
	if(!hook.matchers)
		always.emplace_back(&hook);

	++count;
	hook.registered = true;
	return true;
}

bool
ircd::m::hook<>::site::del(hook &hook)
{
	assert(hook.registered);
	assert(hook.site_name() == name());

	const auto unmap{[&hook]
	(auto &map, const string_view &key)
	{
		auto pit{map.equal_range(key)};
		for(; pit.first != pit.second; ++pit.first)
			if(pit.first->second == &hook)
			{
				--hook.matchers;
				return map.erase(pit.first);
			}

		assert(0);
		return end(map);
	}};

	// Unconditional attempt to remove from always.
	std::remove(begin(always), end(always), &hook);

	if(json::get<"origin"_>(hook.matching))
		unmap(origin, at<"origin"_>(hook.matching));

	if(json::get<"room_id"_>(hook.matching))
		unmap(room_id, at<"room_id"_>(hook.matching));

	if(json::get<"sender"_>(hook.matching))
		unmap(sender, at<"sender"_>(hook.matching));

	if(json::get<"state_key"_>(hook.matching))
		unmap(state_key, at<"state_key"_>(hook.matching));

	if(json::get<"type"_>(hook.matching))
		unmap(type, at<"type"_>(hook.matching));

	const auto erased
	{
		hooks.erase(&hook)
	};

	assert(erased);
	assert(hook.matchers == 0);
	--count;
	hook.registered = false;
	return true;
}

ircd::string_view
ircd::m::hook<>::site::name()
const try
{
	return unquote(feature.at("name"));
}
catch(const std::out_of_range &e)
{
	throw assertive
	{
		"Hook site %p requires a name", this
	};
}

///////////////////////////////////////////////////////////////////////////////
//
// m/import.h
//

decltype(ircd::m::modules)
ircd::m::modules
{};

///////////////////////////////////////////////////////////////////////////////
//
// m/error.h
//

thread_local char
ircd::m::error::fmtbuf[768]
{};

ircd::m::error::error()
:http::error{http::INTERNAL_SERVER_ERROR}
{}

ircd::m::error::error(std::string c)
:http::error{http::INTERNAL_SERVER_ERROR, std::move(c)}
{}

ircd::m::error::error(const http::code &c)
:http::error{c, std::string{}}
{}

ircd::m::error::error(const http::code &c,
                      const json::members &members)
:http::error{c, json::strung{members}}
{}

ircd::m::error::error(const http::code &c,
                      const json::iov &iov)
:http::error{c, json::strung{iov}}
{}

ircd::m::error::error(const http::code &c,
                      const json::object &object)
:http::error{c, std::string{object}}
{}
