#pragma once

#ifndef HXCPP_H
#include <hxcpp.h>
#endif

#include <atomic>

HX_DECLARE_CLASS2(hx, atomic, AtomicInt)

namespace hx
{
	namespace atomic
	{
		// What Haxe 5's haxe.atomic.AtomicInt holds. The atomic lives in the object, so
		// nothing outside the GC heap outlives it. Atomic operations never block, so they
		// don't enter a GC free zone; each one is a GC safe point instead, so a loop that
		// spins on atomics without allocating can't stall a collection.
		class AtomicInt_obj final : public ::hx::Object
		{
			std::atomic<int> atomic;

		public:
			// Holds no pointers, so the collector has nothing to scan in it
			inline void *operator new(size_t inSize)
			{
				return ::hx::Object::operator new(inSize, false, "hx.atomic.AtomicInt");
			}

			inline AtomicInt_obj(int val) : atomic(val) {}

			inline int fetchAdd(int val)
			{
				__hxcpp_gc_safe_point();
				return atomic.fetch_add(val);
			}

			inline int fetchSub(int val)
			{
				__hxcpp_gc_safe_point();
				return atomic.fetch_sub(val);
			}

			inline int fetchAnd(int val)
			{
				__hxcpp_gc_safe_point();
				return atomic.fetch_and(val);
			}

			inline int fetchOr(int val)
			{
				__hxcpp_gc_safe_point();
				return atomic.fetch_or(val);
			}

			inline int fetchXor(int val)
			{
				__hxcpp_gc_safe_point();
				return atomic.fetch_xor(val);
			}

			inline int store(int val)
			{
				__hxcpp_gc_safe_point();
				atomic.store(val);
				return val;
			}

			inline int load()
			{
				__hxcpp_gc_safe_point();
				return atomic.load();
			}

			inline int exchange(int val)
			{
				__hxcpp_gc_safe_point();
				return atomic.exchange(val);
			}

			// Returns the value it found, which equals expected only when it stored
			inline int compareExchange(int expected, int replacement)
			{
				__hxcpp_gc_safe_point();
				// On failure, compare_exchange_strong writes the current value into expected
				atomic.compare_exchange_strong(expected, replacement);
				return expected;
			}
		};
	}
}
