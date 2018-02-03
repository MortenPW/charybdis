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
#define HAVE_IRCD_NET_DNS_H

namespace ircd::net
{
	struct dns extern dns;
}

/// DNS resolution suite.
///
/// This is a singleton class; public usage is to make calls on the singleton
/// object like `ircd::net::dns()` etc.
///
struct ircd::net::dns
{
	struct opts;
	struct resolver;

	struct resolver static *resolver;
	struct opts static const opts_default;

  public:
	using callback = std::function<void (std::exception_ptr, vector_view<const rfc1035::record *>)>;
	using callback_A_one = std::function<void (std::exception_ptr, const rfc1035::record::A &)>;
	using callback_SRV_one = std::function<void (std::exception_ptr, const rfc1035::record::SRV &)>;
	using callback_ipport_one = std::function<void (std::exception_ptr, const ipport &)>;

	// Callback-based interface
	void operator()(const hostport &, const opts &, callback);
	void operator()(const hostport &, const opts &, callback_A_one);
	void operator()(const hostport &, const opts &, callback_SRV_one);
	void operator()(const hostport &, const opts &, callback_ipport_one);

	// Callback-based interface (default options)
	template<class Callback> void operator()(const hostport &, Callback&&);
};

/// DNS resolution options
struct ircd::net::dns::opts
{
	/// Overrides the SRV query to make for this resolution. If empty an
	/// SRV query may still be made from other deductions. This string is
	/// copied at the start of the resolution. It must be a fully qualified
	/// SRV query string: Example: "_matrix._tcp."
	string_view srv;

	/// Specifies the SRV protocol part when deducing the SRV query to be
	/// made. This is used when the net::hostport.service string is just
	/// the name of the service like "matrix" so we then use this value
	/// to build the protocol part of the SRV query string. It is ignored
	/// if `srv` is set or if no service is specified (thus no SRV query is
	/// made in the first place).
	string_view proto{"tcp"};
};

template<class Callback>
void
ircd::net::dns::operator()(const hostport &hostport,
                           Callback&& callback)
{
	operator()(hostport, opts_default, std::forward<Callback>(callback));
}