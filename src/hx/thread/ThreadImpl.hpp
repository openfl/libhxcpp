#pragma once

#ifndef HXCPP_H
#include <hxcpp.h>
#endif

#include <hx/thread/Thread.hpp>
#include <hx/thread/CountingSemaphore.hpp>
#include <array>
#include <thread>

HX_DECLARE_CLASS2(hx, thread, ThreadImpl)

namespace hx
{
	namespace thread
	{
		int ThreadLocalLiveGeneration(int slot);

		class ThreadImpl_obj final : public Thread_obj
		{
			int cursor = 0;
			// Start of the newest pooled scratch allocation, or -1
			int scratchTop = -1;
			Array<uint8_t> scratch;
			Array<Dynamic> slots;
			// ThreadLocal values, apart from the legacy __hxcpp_tls_* slots
			Array<Dynamic> localSlots;
			Array<int> localGenerations;

			void makeArrays();

		public:
			friend class Scratch;

			struct Native
			{
				std::thread thread;
				std::thread::native_handle_type handle;
				bool ownsHandle;

				Native();
				Native(Thread_obj::CreateFunction _job, ThreadImpl _thread, CountingSemaphore _semaphore);
				~Native();
			};

			const int id;
			Native* native;

			ThreadImpl_obj(const int _id);
			ThreadImpl_obj(const int _id, Thread_obj::CreateFunction _run, CountingSemaphore _semaphore);

			String getName() HXCPP_OVERRIDE;
			void setName(const String& name) HXCPP_OVERRIDE;

			Dynamic getSlot(const int id);
			void setSlot(const int id, const Dynamic& obj);
			Dynamic getLocal(const int slot, const int generation);
			void setLocal(const int slot, const int generation, const Dynamic& obj);

			String toString() HXCPP_OVERRIDE;

			void __Mark(HX_MARK_PARAMS) HXCPP_OVERRIDE;
#ifdef HXCPP_VISIT_ALLOCS
			void __Visit(HX_VISIT_PARAMS) HXCPP_OVERRIDE;
#endif

			static void finalise(hx::Object* obj);
		};
	}
}
