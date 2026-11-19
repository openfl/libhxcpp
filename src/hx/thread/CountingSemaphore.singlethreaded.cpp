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
	// -1 waits with no limit: Haxe 5.0.0-preview.1's Lock.wait() passes it when
	// given no timeout. Any other negative timeout has run out already, as in
	// tryAcquire(deadline - now), and returns at once like 0, as does NaN; it
	// waited for ever before (libhxcpp #461)
	if (timeout.Default(0) == -1)
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
