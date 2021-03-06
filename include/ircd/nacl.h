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
#define HAVE_IRCD_NACL_H

namespace ircd::nacl
{
	IRCD_EXCEPTION(ircd::error, error)

	struct init;

	string_view version();
}

struct ircd::nacl::init
{
	init();
	~init() noexcept;
};
