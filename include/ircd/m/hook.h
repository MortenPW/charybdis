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
#define HAVE_IRCD_M_HOOK_H

namespace ircd::m
{
	template<class data = void> struct hook;    // doesn't exist.
	template<> struct hook<void>;               // base of hook system
}

template<>
struct ircd::m::hook<>
:instance_list<hook<>>
{
	struct site;

	IRCD_EXCEPTION(ircd::error, error)

	json::strung _feature;
	json::object feature;
	m::event matching;
	std::function<void (const m::event &)> function;
	bool registered {false};
	size_t matchers {0};
	size_t calls {0};

	string_view site_name() const;
	site *find_site() const;

	bool match(const m::event &) const;

 public:
	hook(const json::members &, decltype(function));
	hook(decltype(function), const json::members &);
	hook(hook &&) = delete;
	hook(const hook &) = delete;
	virtual ~hook() noexcept;
};

/// The hook::site is the call-site for a hook. Each hook site is named
/// and registers itself with the master extern hook::site::list. Each hook
/// then registers itself with a hook::site. The site contains internal
/// state to manage the efficient calling of the participating hooks.
///
/// A hook::site can be created or destroyed at any time (for example if it's
/// in a module which is reloaded) while being agnostic to the hooks it
/// cooperates with.
struct ircd::m::hook<>::site
:instance_list<site>
{
	json::strung _feature;
	json::object feature;
	size_t count {0};

	string_view name() const;

	std::multimap<string_view, hook *> origin;
	std::multimap<string_view, hook *> room_id;
	std::multimap<string_view, hook *> sender;
	std::multimap<string_view, hook *> state_key;
	std::multimap<string_view, hook *> type;
	std::vector<hook *> always;
	std::set<hook *> hooks;

	friend class hook;
	bool add(hook &);
	bool del(hook &);

	void call(hook &, const event &);

  public:
	void operator()(const event &);

	site(const json::members &);
	site(site &&) = delete;
	site(const site &) = delete;
	virtual ~site() noexcept;
};
