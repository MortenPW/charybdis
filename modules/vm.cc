// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

namespace ircd::m::vm
{
	extern log::log log;
	extern hook<>::site commit_hook;
	extern hook<>::site eval_hook;
	extern hook<>::site notify_hook;

	static void write_commit(eval &);
	static fault _eval_edu(eval &, const event &);
	static fault _eval_pdu(eval &, const event &);

	extern "C" fault eval__event(eval &, const event &);
	extern "C" fault eval__commit(eval &, json::iov &, const json::iov &);
	extern "C" fault eval__commit_room(eval &, const room &, json::iov &, const json::iov &);

	static void init();
	static void fini();
}

ircd::mapi::header
IRCD_MODULE
{
	"Matrix Virtual Machine",
	ircd::m::vm::init, ircd::m::vm::fini
};

decltype(ircd::m::vm::log)
ircd::m::vm::log
{
	"vm", 'v'
};

decltype(ircd::m::vm::commit_hook)
ircd::m::vm::commit_hook
{
	{ "name", "vm.commit" }
};

decltype(ircd::m::vm::eval_hook)
ircd::m::vm::eval_hook
{
	{ "name", "vm.eval" }
};

decltype(ircd::m::vm::notify_hook)
ircd::m::vm::notify_hook
{
	{ "name", "vm.notify" }
};

//
// init
//

void
ircd::m::vm::init()
{
	id::event::buf event_id;
	current_sequence = retired_sequence(event_id);

	log::info
	{
		log, "BOOT %s @%lu [%s]",
		string_view{m::my_node.node_id},
		current_sequence,
		current_sequence? string_view{event_id} : "NO EVENTS"_sv
	};
}

void
ircd::m::vm::fini()
{
	assert(eval::list.empty());

	id::event::buf event_id;
	const auto current_sequence
	{
		retired_sequence(event_id)
	};

	log::info
	{
		log, "HLT '%s' @%lu [%s]",
		string_view{m::my_node.node_id},
		current_sequence,
		current_sequence? string_view{event_id} : "NO EVENTS"_sv
	};
}

//
// eval
//

enum ircd::m::vm::fault
ircd::m::vm::eval__commit_room(eval &eval,
                               const room &room,
                               json::iov &event,
                               const json::iov &contents)
{
	// These conditions come from the m::eval linkage in ircd/m.cc
	assert(eval.issue);
	assert(eval.room_id);
	assert(eval.copts);
	assert(eval.opts);
	assert(eval.phase == eval.ENTER);
	assert(room.room_id);

	const auto &opts
	{
		*eval.copts
	};

	const json::iov::push room_id
	{
		event, { "room_id", room.room_id }
	};

	int64_t depth{-1}, prev_cnt{0};
	std::string prev_events(8192, char());
	json::stack ps
	{
		mutable_buffer{prev_events}
	};

	{
		json::stack::array top{ps};
		room::head{room}.for_each(room::head::closure_bool{[&top, &depth, &prev_cnt]
		(const event::idx &idx, const event::id &event_id)
		{
			const m::event::fetch event{idx, std::nothrow};
			if(!event.valid)
				return true;

			depth = std::max(at<"depth"_>(event), depth);
			json::stack::array prev{top};
			prev.append(event_id);
			{
				json::stack::object hash{prev};
				json::stack::member will{hash, "willy", "nilly"};
			}

			return ++prev_cnt < 16;
		}});

		if(!prev_cnt && event.at("type") != "m.room.create")
		{
			const auto t(m::top(room));
			depth = std::get<1>(t);
			json::stack::array prev{top};
			prev.append(std::get<0>(t));
			{
				json::stack::object hash{prev};
				json::stack::member will{hash, "willy", "nilly"};
			}
		}
	}

	prev_events.resize(size(ps.completed()));
	json::valid(prev_events);

	//TODO: X
	const json::iov::set_if depth_
	{
		event, !event.has("depth"),
		{
			"depth", depth + 1
		}
	};

	char ae_buf[512];
	json::array auth_events;

	if(depth != -1 && opts.add_auth_events)
	{
		std::vector<json::value> ae;

		room.get(std::nothrow, "m.room.create", "", [&ae]
		(const m::event &event)
		{
			const json::value ae0[]
			{
				{ json::get<"event_id"_>(event) },
				{ json::get<"hashes"_>(event)   }
			};

			const json::value ae_
			{
				ae0, 2
			};

			ae.emplace_back(ae_);
		});

		room.get(std::nothrow, "m.room.join_rules", "", [&ae]
		(const m::event &event)
		{
			const json::value ae0[]
			{
				{ json::get<"event_id"_>(event) },
				{ json::get<"hashes"_>(event)   }
			};

			const json::value ae_
			{
				ae0, 2
			};

			ae.emplace_back(ae_);
		});

		room.get(std::nothrow, "m.room.power_levels", "", [&ae]
		(const m::event &event)
		{
			const json::value ae0[]
			{
				{ json::get<"event_id"_>(event) },
				{ json::get<"hashes"_>(event)   }
			};

			const json::value ae_
			{
				ae0, 2
			};

			ae.emplace_back(ae_);
		});

		if(event.at("type") != "m.room.member")
			room.get(std::nothrow, "m.room.member", event.at("sender"), [&ae]
			(const m::event &event)
			{
				const json::value ae0[]
				{
					{ json::get<"event_id"_>(event) },
					{ json::get<"hashes"_>(event)   }
				};

				const json::value ae_
				{
					ae0, 2
				};

				ae.emplace_back(ae_);
			});

		auth_events = json::stringify(mutable_buffer{ae_buf}, json::value
		{
			ae.data(), ae.size()
		});
	}

	static const json::array &prev_state
	{
		json::empty_array
	};

	const json::iov::add_if prevs[]
	{
		{
			event, opts.add_auth_events,
			{
				"auth_events", auth_events
			}
		},
		{
			event, opts.add_prev_events,
			{
				"prev_events", prev_events
			}
		},
		{
			event, opts.add_prev_state,
			{
				"prev_state", prev_state
			}
		},
	};

	return eval(event, contents);
}

