/*
 *  ircd-ratbox: A slightly useful ircd.
 *  stdinc.h: Pull in all of the necessary system headers
 *
 *  Copyright (C) 2002 Aaron Sethman <androsyn@ratbox.org>
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
 *
 */

///////////////////////////////////////////////////////////////////////////////
//
// IRCd's namespacing is as follows:
//
// IRCD_     #define and macro namespace
// RB_       #define and macro namespace (legacy, low-level)
// ircd_     C namespace and demangled bindings
// ircd::    C++ namespace
//

///////////////////////////////////////////////////////////////////////////////
//
// Standard includes
//
// This header includes almost everything we use out of the standard library.
// This is a pre-compiled header. Project build time is significantly reduced
// by doing things this way and C++ std headers have very little namespace
// pollution and risk of conflicts.
//
// * If any project header file requires standard library symbols we try to
//   list it here, not in our header files.
//
// * Rare one-off's #includes isolated to a specific .cc file may not always be
//   listed here but can be.
//
// * Third party / dependency / non-std includes, are NEVER listed here.
//   Instead we include those in .cc files, and use forward declarations if
//   we require a symbol in our API to them.
//

#define HAVE_IRCD_STDINC_H

// Generated by ./configure
#include "config.h"

extern "C" {

#include <RB_INC_ASSERT_H
#include <RB_INC_STDARG_H
#include <RB_INC_SYS_TIME_H
#include <RB_INC_SYS_RESOURCE_H

} // extern "C"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <RB_INC_WINDOWS_H
#include <RB_INC_WINSOCK2_H
#include <RB_INC_WS2TCPIP_H
#include <RB_INC_IPHLPAPI_H
#endif

#include <RB_INC_CSTDDEF
#include <RB_INC_CSTDINT
#include <RB_INC_LIMITS
#include <RB_INC_TYPE_TRAITS
#include <RB_INC_TYPEINDEX
#include <RB_INC_VARIANT
#include <RB_INC_CERRNO
#include <RB_INC_UTILITY
#include <RB_INC_FUNCTIONAL
#include <RB_INC_ALGORITHM
#include <RB_INC_NUMERIC
#include <RB_INC_CMATH
#include <RB_INC_MEMORY
#include <RB_INC_EXCEPTION
#include <RB_INC_SYSTEM_ERROR
#include <RB_INC_ARRAY
#include <RB_INC_VECTOR
#include <RB_INC_STACK
#include <RB_INC_STRING
#include <RB_INC_CSTRING
#include <RB_INC_STRING_VIEW
#include <RB_INC_EXPERIMENTAL_STRING_VIEW
#include <RB_INC_LOCALE
#include <RB_INC_CODECVT
#include <RB_INC_MAP
#include <RB_INC_SET
#include <RB_INC_LIST
#include <RB_INC_FORWARD_LIST
#include <RB_INC_UNORDERED_MAP
#include <RB_INC_DEQUE
#include <RB_INC_QUEUE
#include <RB_INC_SSTREAM
#include <RB_INC_FSTREAM
#include <RB_INC_IOSTREAM
#include <RB_INC_IOMANIP
#include <RB_INC_CSTDIO
#include <RB_INC_CHRONO
#include <RB_INC_CTIME
#include <RB_INC_ATOMIC
#include <RB_INC_THREAD
#include <RB_INC_MUTEX
#include <RB_INC_SHARED_MUTEX
#include <RB_INC_CONDITION_VARIABLE
#include <RB_INC_RANDOM

///////////////////////////////////////////////////////////////////////////////
//
// Pollution
//
// This section lists all of the items introduced outside of our namespace
// which may conflict with your project.
//

// Common branch prediction macros
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

// Legacy attribute format printf macros
#define AFP(a, b)       __attribute__((format(printf, a, b)))
#define AFGP(a, b)      __attribute__((format(gnu_printf, a, b)))

// Experimental std::string_view
namespace std
{
	using experimental::string_view;
}

///////////////////////////////////////////////////////////////////////////////
//
// Forward declarations from third party namespaces not included here
//

/// Forward declarations for boost because it is not included here.
///
/// libircd does not include third party headers along with its own headers
/// for the public interface, only standard library headers. boost is only
/// included in specific definition files where we use its functionality.
/// This is a major improvement in project compile time.
namespace boost
{
}

/// Forward declarations for boost::asio because it is not included here.
///
/// Boost headers are not exposed to our users unless explicitly included by a
/// definition file. Other libircd headers may extend this namespace with more
/// forward declarations.
namespace boost::asio
{
	struct io_service;       // Allow a reference to an ios to be passed to ircd
}

///////////////////////////////////////////////////////////////////////////////
//
// Some items imported into our namespace.
//

namespace ircd
{
	using std::nullptr_t;
	using std::begin;
	using std::end;
	using std::get;
	using std::static_pointer_cast;
	using std::dynamic_pointer_cast;
	using std::const_pointer_cast;
	using ostream = std::ostream;
	namespace ph = std::placeholders;
	namespace asio = boost::asio;
	using namespace std::string_literals;
	using namespace std::literals::chrono_literals;
	template<class... T> using ilist = std::initializer_list<T...>;
}

///////////////////////////////////////////////////////////////////////////////
//
// libircd API
//

// Unsorted section
namespace ircd
{
	extern boost::asio::io_service *ios;
	constexpr size_t BUFSIZE { 512 };

	struct socket;
	struct client;

	std::string demangle(const std::string &symbol);
	template<class T> std::string demangle();
}

#include "util.h"
#include "exception.h"
#include "string_view.h"
#include "date.h"
#include "timer.h"
#include "allocator.h"
#include "buffer.h"
#include "rand.h"
#include "hash.h"
#include "info.h"
#include "localee.h"
#include "life_guard.h"
#include "color.h"
#include "lexical.h"
#include "params.h"
#include "iov.h"
#include "parse.h"
#include "rfc1459.h"
#include "json.h"
#include "http.h"
#include "fmt.h"
#include "fs.h"
#include "ctx.h"
#include "logger.h"
#include "db.h"
#include "js.h"
#include "client.h"
#include "mods.h"
#include "listen.h"
#include "m.h"
#include "resource.h"

template<class T>
std::string
ircd::demangle()
{
	return demangle(typeid(T).name());
}
