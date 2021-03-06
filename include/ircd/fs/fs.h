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
#define HAVE_IRCD_FS_H

/*
 * Directory paths and filenames for UNIX systems.
 * IRCD_PREFIX is set using ./configure --prefix, see INSTALL.
 * Do not change these without corresponding changes in the build system.
 *
 * IRCD_PREFIX = prefix for all directories,
 * DPATH       = root directory of installation,
 * BINPATH     = directory for binary files,
 * ETCPATH     = directory for configuration files,
 * LOGPATH     = directory for logfiles,
 * MODPATH     = directory for modules,
 * AUTOMODPATH = directory for autoloaded modules
 */

/// Tools for working with the local filesystem.
///
/// IRCd has wrapped operations for the local filesystem to maintain a
/// cross-platform implementation of asynchronous file IO in conjunction with
/// the ircd::ctx userspace context system. These operations will yield your
/// ircd::ctx when necessary to not block the event loop on the main thread
/// during IOs.
namespace ircd::fs
{
	struct aio;
	struct init;

	IRCD_EXCEPTION(ircd::error, error)
	IRCD_EXCEPTION(error, filesystem_error)

	constexpr auto DPATH = IRCD_PREFIX;
	constexpr auto BINPATH = IRCD_PREFIX "/bin";
	constexpr auto ETCPATH = RB_ETC_DIR;
	constexpr auto LOGPATH = RB_LOG_DIR;
	constexpr auto MODPATH = RB_MODULE_DIR;
	constexpr auto CPATH = RB_ETC_DIR "/ircd.conf";              // ircd.conf file
	constexpr auto SPATH = RB_BIN_DIR "/" BRANDING_NAME;         // ircd executable
	constexpr auto DBPATH = PKGLOCALSTATEDIR "/db";              // database prefix

	// Below are the elements for default paths.
	enum index
	{
		PREFIX,
		BIN,
		ETC,
		LOG,
		LIBEXEC,
		MODULES,
		IRCD_CONF,
		IRCD_EXEC,
		DB,

		_NUM_
	};

	const char *get(index) noexcept;
	const char *name(index) noexcept;

	std::string make_path(const std::initializer_list<string_view> &);

	bool exists(const string_view &path);
	bool is_dir(const string_view &path);
	bool is_reg(const string_view &path);
	size_t size(const string_view &path);

	std::vector<std::string> ls(const string_view &path);
	std::vector<std::string> ls_recursive(const string_view &path);

	bool rename(std::nothrow_t, const string_view &old, const string_view &new_);
	void rename(const string_view &old, const string_view &new_);

	bool remove(std::nothrow_t, const string_view &path);
	bool remove(const string_view &path);

	std::string cwd();
	void chdir(const string_view &path);
	bool mkdir(const string_view &path);

	extern aio *aioctx;
}

#include "read.h"
#include "write.h"
#include "stdin.h"

struct ircd::fs::init
{
	init();
	~init() noexcept;
};