enum ircd::m::vm::fault
ircd::m::vm::eval__commit(eval &eval,
                          json::iov &event,
                          const json::iov &contents)
{
	// These conditions come from the m::eval linkage in ircd/m.cc
	assert(eval.issue);
	assert(eval.copts);
	assert(eval.opts);
	assert(eval.copts);

	const auto &opts
	{
		*eval.copts
	};

	const json::iov::add_if _origin
	{
		event, opts.add_origin,
		{
			"origin", my_host()
		}
	};

	const json::iov::add_if _origin_server_ts
	{
		event, opts.add_origin_server_ts,
		{
			"origin_server_ts", ircd::time<milliseconds>()
		}
	};

	const json::strung content
	{
		contents
	};

	// event_id

	sha256::buf event_id_hash;
	if(opts.add_event_id)
	{
		const json::iov::push _content
		{
			event, { "content", content },
		};

		thread_local char preimage_buf[64_KiB];
		event_id_hash = sha256
		{
			stringify(mutable_buffer{preimage_buf}, event)
		};
	}

	const string_view event_id
	{
		opts.add_event_id?
			make_id(event, eval.event_id, event_id_hash):
			string_view{}
	};

	const json::iov::add_if _event_id
	{
		event, opts.add_event_id,
		{
			"event_id", event_id
		}
	};

	// hashes

	char hashes_buf[128];
	const string_view hashes
	{
		opts.add_hash?
			m::event::hashes(hashes_buf, event, content):
			string_view{}
	};

	const json::iov::add_if _hashes
	{
		event, opts.add_hash,
		{
			"hashes", hashes
		}
	};

	// sigs

	char sigs_buf[384];
	const string_view sigs
	{
		opts.add_sig?
			m::event::signatures(sigs_buf, event, contents):
			string_view{}
	};

	const json::iov::add_if _sigs
	{
		event, opts.add_sig,
		{
			"signatures",  sigs
		}
	};

	const json::iov::push _content
	{
		event, { "content", content },
	};

	return eval(event);
}

enum ircd::m::vm::fault
ircd::m::vm::eval__event(eval &eval,
                         const event &event)
try
{
	// These conditions come from the m::eval linkage in ircd/m.cc
	assert(eval.opts);
	assert(eval.event_);
	assert(eval.id);
	assert(eval.ctx);

	const auto &opts
	{
		*eval.opts
	};

	if(eval.copts)
	{
		if(unlikely(!my_host(at<"origin"_>(event))))
			throw error
			{
				fault::GENERAL, "Committing event for origin: %s", at<"origin"_>(event)
			};

		if(eval.copts->debuglog_precommit)
			log.debug("injecting event(mark +%ld) %s",
			          vm::current_sequence,
			          pretty_oneline(event));

		check_size(event);
		commit_hook(event);
	}

	const event::conforms &report
	{
		opts.conforming && !opts.conformed?
			event::conforms{event, opts.non_conform.report}:
			opts.report
	};

	if(opts.conforming && !report.clean())
		throw error
		{
			fault::INVALID, "Non-conforming event: %s", string(report)
		};

	// A conforming (with lots of masks) event without an event_id is an EDU.
	const fault ret
	{
		json::get<"event_id"_>(event)?
			_eval_pdu(eval, event):
			_eval_edu(eval, event)
	};

	if(ret != fault::ACCEPT)
		return ret;

	vm::accepted accepted
	{
		event, &opts, &report
	};

	if(opts.effects)
		notify_hook(event);

	if(opts.notify)
		vm::accept(accepted);

	if(opts.debuglog_accept)
		log.debug("%s", pretty_oneline(event));

	if(opts.infolog_accept)
		log.info("%s", pretty_oneline(event));

	return ret;
}
catch(const ctx::interrupted &e) // INTERRUPTION
{
	if(eval.opts->errorlog & fault::INTERRUPT)
		log::error
		{
			log, "eval %s: #NMI: %s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what()
		};

	if(eval.opts->warnlog & fault::INTERRUPT)
		log::warning
		{
			log, "eval %s: #NMI: %s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what()
		};

	throw;
}
catch(const error &e) // VM FAULT CODE
{
	if(eval.opts->errorlog & e.code)
		log::error
		{
			log, "eval %s: %s %s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what(),
			e.content
		};

	if(eval.opts->warnlog & e.code)
		log::warning
		{
			log, "eval %s: %s %s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what(),
			e.content
		};

	if(eval.opts->nothrows & e.code)
		return e.code;

	throw;
}
catch(const m::error &e) // GENERAL MATRIX ERROR
{
	if(eval.opts->errorlog & fault::GENERAL)
		log::error
		{
			log, "eval %s: #GP: %s :%s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what(),
			e.content
		};

	if(eval.opts->warnlog & fault::GENERAL)
		log::warning
		{
			log, "eval %s: #GP: %s :%s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what(),
			e.content
		};

	if(eval.opts->nothrows & fault::GENERAL)
		return fault::GENERAL;

	throw;
}
catch(const std::exception &e) // ALL OTHER ERRORS
{
	if(eval.opts->errorlog & fault::GENERAL)
		log::error
		{
			log, "eval %s: #GP: %s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what()
		};

	if(eval.opts->warnlog & fault::GENERAL)
		log::warning
		{
			log, "eval %s: #GP: %s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what()
		};

	if(eval.opts->nothrows & fault::GENERAL)
		return fault::GENERAL;

	throw error
	{
		fault::GENERAL, "%s", e.what()
	};
}

