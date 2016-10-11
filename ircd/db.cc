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
 *
 */

#include <rocksdb/db.h>
#include <rocksdb/cache.h>

namespace ircd {
namespace db   {

struct log::log log
{
	"db", 'D'            // Database subsystem takes SNOMASK +D
};

void throw_on_error(const rocksdb::Status &);
bool valid(const rocksdb::Iterator &);
void valid_or_throw(const rocksdb::Iterator &);
void valid_equal_or_throw(const rocksdb::Iterator &, const rocksdb::Slice &);

const auto BLOCKING = rocksdb::ReadTier::kReadAllTier;
const auto NON_BLOCKING = rocksdb::ReadTier::kBlockCacheTier;
enum class pos
{
	FRONT   = -2,    // .front()    | first element
	PREV    = -1,    // std::prev() | previous element
	END     = 0,     // break;      | exit iteration (or past the end)
	NEXT    = 1,     // continue;   | next element
	BACK    = 2,     // .back()     | last element
};
void seek(rocksdb::Iterator &, const pos &);
void seek(rocksdb::Iterator &, const rocksdb::Slice &);
template<class pos> void seek(rocksdb::DB &, const pos &, rocksdb::ReadOptions &, std::unique_ptr<rocksdb::Iterator> &);
std::unique_ptr<rocksdb::Iterator> seek(rocksdb::DB &, const rocksdb::Slice &, rocksdb::ReadOptions &);

// This is important to prevent thrashing the iterators which have to reset on iops
const auto DEFAULT_READAHEAD = 4_MiB;

rocksdb::WriteOptions make_opts(const sopts &);
rocksdb::ReadOptions make_opts(const gopts &, const bool &iterator = false);
rocksdb::Options make_opts(const opts &);

struct meta
{
	std::string name;
	std::string path;
	rocksdb::Options opts;
	std::shared_ptr<rocksdb::Cache> cache;
};

} // namespace db
} // namespace ircd

using namespace ircd;

db::init::init()
{
}

db::init::~init()
noexcept
{
}

db::handle::handle(const std::string &name,
                   const opts &opts)
try
:meta{[&name, &opts]
{
	auto meta(std::make_unique<struct meta>());
	meta->name = name;
	meta->path = path(name);
	meta->opts = make_opts(opts);

	const auto lru_cache_size(opt_val(opts, opt::LRU_CACHE));
	if(lru_cache_size > 0)
		meta->cache = rocksdb::NewLRUCache(lru_cache_size);

	meta->opts.row_cache = meta->cache;
	return std::move(meta);
}()}
,d{[this]
{
	rocksdb::DB *ptr;
	throw_on_error(rocksdb::DB::Open(meta->opts, meta->path, &ptr));
	return std::unique_ptr<rocksdb::DB>{ptr};
}()}
{
	log.info("Opened database \"%s\" @ `%s' (handle: %p)",
	         meta->name.c_str(),
	         meta->path.c_str(),
	         (const void *)this);
}
catch(const invalid_argument &e)
{
	const bool no_create(has_opt(opts, opt::NO_CREATE));
	const bool no_existing(has_opt(opts, opt::NO_EXISTING));
	const char *const helpstr
	{
		no_create?   " (The database is missing and will not be created)":
		no_existing? " (The database already exists but must be fresh)":
		             ""
	};

	throw error("Failed to open db '%s': %s%s",
	            name.c_str(),
	            e.what(),
	            helpstr);
}
catch(const std::exception &e)
{
	throw error("Failed to open db '%s': %s",
	            name.c_str(),
	            e.what());
}

db::handle::~handle()
noexcept
{
}

void
db::handle::del(const std::string &key,
                const sopts &sopts)
{
	using rocksdb::Slice;

	auto opts(make_opts(sopts));
	const Slice k(key.data(), key.size());
	throw_on_error(d->Delete(opts, k));
}

void
db::handle::set(const std::string &key,
                const std::string &value,
                const sopts &sopts)
{
	set(key, value.data(), value.size(), sopts);
}

void
db::handle::set(const std::string &key,
                const uint8_t *const &buf,
                const size_t &size,
                const sopts &sopts)
{
	set(key, reinterpret_cast<const char *>(buf), size, sopts);
}

void
db::handle::set(const std::string &key,
                const char *const &buf,
                const size_t &size,
                const sopts &sopts)
{
	using rocksdb::Slice;

	auto opts(make_opts(sopts));
	const Slice k(key.data(), key.size());
	const Slice v(buf, size);
	throw_on_error(d->Put(opts, k, v));
}

