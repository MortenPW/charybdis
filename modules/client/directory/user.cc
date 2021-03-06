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
	"Client 8.1 :User Directory"
};

resource
search_resource
{
	"/_matrix/client/r0/user_directory/search",
	{
		"(8.1) User directory search",
	}
};

resource::response
post__search(client &client,
             const resource::request &request)
{
	const auto &search_term
	{
		unquote(request.at("search_term"))
	};

	const ushort &limit
	{
		request.get<ushort>("limit", 10)
	};

	m::room::state users
	{
		m::user::users
	};

	// Search term in this endpoint comes in as-is from Riot. Our query
	// is a lower_bound of a user_id, so we have to prefix the '@'.
	char qbuf[256] {'@'};
	const string_view &query
	{
		!startswith(search_term, '@')?
			string_view{qbuf, strlcpy(qbuf+1, search_term, sizeof(qbuf))}:
			search_term
	};

	bool limited{true};
	std::vector<json::value> results;
	users.test("ircd.user", query, [&results, &limit, &limited]
	(const m::event &event)
	{
		results.emplace_back(json::members
		{
			{ "user_id", at<"state_key"_>(event) },
		});

		limited = results.size() >= limit;
		return limited; // return true to break
	});

	return resource::response
	{
		client, json::members
		{
			{ "limited", limited },
			{ "results", { results.data(), results.size() } },
		}
	};
}

resource::method
search_post
{
	search_resource, "POST", post__search,
	{
		search_post.REQUIRES_AUTH |
		search_post.RATE_LIMITED
	}
};
