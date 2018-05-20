// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#pragma once
#define HAVE_IRCD_M_ROOM_H
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsubobject-linkage"

namespace ircd::m
{
	IRCD_M_EXCEPTION(m::error, CONFLICT, http::CONFLICT);
	IRCD_M_EXCEPTION(m::error, NOT_MODIFIED, http::NOT_MODIFIED);
	IRCD_M_EXCEPTION(CONFLICT, ALREADY_MEMBER, http::CONFLICT);

	struct room;

	// Util
	bool my(const room &);

	// [GET] Util
	bool exists(const room &);
	bool exists(const id::room &);
	bool exists(const id::room_alias &, const bool &remote = false);

	// [GET]
	id::room room_id(const mutable_buffer &, const id::room_alias &);
	id::room room_id(const mutable_buffer &, const string_view &id_or_alias);
	id::room::buf room_id(const id::room_alias &);
	id::room::buf room_id(const string_view &id_or_alias);

	// [GET] Current Event ID and depth suite (non-locking) (one only)
	std::tuple<id::event::buf, int64_t, event::idx> top(std::nothrow_t, const id::room &);
	std::tuple<id::event::buf, int64_t, event::idx> top(const id::room &);

	// [GET] Current Event ID (non-locking) (one only)
	id::event::buf head(std::nothrow_t, const id::room &);
	id::event::buf head(const id::room &);

	// [GET] Current Event idx (non-locking) (one only)
	event::idx head_idx(std::nothrow_t, const id::room &);
	event::idx head_idx(const id::room &);

	// [GET] Current Event depth (non-locking)
	int64_t depth(std::nothrow_t, const id::room &);
	int64_t depth(const id::room &);

	// [SET] Lowest-level
	event::id::buf commit(const room &, json::iov &event, const json::iov &content);

	// [SET] Send state to room
	event::id::buf send(const room &, const m::id::user &sender, const string_view &type, const string_view &state_key, const json::iov &content);
	event::id::buf send(const room &, const m::id::user &sender, const string_view &type, const string_view &state_key, const json::members &content);
	event::id::buf send(const room &, const m::id::user &sender, const string_view &type, const string_view &state_key, const json::object &content);

	// [SET] Send non-state to room
	event::id::buf send(const room &, const m::id::user &sender, const string_view &type, const json::iov &content);
	event::id::buf send(const room &, const m::id::user &sender, const string_view &type, const json::members &content);
	event::id::buf send(const room &, const m::id::user &sender, const string_view &type, const json::object &content);

	// [SET] Convenience sends
	event::id::buf redact(const room &, const m::id::user &sender, const m::id::event &, const string_view &reason);
	event::id::buf message(const room &, const m::id::user &sender, const json::members &content);
	event::id::buf message(const room &, const m::id::user &sender, const string_view &body, const string_view &msgtype = "m.text");
	event::id::buf msghtml(const room &, const m::id::user &sender, const string_view &html, const string_view &alt = {}, const string_view &msgtype = "m.notice");
	event::id::buf notice(const room &, const m::id::user &sender, const string_view &body);
	event::id::buf notice(const room &, const string_view &body); // sender is @ircd
	event::id::buf invite(const room &, const m::id::user &target, const m::id::user &sender);
	event::id::buf leave(const room &, const m::id::user &);
	event::id::buf join(const room &, const m::id::user &);
	event::id::buf join(const id::room_alias &, const m::id::user &);

	// [SET] Create new room
	room create(const id::room &, const id::user &creator, const id::room &parent, const string_view &type);
	room create(const id::room &, const id::user &creator, const string_view &type = {});
}