std::string
db::handle::get(const std::string &key,
                const gopts &gopts)
{
	std::string ret;
	const auto copy([&ret]
	(const char *const &src, const size_t &size)
	{
		ret.assign(src, size);
	});

	get(key, copy, gopts);
	return ret;
}

size_t
db::handle::get(const std::string &key,
                uint8_t *const &buf,
                const size_t &max,
                const gopts &gopts)
{
	size_t ret(0);
	const auto copy([&ret, &buf, &max]
	(const char *const &src, const size_t &size)
	{
		ret = std::min(size, max);
		memcpy(buf, src, ret);
	});

	get(key, copy, gopts);
	return ret;
}

size_t
db::handle::get(const std::string &key,
                char *const &buf,
                const size_t &max,
                const gopts &gopts)
{
	size_t ret(0);
	const auto copy([&ret, &buf, &max]
	(const char *const &src, const size_t &size)
	{
		ret = rb_strlcpy(buf, src, std::min(size, max));
	});

	get(key, copy, gopts);
	return ret;
}

void
db::handle::get(const std::string &key,
                const closure &func,
                const gopts &gopts)
{
	using rocksdb::Slice;
	using rocksdb::Iterator;

	auto opts(make_opts(gopts));
	const Slice sk(key.data(), key.size());
	const auto it(seek(*d, sk, opts));
	valid_equal_or_throw(*it, sk);
	const auto &v(it->value());
	func(v.data(), v.size());
}

bool
db::handle::has(const std::string &key,
                const gopts &gopts)
{
	using rocksdb::Slice;
	using rocksdb::Status;

	const Slice k(key.data(), key.size());
	auto opts(make_opts(gopts));

	// Perform queries which are stymied from any sysentry
	opts.read_tier = NON_BLOCKING;

	// Perform a co-RP query to the filtration
	if(!d->KeyMayExist(opts, k, nullptr, nullptr))
		return false;

	// Perform a query to the cache
	auto status(d->Get(opts, k, nullptr));
	if(status.IsIncomplete())
	{
		// DB cache miss; next query requires I/O, offload it
		opts.read_tier = BLOCKING;
		ctx::offload([this, &opts, &k, &status]
		{
			status = d->Get(opts, k, nullptr);
		});
	}

	// Finally the result
	switch(status.code())
	{
		case Status::kOk:          return true;
		case Status::kNotFound:    return false;
		default:
			throw_on_error(status);
			__builtin_unreachable();
	}
}

namespace ircd {
namespace db   {

struct const_iterator::state
{
	struct handle *handle;
	rocksdb::ReadOptions ropts;
	std::shared_ptr<const rocksdb::Snapshot> snap;
	std::unique_ptr<rocksdb::Iterator> it;

	state(struct handle *const & = nullptr, const gopts & = {});
};

} // namespace db
} // namespace ircd

db::const_iterator
db::handle::cend(const gopts &gopts)
{
	return {};
}

db::const_iterator
db::handle::cbegin(const gopts &gopts)
{
	const_iterator ret
	{
		std::make_unique<struct const_iterator::state>(this, gopts)
	};

	auto &state(*ret.state);
	if(!has_opt(gopts, db::get::READAHEAD))
		state.ropts.readahead_size = DEFAULT_READAHEAD;

	seek(*state.handle->d, pos::FRONT, state.ropts, state.it);
	return std::move(ret);
}

db::const_iterator
db::handle::upper_bound(const std::string &key,
                        const gopts &gopts)
{
	using rocksdb::Slice;

	auto it(lower_bound(key, gopts));
	const Slice sk(key.data(), key.size());
	if(it && it.state->it->key().compare(sk) == 0)
		++it;

	return std::move(it);
}

db::const_iterator
db::handle::lower_bound(const std::string &key,
                        const gopts &gopts)
{
	using rocksdb::Slice;

	const_iterator ret
	{
		std::make_unique<struct const_iterator::state>(this, gopts)
	};

	auto &state(*ret.state);
	if(!has_opt(gopts, db::get::READAHEAD))
		state.ropts.readahead_size = DEFAULT_READAHEAD;

	const Slice sk(key.data(), key.size());
	seek(*state.handle->d, sk, state.ropts, state.it);
	return std::move(ret);
}

db::const_iterator::state::state(struct handle *const &handle,
                                 const gopts &gopts)
:handle{handle}
,ropts{make_opts(gopts, true)}
,snap
{
	[this, &handle, &gopts]() -> const rocksdb::Snapshot *
	{
		if(handle && !has_opt(gopts, get::NO_SNAPSHOT))
			ropts.snapshot = handle->d->GetSnapshot();

		return ropts.snapshot;
	}()
	,[this](const auto *const &snap)
	{
		if(this->handle && this->handle->d)
			this->handle->d->ReleaseSnapshot(snap);
	}
}
{
}

