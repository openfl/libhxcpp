#include <hxcpp.h>
#include <time.h>
#include <memory>
#include <hx/Thread.h>
#include <hx/thread/CountingSemaphore.hpp>

// For single-threaded apps, such as Emscripten builds without pthreads.
// No other thread can release a permit while this one waits, so a permit is
// either there now or never: a timed wait sleeps out its timeout, and a wait
// without one throws instead of hanging.

struct hx::thread::CountingSemaphore_obj::Impl
{
	int permits;

	static void finalise(hx::Object* obj)
	{
		delete reinterpret_cast<hx::thread::CountingSemaphore_obj*>(obj)->impl;
	}
};

hx::thread::CountingSemaphore_obj::CountingSemaphore_obj(int value) : impl(nullptr)
{
	if (value < 0)
	{
		hx::Throw(HX_CSTRING("Failed to create semaphore"));
	}

	impl = new Impl();
	impl->permits = value;

	hx::GCSetFinalizer(this, Impl::finalise);
}

void hx::thread::CountingSemaphore_obj::acquire()
{
	if (impl->permits == 0)
	{
		hx::Throw(HX_CSTRING("Semaphore has no permits, and no other thread can release one"));
	}

	impl->permits--;
}

void hx::thread::CountingSemaphore_obj::release()
{
	if (impl->permits == 0x7FFFFFFF)
	{
		hx::Throw(HX_CSTRING("Failed to release semaphore"));
	}

	impl->permits++;
}

bool hx::thread::CountingSemaphore_obj::tryAcquire(Null<double> timeout)
{
	// A negative timeout waits with no limit, as __hxcpp_lock_wait does: Haxe 5's
	// Lock.wait() passes -1 when it is given no timeout. NaN still returns at once.
	if (timeout.Default(0) < 0)
	{
		acquire();
		return true;
	}

	if (impl->permits > 0)
	{
		impl->permits--;
		return true;
	}

	double seconds = timeout.Default(0);
	if (seconds > 0)
	{
		struct timespec deadline;
		clock_gettime(CLOCK_MONOTONIC, &deadline);
		HxAddSeconds(deadline, seconds);
		while (EINTR == clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr))
		{
		}
	}

	return false;
}