enum ircd::m::vm::fault
ircd::m::vm::_eval_edu(eval &eval,
                       const event &event)
{
	eval_hook(event);
	return fault::ACCEPT;
}

enum ircd::m::vm::fault
ircd::m::vm::_eval_pdu(eval &eval,
                       const event &event)
{
	assert(eval.opts);
	const auto &opts
	{
		*eval.opts
	};

	const m::event::id &event_id
	{
		at<"event_id"_>(event)
	};

	const m::room::id &room_id
	{
		at<"room_id"_>(event)
	};

	const string_view &type
	{
		at<"type"_>(event)
	};

	if(!opts.replays && exists(event_id))  //TODO: exclusivity
		throw error
		{
			fault::EXISTS, "Event has already been evaluated."
		};

	if(opts.verify)
		if(!verify(event))
			throw m::BAD_SIGNATURE
			{
				"Signature verification failed"
			};

	const size_t reserve_bytes
	{
		opts.reserve_bytes == size_t(-1)?
			json::serialized(event):
			opts.reserve_bytes
	};

	// Obtain sequence number here
	eval.sequence = ++vm::current_sequence;

	eval_hook(event);

	const event::prev prev
	{
		event
	};

	const size_t prev_count
	{
		size(json::get<"prev_events"_>(prev))
	};

	int64_t top;
	id::event::buf head;
	std::tie(head, top, std::ignore) = m::top(std::nothrow, room_id);
	if(top < 0 && (opts.head_must_exist || opts.history))
		if(type != "m.room.create")
			throw error
			{
				fault::STATE, "Found nothing for room %s", string_view{room_id}
			};

	for(size_t i(0); i < prev_count; ++i)
	{
		const auto prev_id{prev.prev_event(i)};
		if(opts.prev_check_exists && !exists(prev_id))
			throw error
			{
				fault::EVENT, "Missing prev event %s", string_view{prev_id}
			};
	}

	if(!opts.write)
		return fault::ACCEPT;

	db::txn txn
	{
		*dbs::events, db::txn::opts
		{
			reserve_bytes + opts.reserve_index,   // reserve_bytes
			0,                                    // max_bytes (no max)
		}
	};

	// Expose to eval interface
	eval.txn = &txn;
	const unwind clear{[&eval]
	{
		eval.txn = nullptr;
	}};

	// Preliminary write_opts
	m::dbs::write_opts wopts;
	wopts.present = opts.present;
	wopts.history = opts.history;
	wopts.head = opts.head;
	wopts.refs = opts.refs;
	wopts.event_idx = eval.sequence;

	m::state::id_buffer new_root_buf;
	wopts.root_out = new_root_buf;
	string_view new_root;
	if(prev_count)
	{
		m::room room{room_id, head};
		m::room::state state{room};
		wopts.root_in = state.root_id;
		new_root = dbs::write(txn, event, wopts);
	}
	else
	{
		new_root = dbs::write(txn, event, wopts);
	}

	write_commit(eval);
	return fault::ACCEPT;
}

void
ircd::m::vm::write_commit(eval &eval)
{
	assert(eval.txn);
	auto &txn(*eval.txn);
	if(eval.opts->debuglog_accept)
		log::debug
		{
			log, "Committing %zu cells in %zu bytes to events database...",
			txn.size(),
			txn.bytes()
		};

	txn();
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
