#include <hxcpp.h>
#include <dispatch/dispatch.h>
#include <cmath>
#include <hx/thread/CountingSemaphore.hpp>

struct hx::thread::CountingSemaphore_obj::Impl
{
	dispatch_semaphore_t semaphore;

	static void finalise(hx::Object* obj)
	{
		auto semaphore = reinterpret_cast<hx::thread::CountingSemaphore_obj*>(obj);

		dispatch_release(semaphore->impl->semaphore);

		delete semaphore->impl;
	}
};

hx::thread::CountingSemaphore_obj::CountingSemaphore_obj(int value) : impl(new Impl())
{
	// libdispatch aborts when a semaphore is released below its initial value
	if (value < 0 || nullptr == (impl->semaphore = dispatch_semaphore_create(0)))
	{
		delete impl;
		impl = nullptr;
		hx::Throw(HX_CSTRING("Failed to create semaphore"));
	}

	for (int i = 0; i < value; i++)
	{
		dispatch_semaphore_signal(impl->semaphore);
	}

	hx::GCSetFinalizer(this, Impl::finalise);
}

void hx::thread::CountingSemaphore_obj::acquire()
{
	hx::EnterGCFreeZone();

	if (0 != dispatch_semaphore_wait(impl->semaphore, DISPATCH_TIME_FOREVER))
	{
		hx::ExitGCFreeZone();
		hx::Throw(HX_CSTRING("Failed to wait for semaphore"));
	}

	hx::ExitGCFreeZone();
}

void hx::thread::CountingSemaphore_obj::release()
{
	dispatch_semaphore_signal(impl->semaphore);
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

	hx::AutoGCFreeZone zone;

	double seconds = timeout.Default(0);
	dispatch_time_t deadline = DISPATCH_TIME_NOW;
	// double->int64_t is undefined past 2^63 nanoseconds
	if (seconds >= 9.2e9)
		deadline = DISPATCH_TIME_FOREVER;
	// Convert to nanoseconds before truncating - casting the seconds value
	// first discards the fractional part, so tryAcquire(0.5) returned
	// immediately instead of waiting 500ms
	else if (seconds > 0)
		deadline = dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(std::ceil(seconds * 1e9)));

	return 0 == dispatch_semaphore_wait(impl->semaphore, deadline);
}