db::const_iterator::const_iterator(std::unique_ptr<struct state> &&state)
:state{std::move(state)}
{
}

db::const_iterator::const_iterator(const_iterator &&o)
noexcept
:state{std::move(o.state)}
{
}

db::const_iterator::~const_iterator()
noexcept
{
}

db::const_iterator &
db::const_iterator::operator--()
{
	seek(*state->handle->d, pos::PREV, state->ropts, state->it);
	return *this;
}

db::const_iterator &
db::const_iterator::operator++()
{
	seek(*state->handle->d, pos::NEXT, state->ropts, state->it);
	return *this;
}

const db::const_iterator::value_type &
db::const_iterator::operator*()
const
{
	const auto &k(state->it->key());
	val.first.first = k.data();
	val.first.second = k.size();

	const auto &v(state->it->value());
	val.second.first = v.data();
	val.second.second = v.size();

	return val;
}

const db::const_iterator::value_type *
db::const_iterator::operator->()
const
{
	return &operator*();
}

bool
db::const_iterator::operator>=(const const_iterator &o)
const
{
	return (*this > o) || (*this == o);
}

bool
db::const_iterator::operator<=(const const_iterator &o)
const
{
	return (*this < o) || (*this == o);
}

bool
db::const_iterator::operator!=(const const_iterator &o)
const
{
	return !(*this == o);
}

bool
db::const_iterator::operator==(const const_iterator &o)
const
{
	if(*this && o)
	{
		const auto &a(state->it->key());
		const auto &b(o.state->it->key());
		return a.compare(b) == 0;
	}

	if(!*this && !o)
		return true;

	return false;
}

bool
db::const_iterator::operator>(const const_iterator &o)
const
{
	if(*this && o)
	{
		const auto &a(state->it->key());
		const auto &b(o.state->it->key());
		return a.compare(b) == 1;
	}

	if(!*this && o)
		return true;

	if(!*this && !o)
		return false;

	assert(!*this && o);
	return false;
}

bool
db::const_iterator::operator<(const const_iterator &o)
const
{
	if(*this && o)
	{
		const auto &a(state->it->key());
		const auto &b(o.state->it->key());
		return a.compare(b) == -1;
	}

	if(!*this && o)
		return false;

	if(!*this && !o)
		return false;

	assert(*this && !o);
	return true;
}

bool
db::const_iterator::operator!()
const
{
	if(!state)
		return true;

	if(!state->it)
		return true;

	if(!state->it->Valid())
		return true;

	return false;
}

db::const_iterator::operator bool()
const
{
	return !!*this;
}

rocksdb::Options
db::make_opts(const opts &opts)
{
	rocksdb::Options ret;
	ret.create_if_missing = true;            // They default this to false, but we invert the option

	for(const auto &o : opts) switch(o.first)
	{
		case opt::NO_CREATE:
			ret.create_if_missing = false;
			continue;

		case opt::NO_EXISTING:
			ret.error_if_exists = true;
			continue;

		case opt::NO_CHECKSUM:
			ret.paranoid_checks = false;
			continue;

		case opt::NO_MADV_DONTNEED:
			ret.allow_os_buffer = false;
			continue;

		case opt::NO_MADV_RANDOM:
			ret.advise_random_on_open = false;
			continue;

		case opt::FALLOCATE:
			ret.allow_fallocate = true;
			continue;

		case opt::NO_FALLOCATE:
			ret.allow_fallocate = false;
			continue;

		case opt::NO_FDATASYNC:
			ret.disableDataSync = true;
			continue;

		case opt::FSYNC:
			ret.use_fsync = true;
			continue;

		case opt::MMAP_READS:
			ret.allow_mmap_reads = true;
			continue;

		case opt::MMAP_WRITES:
			ret.allow_mmap_writes = true;
			continue;

		case opt::STATS_THREAD:
			ret.enable_thread_tracking = true;
			continue;

		case opt::STATS_MALLOC:
			ret.dump_malloc_stats = true;
			continue;

		case opt::OPEN_FAST:
			ret.skip_stats_update_on_db_open = true;
			continue;

		case opt::OPEN_BULKLOAD:
			ret.PrepareForBulkLoad();
			continue;

		case opt::OPEN_SMALL:
			ret.OptimizeForSmallDb();
			continue;

		default:
			continue;
	}

	return ret;
}