/// Interface to a room.
///
/// This is a lightweight object which uses a room_id and an optional event_id
/// to provide an interface to a matrix room. This object itself isn't the
/// actual room data since that takes the form of events in the database;
/// this is just a handle with aforementioned string_view's used by its member
/// functions.
///
/// This object allows the programmer to represent the room either at its
/// present state, or if an event_id is given, at the point of that event.
///
/// Many convenience functions are provided outside of this class.
/// Additionally, several sub-classes provide functionality even more specific
/// than this interface too. If a subclass is provided, for example:
/// `struct members`, such an interface may employ optimized tactics for its
/// specific task.
///
struct ircd::m::room
{
	struct messages;
	struct state;
	struct members;
	struct origins;
	struct head;

	using id = m::id::room;
	using alias = m::id::room_alias;
	using closure = std::function<void (const room &)>;
	using closure_bool = std::function<bool (const room &)>;

	id room_id;
	event::id event_id;
	const vm::copts *opts {nullptr};

	operator const id &() const;

	// Convenience passthru to room::messages (linear query; newest first)
	bool for_each(const string_view &type, const event::closure_idx_bool &) const;
	void for_each(const string_view &type, const event::closure_idx &) const;
	bool for_each(const string_view &type, const event::id::closure_bool &) const;
	void for_each(const string_view &type, const event::id::closure &) const;
	bool for_each(const string_view &type, const event::closure_bool &) const;
	void for_each(const string_view &type, const event::closure &) const;
	bool for_each(const event::closure_idx_bool &) const;
	void for_each(const event::closure_idx &) const;
	bool for_each(const event::id::closure_bool &) const;
	void for_each(const event::id::closure &) const;
	bool for_each(const event::closure_bool &) const;
	void for_each(const event::closure &) const;

	// Convenience passthru to room::state (logarithmic query)
	bool has(const string_view &type, const string_view &state_key) const;
	bool get(std::nothrow_t, const string_view &type, const string_view &state_key, const event::closure &) const;
	void get(const string_view &type, const string_view &state_key, const event::closure &) const;

	// Convenience passthru to room::messages (linear query)
	bool get(std::nothrow_t, const string_view &type, const event::closure &) const;
	void get(const string_view &type, const event::closure &) const;
	bool has(const string_view &type) const;

	// misc
	bool membership(const m::id::user &, const string_view &membership = "join") const;
	string_view membership(const mutable_buffer &out, const m::id::user &) const;
	bool visible(const id::user &) const;
	bool visible(const id::node &) const;

	room(const id &room_id, const string_view &event_id, const vm::copts *const &opts = nullptr)
	:room_id{room_id}
	,event_id{event_id? event::id{event_id} : event::id{}}
	,opts{opts}
	{}

	room(const id &room_id, const vm::copts *const &opts = nullptr)
	:room_id{room_id}
	,opts{opts}
	{}

	room() = default;
};

/// Interface to room messages
///
/// This interface has the form of an STL-style iterator over room messages
/// which are state and non-state events from all integrated timelines.
/// Moving the iterator is cheap, but the dereference operators fetch a
/// full event. One can iterate just event_idx's by using event_idx() instead
/// of the dereference operators.
///
struct ircd::m::room::messages
{
	m::room room;
	db::index::const_iterator it;
	event::idx _event_idx;
	event::fetch _event;

  public:
	operator bool() const              { return bool(it);                      }
	bool operator!() const             { return !it;                           }

	event::id::buf event_id();         // deprecated; will remove
	const event::idx &event_idx();
	string_view state_root() const;

	const m::event &fetch(std::nothrow_t);
	const m::event &fetch();

	bool seek(const uint64_t &depth);
	bool seek(const event::id &);
	bool seek();

	// These are reversed on purpose
	auto &operator++()                 { return --it;                          }
	auto &operator--()                 { return ++it;                          }

	const m::event &operator*();
	const m::event *operator->()       { return &operator*();                  }

	messages(const m::room &room, const uint64_t &depth);
	messages(const m::room &room, const event::id &);
	messages(const m::room &room);
	messages() = default;
	messages(const messages &) = delete;
	messages &operator=(const messages &) = delete;
};

