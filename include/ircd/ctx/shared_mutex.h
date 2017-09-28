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
#define HAVE_IRCD_CTX_SHARED_MUTEX_H

namespace ircd::ctx
{
	class shared_mutex;
}

class ircd::ctx::shared_mutex
{
	bool u;
	ssize_t s;
	std::deque<ctx *> q;

	void release();

  public:
	bool try_lock();
	bool try_lock_shared();
	bool try_lock_upgrade();

	template<class time_point> bool try_lock_until(time_point&&);
	template<class time_point> bool try_lock_shared_until(time_point&&);
	template<class time_point> bool try_lock_upgrade_until(time_point&&);

	template<class duration> bool try_lock_for(duration&&);
	template<class duration> bool try_lock_shared_for(duration&&);
	template<class duration> bool try_lock_upgrade_for(duration&&);

	void lock();
	void lock_shared();
	void lock_upgrade();

	bool try_unlock_shared_and_lock();
	bool try_unlock_shared_and_lock_upgrade();
	bool try_unlock_upgrade_and_lock();

	template<class time_point> bool try_unlock_shared_and_lock_until(time_point&&);
	template<class time_point> bool try_unlock_shared_and_lock_upgrade_until(time_point&&);
	template<class time_point> bool try_unlock_upgrade_and_lock_until(time_point&&);

	template<class duration> bool try_unlock_shared_and_lock_for(duration&&);
	template<class duration> bool try_unlock_shared_and_lock_upgrade_for(duration&&);
	template<class duration> bool try_unlock_upgrade_and_lock_for(duration&&);

	void unlock();
	void unlock_shared();
	void unlock_upgrade();
	void unlock_and_lock_shared();
	void unlock_and_lock_upgrade();
	void unlock_upgrade_and_lock();
	void unlock_upgrade_and_lock_shared();

	shared_mutex();
	~shared_mutex() noexcept;
};

inline
ircd::ctx::shared_mutex::shared_mutex()
:u{false}
,s{0}
{
}

inline
ircd::ctx::shared_mutex::~shared_mutex()
noexcept
{
	assert(!u);
	assert(s == 0);
	assert(q.empty());
}

inline void
ircd::ctx::shared_mutex::unlock_upgrade_and_lock_shared()
{
	++s;
	u = false;
	release();
}

inline void
ircd::ctx::shared_mutex::unlock_upgrade_and_lock()
{
	s = std::numeric_limits<decltype(s)>::min();
	u = false;
}

inline void
ircd::ctx::shared_mutex::unlock_and_lock_upgrade()
{
	s = 0;
	u = true;
}

inline void
ircd::ctx::shared_mutex::unlock_and_lock_shared()
{
	s = 1;
	release();
}

inline void
ircd::ctx::shared_mutex::unlock_upgrade()
{
	u = false;
	release();
}

inline void
ircd::ctx::shared_mutex::unlock_shared()
{
	--s;
	release();
}

inline void
ircd::ctx::shared_mutex::unlock()
{
	s = 0;
	release();
}

template<class duration>
bool
ircd::ctx::shared_mutex::try_unlock_upgrade_and_lock_for(duration&& d)
{
	return try_unlock_upgrade_and_lock_until(steady_clock::now() + d);
}

template<class duration>
bool
ircd::ctx::shared_mutex::try_unlock_shared_and_lock_upgrade_for(duration&& d)
{
	return try_unlock_shared_and_lock_upgrade_until(steady_clock::now() + d);
}

template<class duration>
bool
ircd::ctx::shared_mutex::try_unlock_shared_and_lock_for(duration&& d)
{
	return try_unlock_shared_and_lock_until(steady_clock::now() + d);
}

template<class time_point>
bool
ircd::ctx::shared_mutex::try_unlock_upgrade_and_lock_until(time_point&& tp)
{
	assert(0);
	return false;
}

template<class time_point>
bool
ircd::ctx::shared_mutex::try_unlock_shared_and_lock_upgrade_until(time_point&& tp)
{
	assert(0);
	return false;
}

template<class time_point>
bool
ircd::ctx::shared_mutex::try_unlock_shared_and_lock_until(time_point&& tp)
{
	assert(0);
	return false;
}

inline bool
ircd::ctx::shared_mutex::try_unlock_upgrade_and_lock()
{
	if(!try_lock())
		return false;

	u = false;
	return true;
}

inline bool
ircd::ctx::shared_mutex::try_unlock_shared_and_lock_upgrade()
{
	if(!try_lock_upgrade())
		return false;

	--s;
	return true;
}

inline bool
ircd::ctx::shared_mutex::try_unlock_shared_and_lock()
{
	if(s != 1)
		return false;

	s = 1;
	return true;
}

inline void
ircd::ctx::shared_mutex::lock_upgrade()
{
	if(likely(try_lock_upgrade()))
		return;

	q.emplace_back(&cur());
	while(!try_lock_upgrade())
		wait();
}

inline void
ircd::ctx::shared_mutex::lock_shared()
{
	if(likely(try_lock_shared()))
		return;

	q.emplace_back(&cur());
	while(!try_lock_shared())
		wait();
}

inline void
ircd::ctx::shared_mutex::lock()
{
	if(likely(try_lock()))
		return;

	q.emplace_back(&cur());
	while(!try_lock())
		wait();
}

template<class duration>
bool
ircd::ctx::shared_mutex::try_lock_upgrade_for(duration&& d)
{
	return try_lock_upgrade_until(steady_clock::now() + d);
}

template<class duration>
bool
ircd::ctx::shared_mutex::try_lock_shared_for(duration&& d)
{
	return try_lock_shared_until(steady_clock::now() + d);
}

template<class duration>
bool
ircd::ctx::shared_mutex::try_lock_for(duration&& d)
{
	return try_lock_until(steady_clock::now() + d);
}

template<class time_point>
bool
ircd::ctx::shared_mutex::try_lock_upgrade_until(time_point&& d)
{
	assert(0);
	return false;
}

template<class time_point>
bool
ircd::ctx::shared_mutex::try_lock_shared_until(time_point&& d)
{
	assert(0);
	return false;
}

template<class time_point>
bool
ircd::ctx::shared_mutex::try_lock_until(time_point&& tp)
{
	if(likely(try_lock()))
		return true;

	q.emplace_back(&cur());
	while(!try_lock())
	{
		if(unlikely(wait_until<std::nothrow_t>(tp)))
		{
			const auto it(std::find(begin(q), end(q), &cur()));
			assert(it != end(q));
			q.erase(it);
			return false;
		}
	}

	return true;
}

inline bool
ircd::ctx::shared_mutex::try_lock_upgrade()
{
	if(s < 0)
		return false;

	if(u)
		return false;

	u = true;
	return true;
}

inline bool
ircd::ctx::shared_mutex::try_lock_shared()
{
	++s;
	return s >= 0;
}

inline bool
ircd::ctx::shared_mutex::try_lock()
{
	if(s)
		return false;

	s = std::numeric_limits<decltype(s)>::min();
	return true;
}

inline void
ircd::ctx::shared_mutex::release()
{
	ctx *next; do
	{
		if(!q.empty())
		{
			next = q.front();
			q.pop_front();
		}
		else next = nullptr;
	}
	while(next == &cur());

	if(next)
		yield(*next);
}