rocksdb::ReadOptions
db::make_opts(const gopts &opts,
              const bool &iterator)
{
	rocksdb::ReadOptions ret;

	if(iterator)
		ret.fill_cache = false;

	for(const auto &opt : opts) switch(opt.first)
	{
		case get::PIN:
			ret.pin_data = true;
			continue;

		case get::CACHE:
			ret.fill_cache = true;
			continue;

		case get::NO_CACHE:
			ret.fill_cache = false;
			continue;

		case get::NO_CHECKSUM:
			ret.verify_checksums = false;
			continue;

		case get::READAHEAD:
			ret.readahead_size = opt.second;
			continue;

		default:
			continue;
	}

	return ret;
}

rocksdb::WriteOptions
db::make_opts(const sopts &opts)
{
	rocksdb::WriteOptions ret;
	for(const auto &opt : opts) switch(opt.first)
	{
		case set::FSYNC:
			ret.sync = true;
			continue;

		case set::NO_JOURNAL:
			ret.disableWAL = true;
			continue;

		case set::MISSING_COLUMNS:
			ret.ignore_missing_column_families = true;
			continue;

		default:
			continue;
	}

	return ret;
}

std::unique_ptr<rocksdb::Iterator>
db::seek(rocksdb::DB &db,
         const rocksdb::Slice &key,
         rocksdb::ReadOptions &opts)
{
	using rocksdb::Iterator;

	// Perform a query which won't be allowed to do kernel IO
	opts.read_tier = NON_BLOCKING;

	std::unique_ptr<Iterator> it(db.NewIterator(opts));
	seek(*it, key);

	if(it->status().IsIncomplete())
	{
		// DB cache miss: reset the iterator to blocking mode and offload it
		opts.read_tier = BLOCKING;
		it.reset(db.NewIterator(opts));
		ctx::offload([&] { seek(*it, key); });
	}
	// else DB cache hit; no context switch; no thread switch; no kernel I/O; gg

	return std::move(it);
}

template<class pos>
void
db::seek(rocksdb::DB &db,
         const pos &p,
         rocksdb::ReadOptions &opts,
         std::unique_ptr<rocksdb::Iterator> &it)
{
	// Start with a non-blocking query
	if(!it || opts.read_tier == BLOCKING)
	{
		opts.read_tier = NON_BLOCKING;
		it.reset(db.NewIterator(opts));
	}

	seek(*it, p);
	if(it->status().IsIncomplete())
	{
		// DB cache miss: reset the iterator to blocking mode and offload it
		opts.read_tier = BLOCKING;
		it.reset(db.NewIterator(opts));
		ctx::offload([&] { seek(*it, p); });
	}
}

void
db::seek(rocksdb::Iterator &it,
         const rocksdb::Slice &sk)
{
	it.Seek(sk);
}

void
db::seek(rocksdb::Iterator &it,
         const pos &p)
{
	switch(p)
	{
		case pos::NEXT:     it.Next();           break;
		case pos::PREV:     it.Prev();           break;
		case pos::FRONT:    it.SeekToFirst();    break;
		case pos::BACK:     it.SeekToLast();     break;
		default:
		case pos::END:
		{
			it.SeekToLast();
			if(it.Valid())
				it.Next();

			break;
		}
	}
}

void
db::valid_equal_or_throw(const rocksdb::Iterator &it,
                         const rocksdb::Slice &sk)
{
	valid_or_throw(it);
	if(it.key().compare(sk) != 0)
		throw not_found();
}

void
db::valid_or_throw(const rocksdb::Iterator &it)
{
	if(!valid(it))
	{
		throw_on_error(it.status());
		assert(0); // status == ok + !Valid() == ???
	}
}

bool
db::valid(const rocksdb::Iterator &it)
{
	return it.Valid();
}

void
db::throw_on_error(const rocksdb::Status &s)
{
	using rocksdb::Status;

	switch(s.code())
	{
		case Status::kOk:                   return;
		case Status::kNotFound:             throw not_found();
		case Status::kCorruption:           throw corruption();
		case Status::kNotSupported:         throw not_supported();
		case Status::kInvalidArgument:      throw invalid_argument();
		case Status::kIOError:              throw io_error();
		case Status::kMergeInProgress:      throw merge_in_progress();
		case Status::kIncomplete:           throw incomplete();
		case Status::kShutdownInProgress:   throw shutdown_in_progress();
		case Status::kTimedOut:             throw timed_out();
		case Status::kAborted:              throw aborted();
		case Status::kBusy:                 throw busy();
		case Status::kExpired:              throw expired();
		case Status::kTryAgain:             throw try_again();
		default:
			throw error("Unknown error");
	}
}

std::string
db::path(const std::string &name)
{
	const auto prefix(path::get(path::DB));
	return path::build({prefix, name});
}