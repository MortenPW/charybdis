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
#define HAVE_IRCD_CTX_MUTEX_H

namespace ircd {
namespace ctx  {

class mutex
{
	bool m;
	std::deque<ctx *> q;

  public:
	bool try_lock();
	template<class time_point> bool try_lock_until(const time_point &);
	template<class duration> bool try_lock_for(const duration &);

	void lock();
	void unlock();

	mutex();
	~mutex() noexcept;
};

inline
mutex::mutex():
m(false)
{
}

inline
mutex::~mutex()
noexcept
{
	assert(!m);
	assert(q.empty());
}

inline void
mutex::unlock()
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

	m = false;

	if(next)
		notify(*next);
}

inline void
mutex::lock()
{
	if(likely(try_lock()))
		return;

	q.emplace_back(&cur());
	while(!try_lock())
		wait();
}

template<class duration>
bool
mutex::try_lock_for(const duration &d)
{
	return try_lock_until(steady_clock::now() + d);
}

template<class time_point>
bool
mutex::try_lock_until(const time_point &tp)
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
mutex::try_lock()
{
	if(m)
		return false;

	m = true;
	return true;
}

} // namespace ctx
} // namespace ircd