/// Interface to room state.
///
/// This interface focuses specifically on the details of room state. Most of
/// the queries to this interface respond in logarithmic time. If an event with
/// a state_key is present in room::messages but it is not present in
/// room::state (state tree) it was accepted into the room but we will not
/// apply it to our machine, though other parties may (this is a
/// state-conflict).
///
struct ircd::m::room::state
{
	struct tuple;
	struct opts;

	using keys = std::function<void (const string_view &)>;
	using keys_bool = std::function<bool (const string_view &)>;

	room::id room_id;
	event::id::buf event_id;
	m::state::id_buffer root_id_buf;
	m::state::id root_id;

	bool present() const;

	// Iterate the state; for_each protocol
	void for_each(const string_view &type, const keys &) const;
	void for_each(const string_view &type, const event::closure_idx &) const;
	void for_each(const string_view &type, const event::id::closure &) const;
	void for_each(const string_view &type, const event::closure &) const;
	void for_each(const event::closure_idx &) const;
	void for_each(const event::id::closure &) const;
	void for_each(const event::closure &, const event::keys::selection &) const;
	void for_each(const event::closure &) const;

	// Iterate the state; test protocol
	bool test(const string_view &type, const string_view &lower_bound, const event::closure_idx_bool &view) const;
	bool test(const string_view &type, const string_view &lower_bound, const event::id::closure_bool &view) const;
	bool test(const string_view &type, const string_view &lower_bound, const event::closure_bool &view) const;
	bool test(const string_view &type, const keys_bool &view) const;
	bool test(const string_view &type, const event::closure_idx_bool &view) const;
	bool test(const string_view &type, const event::id::closure_bool &view) const;
	bool test(const string_view &type, const event::closure_bool &view) const;
	bool test(const event::closure_idx_bool &view) const;
	bool test(const event::id::closure_bool &view) const;
	bool test(const event::closure_bool &view) const;

	// Counting / Statistics
	size_t count(const string_view &type) const;
	size_t count() const;

	// Existential
	bool has(const string_view &type, const string_view &state_key) const;
	bool has(const string_view &type) const;

	// Fetch a state event
	bool get(std::nothrow_t, const string_view &type, const string_view &state_key, const event::closure_idx &) const;
	bool get(std::nothrow_t, const string_view &type, const string_view &state_key, const event::id::closure &) const;
	bool get(std::nothrow_t, const string_view &type, const string_view &state_key, const event::closure &) const;
	void get(const string_view &type, const string_view &state_key, const event::closure_idx &) const;
	void get(const string_view &type, const string_view &state_key, const event::id::closure &) const;
	void get(const string_view &type, const string_view &state_key, const event::closure &) const;

	// Fetch and return state event id
	event::id::buf get(std::nothrow_t, const string_view &type, const string_view &state_key = "") const;
	event::id::buf get(const string_view &type, const string_view &state_key = "") const;

	state(const m::room &, const opts &);
	state(const m::room &);
	state() = default;
	state(const state &) = delete;
	state &operator=(const state &) = delete;
};

struct ircd::m::room::state::opts
{
	/// If true, the state btree at the present state may become the source of
	/// the data for this interface. This is only significant if no event_id
	/// was specified in the room object, otherwise snapshot is implied at that
	/// event. If false the faster sequential present state table is used; note
	/// that the present state may change while you use this object.
	///
	/// Also note that passing an event_id (implied snapshot) may still take
	/// advantage of a rocksdb snapshot of the present state table and not the
	/// btree as an optimization if possible.
	bool snapshot {false};
};

/// Interface to the members of a room.
///
/// This interface focuses specifically on room membership and its routines
/// are optimized for this area of room functionality.
///
struct ircd::m::room::members
{
	using closure = std::function<void (const id::user &)>;
	using closure_bool = std::function<bool (const id::user &)>;

	m::room room;

