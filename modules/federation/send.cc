// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

using namespace ircd;

mapi::header
IRCD_MODULE
{
	"federation send"
};

resource
send_resource
{
	"/_matrix/federation/v1/send/",
	{
		"federation send",
		resource::DIRECTORY,
	}
};

void
handle_edu(client &client,
           const resource::request::object<m::txn> &request,
           const string_view &txn_id,
           const m::edu &edu)
{
	m::event event;
	json::get<"origin"_>(event) = at<"origin"_>(request);
	json::get<"origin_server_ts"_>(event) = at<"origin_server_ts"_>(request);
	json::get<"content"_>(event) = at<"content"_>(edu);
	json::get<"type"_>(event) = at<"edu_type"_>(edu);
	json::get<"depth"_>(event) = std::numeric_limits<int64_t>::max(); // custom undefined

	m::vm::opts vmopts;
	vmopts.non_conform.set(m::event::conforms::INVALID_OR_MISSING_EVENT_ID);
	vmopts.non_conform.set(m::event::conforms::INVALID_OR_MISSING_ROOM_ID);
	vmopts.non_conform.set(m::event::conforms::INVALID_OR_MISSING_SENDER_ID);
	vmopts.non_conform.set(m::event::conforms::MISSING_ORIGIN_SIGNATURE);
	vmopts.non_conform.set(m::event::conforms::MISSING_SIGNATURES);
	vmopts.non_conform.set(m::event::conforms::MISSING_PREV_EVENTS);
	vmopts.non_conform.set(m::event::conforms::MISSING_PREV_STATE);
	vmopts.non_conform.set(m::event::conforms::DEPTH_ZERO);
	m::vm::eval eval
	{
		event, vmopts
	};
}

void
handle_pdu(client &client,
           const resource::request::object<m::txn> &request,
           const string_view &txn_id,
           const m::event &event)
{
	m::vm::opts vmopts;
	vmopts.non_conform.set(m::event::conforms::MISSING_PREV_STATE);
	vmopts.non_conform.set(m::event::conforms::MISSING_MEMBERSHIP);
	vmopts.verify = true;
	vmopts.prev_check_exists = false;
	vmopts.nothrows = -1U;
	vmopts.infolog_accept = true;
	vmopts.warnlog |= m::vm::fault::STATE;
	vmopts.errorlog &= ~m::vm::fault::STATE;
	m::vm::eval eval
	{
		event, vmopts
	};
}

void
handle_pdu_failure(client &client,
                   const resource::request::object<m::txn> &request,
                   const string_view &txn_id,
                   const json::object &pdu_failure)
{
	log::error
	{
		"%s :%s | (pdu_failure) %s",
		txn_id,
		at<"origin"_>(request),
		pdu_failure.get("sender", string_view{"*"}),
		string_view{pdu_failure}
	};
}

resource::response
handle_put(client &client,
           const resource::request::object<m::txn> &request)
{
	const auto txn_id
	{
		request.parv[0]
	};

	const string_view &origin
	{
		json::at<"origin"_>(request)
	};

	const json::array &edus
	{
		json::get<"edus"_>(request)
	};

	const json::array &pdus
	{
		json::get<"pdus"_>(request)
	};

	const json::array &pdu_failures
	{
		json::get<"pdu_failures"_>(request)
	};

	log::debug
	{
		"%s :%s | %s --> edus:%zu pdus:%zu errors:%zu",
		txn_id,
		origin,
		string(remote(client)),
		edus.count(),
		pdus.count(),
		pdu_failures.count()
	};

	for(const auto &pdu_failure : pdu_failures)
		handle_pdu_failure(client, request, txn_id, pdu_failure);

	for(const json::object &edu : edus)
		handle_edu(client, request, txn_id, edu);

	for(const json::object &pdu : pdus)
		handle_pdu(client, request, txn_id, pdu);

	return resource::response
	{
		client, http::OK
	};
}

resource::method
method_put
{
	send_resource, "PUT", handle_put,
	{
		method_put.VERIFY_ORIGIN,

		// Coarse timeout
		30s,

		// Payload maximum
		4_MiB // larger = HTTP 413  //TODO: conf
	}
};
