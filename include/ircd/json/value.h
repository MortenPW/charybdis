/*
 * Copyright (C) 2017 Charybdis Development Team
 * Copyright (C) 2017 Jason Volk <jason@zemos.net>
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
#define HAVE_IRCD_JSON_VALUE_H

// The ircd::json::value is used if we have to keep state in machine-form
// rather than directly computing JSON strings. This class ends up being useful
// for recursive initializer_lists to compose JSON from machine values. It
// is lightweight, consuming the space of two pointers which is the same size
// as a string_view.
//
struct ircd::json::value
{
	union // xxx std::variant
	{
		int64_t integer;
		double floating;
		const char *string;
		const struct index *object;
		const struct array *array;
	};

	uint64_t len     : 58;
	enum type type   : 3;
	uint64_t serial  : 1;
	uint64_t alloc   : 1;
	uint64_t floats  : 1;

  public:
	bool empty() const;
	size_t size() const;
	operator string_view() const;
	explicit operator std::string() const;

	template<class T> explicit value(const T &specialized);
	template<size_t N> value(const char (&)[N]);
	value(const string_view &sv, const enum type &);
	value(const string_view &sv);
	value(const char *const &s);
	value(const index *const &);    // alloc = false
	value(std::unique_ptr<index>);  // alloc = true
	value();
	value(value &&) noexcept;
	value(const value &) = delete;
	value &operator=(value &&) noexcept;
	value &operator=(const value &) = delete;
	~value() noexcept;

	friend enum type type(const value &a);
	friend bool operator==(const value &a, const value &b);
	friend bool operator!=(const value &a, const value &b);
	friend bool operator<=(const value &a, const value &b);
	friend bool operator>=(const value &a, const value &b);
	friend bool operator<(const value &a, const value &b);
	friend bool operator>(const value &a, const value &b);
	friend std::ostream &operator<<(std::ostream &, const value &);
};

namespace ircd::json
{
	template<> value::value(const double &floating);
	template<> value::value(const int64_t &integer);
	template<> value::value(const float &floating);
	template<> value::value(const int32_t &integer);
	template<> value::value(const int16_t &integer);
	template<> value::value(const bool &boolean);
	template<> value::value(const std::string &str);
}

static_assert(sizeof(ircd::json::value) == 16, "");

inline
ircd::json::value::value()
:string{""}
,len{0}
,type{STRING}
,serial{true}
,alloc{false}
,floats{false}
{}

inline
ircd::json::value::value(const string_view &sv,
                         const enum type &type)
:string{sv.data()}
,len{sv.size()}
,type{type}
,serial{true}
,alloc{false}
,floats{false}
{}

inline
ircd::json::value::value(const index *const &object)
:object{object}
,len{0}
,type{OBJECT}
,serial{false}
,alloc{false}
,floats{false}
{}

inline
ircd::json::value::value(std::unique_ptr<index> object)
:object{object.get()}
,len{0}
,type{OBJECT}
,serial{false}
,alloc{true}
,floats{false}
{
	object.release();
}

template<> inline
ircd::json::value::value(const int64_t &integer)
:integer{integer}
,len{0}
,type{NUMBER}
,serial{false}
,alloc{false}
,floats{false}
{}

template<> inline
ircd::json::value::value(const double &floating)
:floating{floating}
,len{0}
,type{NUMBER}
,serial{false}
,alloc{false}
,floats{true}
{}

template<> inline
ircd::json::value::value(const float &floating)
:value{double(floating)}
{}

template<> inline
ircd::json::value::value(const int32_t &integer)
:value{int64_t(integer)}
{}

template<> inline
ircd::json::value::value(const int16_t &integer)
:value{int64_t(integer)}
{}

template<> inline
ircd::json::value::value(const bool &boolean)
:value
{
	boolean? "true" : "false",
	type::LITERAL
}{}

template<> inline
ircd::json::value::value(const std::string &str)
:value{string_view{str}}
{}

template<size_t N>
ircd::json::value::value(const char (&str)[N])
:value{string_view{str}}
{}

inline
ircd::json::value::value(const char *const &s)
:value{string_view{s}}
{}

inline
ircd::json::value::value(const string_view &sv)
:value{sv, json::type(sv, std::nothrow)}
{}

inline
ircd::json::value::value(value &&other)
noexcept
:integer{other.integer}
,len{other.len}
,type{other.type}
,serial{other.serial}
,alloc{other.alloc}
,floats{other.floats}
{
	other.alloc = false;
}

inline ircd::json::value &
ircd::json::value::operator=(value &&other)
noexcept
{
	this->~value();
	integer = other.integer;
	len = other.len;
	type = other.type;
	serial = other.serial;
	alloc = other.alloc;
	other.alloc = false;
	floats = other.floats;
	return *this;
}

inline enum ircd::json::type
ircd::json::type(const value &a)
{
	return a.type;
}