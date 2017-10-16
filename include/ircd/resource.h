/*
 * Copyright (C) 2016 Charybdis Development Team
 * Copyright (C) 2016 Jason Volk <jason@zemos.net>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once
#define HAVE_IRCD_RESOURCE_H

namespace ircd
{
	struct resource;
}

struct ircd::resource
{
	IRCD_EXCEPTION(ircd::error, error)

	struct method;
	struct request;
	struct response;

	enum flag
	{
		DIRECTORY  = 0x01,
	};

	struct opts
	{
		flag flags{flag(0)};
		string_view description;
	};

	static std::map<string_view, resource *, iless> resources;

	string_view path;
	string_view description;
	flag flags;
	std::map<string_view, method *> methods;
	unique_const_iterator<decltype(resources)> resources_it;

  private:
	virtual void handle_request(client &, method &, resource::request &);

  public:
	method &operator[](const string_view &path);
	void operator()(client &, parse::capstan &, const http::request::head &);

	resource(const string_view &path, const string_view &description, opts = {{}});
	resource(const string_view &path, opts = {{}});
	resource() = default;
	virtual ~resource() noexcept;

	static resource &find(string_view path);
};

struct ircd::resource::request
:json::object
{
	template<class> struct object;

	const http::request::head &head;
	http::request::content &content;
	http::query::string query;
	m::user::id::buf user_id;
	vector_view<string_view> parv;

	request(const http::request::head &head, http::request::content &content, http::query::string query, const vector_view<string_view> &parv);
};

template<class tuple>
struct ircd::resource::request::object
:tuple
{
	resource::request &r;
	const http::request::head &head;
	const http::request::content &content;
	const http::query::string &query;
	const vector_view<string_view> &parv;
	const json::object &body;

	object(resource::request &r)
	:tuple{r}
	,r{r}
	,head{r.head}
	,content{r.content}
	,query{r.query}
	,parv{r.parv}
	,body{r}
	{}
};

struct ircd::resource::response
{
	response(client &, const string_view &str, const string_view &ct = "text/plain; charset=utf8", const http::code & = http::OK);
	response(client &, const json::object &str, const http::code & = http::OK);
	response(client &, const json::array &str, const http::code & = http::OK);
	response(client &, const json::members & = {}, const http::code & = http::OK);
	response(client &, const json::value &, const http::code & = http::OK);
	response(client &, const json::iov &, const http::code & = http::OK);
	response(client &, const http::code &, const json::members &);
	response(client &, const http::code &, const json::value &);
	response(client &, const http::code &, const json::iov &);
	response(client &, const http::code &);
	response() = default;
};

struct ircd::resource::method
{
	enum flag
	{
		REQUIRES_AUTH  = 0x01,
		RATE_LIMITED   = 0x02,
		VERIFY_ORIGIN  = 0x04,
	};

	struct opts
	{
		flag flags{flag(0)};
	};

	using handler = std::function<response (client &, request &)>;

	string_view name;
	struct resource *resource;
	handler function;
	flag flags;
	unique_const_iterator<decltype(resource::methods)> methods_it;

  public:
	virtual response operator()(client &, request &);

	method(struct resource &, const string_view &name, const handler &, opts = {{}});
	virtual ~method() noexcept;
};
