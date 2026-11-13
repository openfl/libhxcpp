#include <hxcpp.h>
#include <thread>
#include <array>
#include <atomic>
#include <cstdio>
#include <system_error>
#include <hx/thread/CountingSemaphore.hpp>
#include "ThreadImpl.hpp"

namespace
{
	struct ThreadInfoHolder
	{
		hx::Object** root = nullptr;

		~ThreadInfoHolder()
		{
			if (root)
			{
				hx::GCRemoveRoot(root);

				delete root;
			}
		}

		void set(hx::Object** _root)
		{
			root = _root;
		}

		hx::thread::ThreadImpl_obj* get() const
		{
			return root ? reinterpret_cast<hx::thread::ThreadImpl_obj*>(*root) : nullptr;
		}
	};

	thread_local ThreadInfoHolder tls;

	// 0 is the main thread's: static initialisation runs on it
	std::atomic_int nextThreadnumber(1);
	const std::thread::id mainThreadId = std::this_thread::get_id();
	thread_local int attachedId = -1;

	// For a thread without a ThreadImpl, which current() would allocate
	int unregisteredId()
	{
		if (std::this_thread::get_id() == mainThreadId)
			return 0;
		if (attachedId < 0)
			attachedId = nextThreadnumber++;
		return attachedId;
	}

	void run(hx::thread::ThreadImpl thread, hx::thread::Thread_obj::CreateFunction job, hx::thread::CountingSemaphore semaphore)
	{
		// info[1] will the the "top of stack" - values under this
		// (ie info[0] and other stack values) will be in the GC conservative range
		// 
		// I'm assuming the array of two elements is some trick to ensure it's on the stack.
		// Maybe we could use OS specific functions to get the stack range?

		auto root = new hx::Object* { thread.GetPtr() };

		hx::GCAddRoot(root);

		tls.set(root);

		auto info = std::array<hx::thread::ThreadImpl_obj*, 2>{ thread.GetPtr(), nullptr };

		hx::SetTopOfStack(reinterpret_cast<int*>(&info[1]), true);

		// Release the creation function
		semaphore->release();

		// An exception escaping a std::thread's function is std::terminate -
		// the whole process died with no diagnostic at all
		try
		{
			job();
		}
		catch(Dynamic e)
		{
			hx::strbuf buf;
			String err = e==null() ? HX_CSTRING("null") : e->toString();
			fprintf(stderr, "Uncaught exception in thread: %s\n", err.utf8_str(&buf));
		}
		catch(...)
		{
			fprintf(stderr, "Uncaught native exception in thread\n");
		}

		hx::UnregisterCurrentThread();
	}
}

hx::thread::Thread hx::thread::Thread_obj::create(CreateFunction job)
{
#ifdef __EMSCRIPTEN__
	return hx::Throw(HX_CSTRING("Threads are not supported on Emscripten"));
#else

	auto semaphore = new hx::thread::CountingSemaphore_obj(0);
	ThreadImpl_obj* obj;
	try
	{
		obj = new ThreadImpl_obj(nextThreadnumber++, job, semaphore);
	}
	catch (const std::system_error&)
	{
		return hx::Throw(HX_CSTRING("Could not create thread"));
	}

	hx::GCSetFinalizer(obj, ThreadImpl_obj::finalise);
	hx::GCPrepareMultiThreaded();
	
	semaphore->acquire();

	return hx::thread::Thread{ obj };
#endif
}

String hx::thread::ThreadImpl_obj::toString()
{
	return String(id);
}

hx::thread::Thread hx::thread::Thread_obj::current()
{
	auto info = tls.get();
	if (nullptr == info)
	{
		// Threads created from Haxe have the TLS set, and the GC sets the TLS for the main thread.
		// So, if the TLS is null then we are in a attached foreign thread, which will just get assigned the next Id.
		// 
		// The C++ std has no way of getting the native handle of the current thread, so we need to use OS specific
		// apis to get that so we can get and set thread names.
		info = new ThreadImpl_obj(unregisteredId());

		auto root = new hx::Object* { info };

		hx::GCSetFinalizer(info, ThreadImpl_obj::finalise);
		hx::GCAddRoot(root);

		tls.set(root);
	}

	return Thread{ info };
}

