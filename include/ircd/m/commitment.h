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
#define HAVE_IRCD_M_COMMIT_H

namespace ircd::m
{
	struct commitment;
}

/// A commitment is an object to compose a single transaction from multiple
/// operations. The commit is usually placed on the stack. It will transact
/// all operations during its lifetime for a specific ircd::ctx. There are
/// a few justifying cases for use:
/// - Atomicity to supplement mutual exclusion.
/// - Rollback on intermediate failures.
///
/// Consider the following operations:
/// 1. create(user);
/// 2. set_password(user, pass);
///
/// If set_password() fails for some reason the user will not be able to login
/// because password wasn't set; but because the user was first created and now
/// appears in the user's index, attempts to re-register user will be denied.
/// The transactional properties of the commit address this:
///
/// try {
///   m::commitment commit;
///   create(user);
///   set_password(user, pass); // throws here
/// } catch(...) {
///   // create(user) was rolled back already
/// }
///
/// To effectively transact all operations for the entire callstack, including
/// effects of operations which contribute more operations, without burdening
/// the developer or polluting the entire call tree with commit arguments:
/// commits are context-oriented. Multiple commit instances may exist but they
/// have stackful behavior: thus there cannot be concurrent commitments from
/// the same ircd::ctx.
///
struct ircd::m::commitment
{

};
