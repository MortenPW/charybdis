/*
 * charybdis: 21st Century IRCd
 * err.h: Throwable protocol exceptions.
 *
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
 *
 */

#pragma once
#define HAVE_IRCD_ERR_H

#ifdef __cplusplus
namespace ircd {
namespace err  {

struct err
:exception
{
	err(const char *const fmt, ...) noexcept AFP(2, 3);
};

#define IRCD_ERR(num, name)          \
struct name                          \
:err                                 \
{                                    \
    template<class... args>          \
    name(args&&... a)                \
    :err                             \
    {                                \
        NUMERIC_STR_##num,           \
        std::forward<args>(a)...     \
    }{}                              \
};

IRCD_ERR(401, NOSUCHNICK)
IRCD_ERR(402, NOSUCHSERVER)
IRCD_ERR(403, NOSUCHCHANNEL)
IRCD_ERR(404, CANNOTSENDTOCHAN)
IRCD_ERR(405, TOOMANYCHANNELS)
IRCD_ERR(406, WASNOSUCHNICK)
IRCD_ERR(407, TOOMANYTARGETS)
IRCD_ERR(409, NOORIGIN)
IRCD_ERR(410, INVALIDCAPCMD)
IRCD_ERR(411, NORECIPIENT)
IRCD_ERR(412, NOTEXTTOSEND)
IRCD_ERR(413, NOTOPLEVEL)
IRCD_ERR(414, WILDTOPLEVEL)
IRCD_ERR(416, TOOMANYMATCHES)
IRCD_ERR(421, UNKNOWNCOMMAND)
IRCD_ERR(422, NOMOTD)
IRCD_ERR(431, NONICKNAMEGIVEN)
IRCD_ERR(432, ERRONEUSNICKNAME)
IRCD_ERR(433, NICKNAMEINUSE)
IRCD_ERR(435, BANNICKCHANGE)       // bahamut's ERR_BANONCHAN -- jilles
IRCD_ERR(436, NICKCOLLISION)
IRCD_ERR(437, UNAVAILRESOURCE)
IRCD_ERR(438, NICKTOOFAST)         // We did it first Undernet! ;) db
IRCD_ERR(440, SERVICESDOWN)
IRCD_ERR(441, USERNOTINCHANNEL)
IRCD_ERR(442, NOTONCHANNEL)
IRCD_ERR(443, USERONCHANNEL)
IRCD_ERR(451, NOTREGISTERED)
IRCD_ERR(456, ACCEPTFULL)
IRCD_ERR(457, ACCEPTEXIST)
IRCD_ERR(458, ACCEPTNOT)
IRCD_ERR(461, NEEDMOREPARAMS)
IRCD_ERR(462, ALREADYREGISTRED)
IRCD_ERR(464, PASSWDMISMATCH)
IRCD_ERR(465, YOUREBANNEDCREEP)
IRCD_ERR(470, LINKCHANNEL)
IRCD_ERR(471, CHANNELISFULL)
IRCD_ERR(472, UNKNOWNMODE)
IRCD_ERR(473, INVITEONLYCHAN)
IRCD_ERR(474, BANNEDFROMCHAN)
IRCD_ERR(475, BADCHANNELKEY)
IRCD_ERR(477, NEEDREGGEDNICK)
IRCD_ERR(478, BANLISTFULL)             // I stole the numeric from ircu -db
IRCD_ERR(479, BADCHANNAME)
IRCD_ERR(480, THROTTLE)
IRCD_ERR(481, NOPRIVILEGES)
IRCD_ERR(482, CHANOPRIVSNEEDED)
IRCD_ERR(483, CANTKILLSERVER)
IRCD_ERR(484, ISCHANSERVICE)
IRCD_ERR(486, NONONREG)                // bahamut; aka ERR_ACCOUNTONLY asuka -- jilles
IRCD_ERR(489, VOICENEEDED)
IRCD_ERR(491, NOOPERHOST)
IRCD_ERR(492, CANNOTSENDTOUSER)
IRCD_ERR(494, OWNMODE)                 // from bahamut -- jilles
IRCD_ERR(501, UMODEUNKNOWNFLAG)
IRCD_ERR(502, USERSDONTMATCH)
IRCD_ERR(504, USERNOTONSERV)
IRCD_ERR(513, WRONGPONG)
IRCD_ERR(517, DISABLED)                // from ircu
IRCD_ERR(524, HELPNOTFOUND)
IRCD_ERR(691, STARTTLS)                // ircv3.atheme.org tls-3.2
IRCD_ERR(707, TARGCHANGE)
IRCD_ERR(712, TOOMANYKNOCK)
IRCD_ERR(713, CHANOPEN)
IRCD_ERR(714, KNOCKONCHAN)
IRCD_ERR(715, KNOCKDISABLED)
IRCD_ERR(716, TARGUMODEG)
IRCD_ERR(723, NOPRIVS)
IRCD_ERR(734, MONLISTFULL)
IRCD_ERR(742, MLOCKRESTRICTED)
IRCD_ERR(743, INVALIDBAN)
IRCD_ERR(902, NICKLOCKED)
IRCD_ERR(904, SASLFAIL)
IRCD_ERR(905, SASLTOOLONG)
IRCD_ERR(906, SASLABORTED)
IRCD_ERR(907, SASLALREADY)

inline
err::err(const char *const fmt, ...)
noexcept
{
	va_list ap;
	va_start(ap, fmt);
	generate(fmt, ap);
	va_end(ap);
}

}       // namespace err
}       // namespace ircd
#endif  // __cplusplus