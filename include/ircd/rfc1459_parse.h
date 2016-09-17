/**
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
#define HAVE_IRCD_RFC1459_PARSE_H

#ifdef __cplusplus
#include <boost/spirit/include/qi.hpp>

BOOST_FUSION_ADAPT_STRUCT
(
	ircd::rfc1459::pfx,
	( ircd::rfc1459::nick,  nick )
	( ircd::rfc1459::user,  user )
	( ircd::rfc1459::host,  host )
)

BOOST_FUSION_ADAPT_STRUCT
(
	ircd::rfc1459::line,
	( ircd::rfc1459::pfx,   pfx  )
	( ircd::rfc1459::cmd,   cmd  )
	( ircd::rfc1459::parv,  parv )
)

namespace ircd    {
namespace rfc1459 {
namespace parse   {

namespace qi = boost::spirit::qi;

using qi::lit;
using qi::char_;
using qi::repeat;
using qi::attr;
using qi::eps;

/* The grammar template class.
 * This aggregates all the rules under one template to make composing them easier.
 *
 * The grammar template is instantiated by individual parsers depending on the goal
 * for the specific parse, or the "top level." The first top-level was an IRC line, so
 * a class was created `struct head` specifying grammar::line as the top rule, and
 * rfc1459::line as the top output target to parse into.
 */
template<class it,
         class top>
struct grammar
:qi::grammar<it, top>
{
	qi::rule<it> space;
	qi::rule<it> colon;
	qi::rule<it> nulcrlf;
	qi::rule<it> spnulcrlf;
	qi::rule<it> terminator;

	qi::rule<it, std::string()> hostname;
	qi::rule<it, std::string()> user;
	qi::rule<it, std::string()> server;
	qi::rule<it, std::string()> nick;
	qi::rule<it, rfc1459::pfx> prefix;

	qi::rule<it, std::string()> trailing;
	qi::rule<it, std::string()> middle;
	qi::rule<it, rfc1459::parv> params;

	qi::rule<it, std::string()> numeric;
	qi::rule<it, rfc1459::cmd> command;

	qi::rule<it, rfc1459::line> line;
	qi::rule<it, rfc1459::tape> tape;

	grammar(qi::rule<it, top> &top_rule);
};

template<class it,
         class top>
grammar<it, top>::grammar(qi::rule<it, top> &top_rule)
:grammar<it, top>::base_type
{
	top_rule
}
,space // A single space character
{
	/* TODO: RFC says:
	 *   1) <SPACE> is consists only of SPACE character(s) (0x20).
	 *      Specially notice that TABULATION, and all other control
	 *      characters are considered NON-WHITE-SPACE.
	 * But our table in this namespace has control characters labeled as SPACE.
	 * This needs to be fixed.
	 */

	//char_(gather(character::SPACE))
	lit(' ')
	,"space"
}
,colon // A single colon character
{
	lit(':')
	,"colon"
}
,nulcrlf // Match on NUL or CR or LF
{
	lit('\0') | lit('\r') | lit('\n')
	,"nulcrlf"
}
,spnulcrlf // Match on space or nulcrlf
{
	space | nulcrlf
	,"spnulcrlf"
}
,terminator // The message terminator
{
	lit('\r') >> lit('\n')
	,"terminator"
}
,hostname // A valid hostname
{
	+char_(gather(character::HOST)) // TODO: https://tools.ietf.org/html/rfc952
	,"hostname"
}
,user // A valid username
{
	+char_(gather(character::USER))
	,"user"
}
,server // A valid servername
{
	hostname
	,"server"
}
,nick // A valid nickname, leading letter followed by any NICK chars
{
	char_(gather(character::ALPHA)) >> *char_(gather(character::NICK))
	,"nick"
}
,prefix // A valid prefix, required name, optional user and host (or empty placeholders)
{
	colon >> (nick | server) >> -(lit('!') >> user) >> -(lit('@') >> hostname)
	,"prefix"
}
,trailing // Trailing string pinch
{
	colon >> +(char_ - nulcrlf)
	,"trailing"
}
,middle // Spaced parameters
{
	!colon >> +(char_ - spnulcrlf)
	,"middle"
}
,params // Parameter vector
{
	*(+space >> middle) >> -(+space >> trailing)
	,"params"
}
,numeric // \d\d\d numeric
{
	repeat(3)[char_(gather(character::DIGIT))]
	,"numeric"
}
,command // A command or numeric
{
	+char_(gather(character::ALPHA)) | numeric
	,"command"
}
,line
{
	-(prefix >> +space) >> command >> params
	,"line"
}
,tape
{
	+(-line >> +terminator)
	,"tape"
}
{
}

}      // namespace parse
}      // namespace rfc1459
}      // namespace ircd
#endif // __cplusplus