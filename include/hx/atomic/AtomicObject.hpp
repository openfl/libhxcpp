#pragma once

#ifndef HXCPP_H
#include <hxcpp.h>
#endif

#include <atomic>

HX_DECLARE_CLASS2(hx, atomic, AtomicObject)

namespace hx
{
	namespace atomic
	{
		// What Haxe 5's haxe.atomic.AtomicObject holds. Like AtomicInt, each operation is a
		// GC safe point rather than a GC free zone: a collection that ran inside a free zone
		// could move the object a load had just read.
		class AtomicObject_obj final : public ::hx::Object
		{
			std::atomic<::hx::Object *> atomic;

		public:
			inline AtomicObject_obj(::Dynamic val) : atomic(val.mPtr)
			{
				HX_OBJ_WB_NEW_MARKED_OBJECT(this);
			}

			void __Mark(::hx::MarkContext *__inCtx) HXCPP_OVERRIDE
			{
				::Dynamic ptr = atomic.load();
				HX_MARK_MEMBER(ptr);
			}

			#ifdef HXCPP_VISIT_ALLOCS
			void __Visit(::hx::VisitContext *__inCtx) HXCPP_OVERRIDE
			{
				::hx::Object *ptr = atomic.load();
				HX_VISIT_MEMBER(ptr);
				atomic.store(ptr);
			}
			#endif

			inline ::Dynamic store(::Dynamic val)
			{
				__hxcpp_gc_safe_point();
				atomic.store(val.mPtr);
				HX_OBJ_WB_GET(this, val.mPtr);
				return val;
			}

			inline ::Dynamic load()
			{
				__hxcpp_gc_safe_point();
				return atomic.load();
			}

			inline ::Dynamic exchange(::Dynamic val)
			{
				__hxcpp_gc_safe_point();
				::Dynamic old = atomic.exchange(val.mPtr);
				HX_OBJ_WB_GET(this, val.mPtr);
				return old;
			}

			// Compares with Dynamic's ==, as the intrinsics Haxe 4 uses do, so boxed numbers
			// and strings compare by value. Returns the value it found.
			inline ::Dynamic compareExchange(::Dynamic expected, ::Dynamic replacement)
			{
				__hxcpp_gc_safe_point();
				::Dynamic original = atomic.load();
				while (original == expected)
				{
					// On failure, this writes the current value into original and tries again
					if (atomic.compare_exchange_weak(original.mPtr, replacement.mPtr))
					{
						HX_OBJ_WB_GET(this, replacement.mPtr);
						return original;
					}
				}
				return original;
			}
		};
	}
}
