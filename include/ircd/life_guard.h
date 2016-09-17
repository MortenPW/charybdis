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
#define HAVE_IRCD_LIFE_GUARD_H

#ifdef __cplusplus
namespace ircd {

// Tests if type inherits from std::enable_shared_from_this<>
template<class T>
constexpr typename std::enable_if<is_complete<T>::value, bool>::type
is_shared_from_this()
{
	return std::is_base_of<std::enable_shared_from_this<T>, T>();
}

// Unconditional failure for fwd-declared incomplete types, which
// obviously don't inherit from std::enable_shared_from_this<>
template<class T>
constexpr typename std::enable_if<!is_complete<T>::value, bool>::type
is_shared_from_this()
{
	return false;
}

/* Use the life_guard to keep an object alive within a function running in a context.
 *
 * Example:
 *
	void foo(client &c)
	{
		const life_guard<client> lg(c);

		c.call();      // This call was always safe with or w/o life_guard.
		ctx::wait();   // The context has now yielded and another context might destroy client &c
		c.call();      // The context continues and this would have made a call on a dead c.
	}
*/
template<class T>
struct life_guard
:std::shared_ptr<T>
{
	// This constructor is used when the templated type inherits from std::enable_shared_from_this<>
	template<class SFINAE = T>
	life_guard(T &t,
	           typename std::enable_if<is_shared_from_this<SFINAE>(), void>::type * = 0)
	:std::shared_ptr<T>(t.shared_from_this())
	{
	}

	// This constructor uses our convention for forward declaring objects that internally
	// inherit from std::enable_shared_from_this<>. Our convention is to provide:
	//
	//     std::shared_ptr<T> shared_from(T &c);
	//
	template<class SFINAE = T>
	life_guard(T &t,
	           typename std::enable_if<!is_shared_from_this<SFINAE>(), void>::type * = 0)
	:std::shared_ptr<T>(shared_from(t))
	{
	}

	// This constructor is used with a weak_ptr of the type. This throws an exception
	// to abort the scope when the object already died before being able to guard at all.
	life_guard(const std::weak_ptr<T> &wp)
	:std::shared_ptr<T>(wp.lock())
	{
		if(wp.expired())
			throw std::bad_weak_ptr();
	}
};

}      // namespace ircd
#endif // __cplusplus