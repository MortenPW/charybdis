/*
 *  Copyright (C) 2016 Charybdis Development Team
 *  Copyright (C) 2016 Jason Volk <jason@zemos.net>
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
#define HAVE_IRCD_CTX_POOL_H

namespace ircd {
namespace ctx  {

struct pool
{
	using closure = std::function<void ()>;

  private:
	size_t stack_size;
	size_t available;
	struct dock dock;
	std::deque<closure> queue;
	std::vector<context> ctxs;

	void next();
	void main();

  public:
	auto size() const                            { return ctxs.size();                             }
	auto avail() const                           { return available;                               }
	auto queued() const                          { return queue.size();                            }

	void add(const size_t & = 1);
	void del(const size_t & = 1);

	void operator()(closure);
	template<class F, class... A> future_void<F, A...> async(F&&, A&&...);
	template<class F, class... A> future_value<F, A...> async(F&&, A&&...);

	pool(const size_t & = 0, const size_t &stack_size = DEFAULT_STACK_SIZE);
	~pool() noexcept;
};

template<class F,
         class... A>
future_value<F, A...>
pool::async(F&& f,
            A&&... a)
{
	using R = typename std::result_of<F (A...)>::type;

	auto func(std::bind(std::forward<F>(f), std::forward<A>(a)...));
	auto p(std::make_shared<promise<R>>());
	(*this)([p, func(std::move(func))]
	() -> void
	{
		p->set_value(func());
	});

	return future<R>(*p);
}

template<class F,
         class... A>
future_void<F, A...>
pool::async(F&& f,
            A&&... a)
{
	using R = typename std::result_of<F (A...)>::type;

	auto func(std::bind(std::forward<F>(f), std::forward<A>(a)...));
	auto p(std::make_shared<promise<R>>());
	(*this)([p, func(std::move(func))]
	() -> void
	{
		func();
		p->set_value();
	});

	return future<R>(*p);
}

} // namespace ctx
} // namespace ircd