int hx::thread::Thread_obj::id()
{
	auto info = tls.get();

	if (nullptr == info)
	{
		return unregisteredId();
	}
	else
	{
		return info->id;
	}
}

// An incremental slice can find the new object on this stack while one of
// these is being allocated, and the cycle can end at the next allocation. The
// mutation scope has that last stop scan the object again, so the arrays
// stored after it was found are kept too.
void hx::thread::ThreadImpl_obj::makeArrays()
{
	HX_INCREMENTAL_MUTATION(this);
	scratch = Array<uint8_t>(std::numeric_limits<uint16_t>::max(), std::numeric_limits<uint16_t>::max());
	slots = Array<Dynamic>(0, 0);
	localSlots = Array<Dynamic>(0, 0);
	localGenerations = Array<int>(0, 0);
}

hx::thread::ThreadImpl_obj::ThreadImpl_obj(const int _id)
	: id(_id)
	, native(nullptr)
{
	makeArrays();
	native = new Native();
	HX_OBJ_WB_NEW_MARKED_OBJECT(this);
}

hx::thread::ThreadImpl_obj::ThreadImpl_obj(const int _id, Thread_obj::CreateFunction _job, CountingSemaphore _semaphore)
	: id(_id)
	, native(nullptr)
{
	// Before the thread starts, which reads them
	makeArrays();
	native = new Native(_job, this, _semaphore);
	HX_OBJ_WB_NEW_MARKED_OBJECT(this);
}

hx::thread::ThreadImpl_obj::Native::Native(Thread_obj::CreateFunction _job, ThreadImpl _thread, CountingSemaphore _semaphore)
	: thread(run, _thread, _job, _semaphore)
	, handle(thread.native_handle())
	, ownsHandle(false)
{
	// Only Windows implements thread name getting and setting currently, and this requires the thread to be attached.
	// Threads are normally only detached once the thread object is finalised and this was causing the 32bit linux tests to hit OS resource limits.
	// This does feel like a bit of a bodge and something which will actually need to be addressed to add name setting on Linux.

#ifndef HX_WINDOWS
	thread.detach();
#endif
}

Dynamic hx::thread::ThreadImpl_obj::getSlot(const int id)
{
	return slots[id];
}

void hx::thread::ThreadImpl_obj::setSlot(const int id, const Dynamic& obj)
{
	slots->__SetItem(id, obj);
}

// A value stored by an earlier owner of a recycled slot reads as null, and
// is dropped so it does not stay alive
Dynamic hx::thread::ThreadImpl_obj::getLocal(const int slot, const int generation)
{
	if (slot >= localGenerations->length || localGenerations[slot] != generation)
	{
		if (slot < localSlots->length)
			localSlots->__SetItem(slot, null());
		return null();
	}
	return localSlots[slot];
}

void hx::thread::ThreadImpl_obj::setLocal(const int slot, const int generation, const Dynamic& obj)
{
	localSlots->__SetItem(slot, obj);
	localGenerations[slot] = generation;
}

void hx::thread::ThreadImpl_obj::__Mark(HX_MARK_PARAMS)
{
	HX_MARK_MEMBER(scratch);
	HX_MARK_MEMBER(slots);
	// Values of collected ThreadLocals can never be read again. The constructor
	// can be marked before it has made localSlots.
	for (int i = 0; localSlots.mPtr && i < localSlots->length; i++)
		if (i >= localGenerations->length || localGenerations[i] != ThreadLocalLiveGeneration(i))
			localSlots->__unsafe_set(i, null());
	HX_MARK_MEMBER(localSlots);
	HX_MARK_MEMBER(localGenerations);
}

#ifdef HXCPP_VISIT_ALLOCS

void hx::thread::ThreadImpl_obj::__Visit(HX_VISIT_PARAMS)
{
	HX_VISIT_MEMBER(scratch);
	HX_VISIT_MEMBER(slots);
	HX_VISIT_MEMBER(localSlots);
	HX_VISIT_MEMBER(localGenerations);
}

void hx::thread::ThreadImpl_obj::finalise(hx::Object* obj)
{
	auto thread = reinterpret_cast<ThreadImpl_obj*>(obj);
	auto native = std::unique_ptr<ThreadImpl_obj::Native>{ thread->native };

	if (native->thread.joinable())
	{
		native->thread.detach();
	}
}

#endif