	size_t count() const;
	size_t count(const string_view &membership) const;

	bool test(const string_view &membership, const event::closure_bool &view) const;
	bool test(const event::closure_bool &view) const;

	void for_each(const string_view &membership, const event::closure &) const;
	bool for_each(const string_view &membership, const closure_bool &) const;
	void for_each(const string_view &membership, const closure &) const;
	void for_each(const event::closure &) const;
	bool for_each(const closure_bool &) const;
	void for_each(const closure &) const;

	members(const m::room &room)
	:room{room}
	{}
};

/// Interface to the origins (autonomous systems) of a room
///
/// This interface focuses specifically on the origins (from the field in the
/// event object) which are servers/networks/autonomous systems, or something.
/// Messages have to be sent to them, and an efficient iteration of the
/// origins as provided by this interface helps with that.
///
struct ircd::m::room::origins
{
	using closure = std::function<void (const string_view &)>;
	using closure_bool = std::function<bool (const string_view &)>;

	m::room room;

	bool _test_(const closure_bool &view) const;
	bool test(const closure_bool &view) const;
	void for_each(const closure &view) const;
	bool has(const string_view &origin) const;
	size_t count() const;

	origins(const m::room &room)
	:room{room}
	{}
};

/// Interface to the room head
///
/// This interface helps compute and represent aspects of the room graph,
/// specifically concerning the "head" or the "front" or the "top" of this
/// graph where events are either furthest from the m.room.create genesis,
/// or are yet unreferenced by another event. Usage of this interface is
/// fundamental when composing the references of a new event on the graph.
///
struct ircd::m::room::head
{
	using closure = std::function<void (const event::idx &, const event::id &)>;
	using closure_bool = std::function<bool (const event::idx &, const event::id &)>;

	m::room room;

	bool for_each(const closure_bool &) const;
	void for_each(const closure &) const;
	bool has(const event::id &) const;
	size_t count() const;

	head(const m::room &room)
	:room{room}
	{}
};

/// Tuple to represent fundamental room state singletons (state_key = "")
///
/// This is not a complete representation of room state. Missing from here
/// are any state events with a state_key which is not an empty string. When
/// the state_key is not "", multiple events of that type contribute to the
/// room's eigenvalue. Additionally, state events which are not related to
/// the matrix protocol `m.room.*` are not represented here.
///
/// NOTE that in C++-land state_key="" is represented as a valid but empty
/// character pointer. It is not a string containing "". Testing for a
/// "" state_key `ircd::defined(string_view) && ircd::empty(string_view)`
/// analogous to `data() && !*data()`. A default constructed string_view{}
/// is considered "JS undefined" because the character pointers are null.
/// A "JS null" is rarer and carried with a hack which will not be discussed
/// here.
///
struct ircd::m::room::state::tuple
:json::tuple
<
	json::property<name::m_room_aliases, event>,
	json::property<name::m_room_canonical_alias, event>,
	json::property<name::m_room_create, event>,
	json::property<name::m_room_join_rules, event>,
	json::property<name::m_room_power_levels, event>,
	json::property<name::m_room_message, event>,
	json::property<name::m_room_name, event>,
	json::property<name::m_room_topic, event>,
	json::property<name::m_room_avatar, event>,
	json::property<name::m_room_pinned_events, event>,
	json::property<name::m_room_history_visibility, event>,
	json::property<name::m_room_third_party_invite, event>,
	json::property<name::m_room_guest_access, event>
>
{
	using super_type::tuple;

	tuple(const json::array &pdus);
	tuple(const m::room &, const mutable_buffer &);
	tuple() = default;
	using super_type::operator=;

	friend std::string pretty(const room::state::tuple &);
	friend std::string pretty_oneline(const room::state::tuple &);
};

#pragma GCC diagnostic pop

inline ircd::m::room::operator
const ircd::m::room::id &()
const
{
	return room_id;
}
