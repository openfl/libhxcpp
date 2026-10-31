#include <hxcpp.h>
#include <semaphore.h>
#include <time.h>
#include <memory>
#include <cerrno>
#include <hx/Thread.h>
#include <hx/thread/CountingSemaphore.hpp>

// sem_timedwait takes a deadline on the wall clock, which changing the system time moves
#if (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 30))) || (defined(__ANDROID__) && __ANDROID_API__ >= 30)
#define HX_SEM_CLOCKWAIT
#endif

struct hx::thread::CountingSemaphore_obj::Impl
{
	sem_t semaphore;

	static void finalise(hx::Object* obj)
	{
		auto impl = std::unique_ptr<Impl>(reinterpret_cast<hx::thread::CountingSemaphore_obj*>(obj)->impl);

		sem_destroy(&impl->semaphore);
	}
};

hx::thread::CountingSemaphore_obj::CountingSemaphore_obj(int value) : impl(new Impl())
{
	if (0 != (sem_init(&impl->semaphore, false, value)))
	{
		delete impl;
		impl = nullptr;
		hx::Throw(HX_CSTRING("Failed to create semaphore"));
	}

	hx::GCSetFinalizer(this, Impl::finalise);
}

void hx::thread::CountingSemaphore_obj::acquire()
{
	// Uncontended fast path - skip the GC free zone unless we will block.
	// A thread that only ever takes the semaphore uncontended, allocating
	// nothing, otherwise never reaches a safe point, and a collection waits
	// for it for ever.
	if (hx::gPauseForCollect)
		__hxcpp_gc_safe_point();
	if (0 == sem_trywait(&impl->semaphore))
		return;

	hx::EnterGCFreeZone();

	// sem_wait is never restarted after a caught signal, regardless of
	// SA_RESTART - retry on EINTR rather than throwing spuriously
	int r;
	do
	{
		r = sem_wait(&impl->semaphore);
	}
	while (0 != r && EINTR == errno);

	if (0 != r)
	{
		hx::ExitGCFreeZone();
		hx::Throw(HX_CSTRING("Failed to wait on semaphore"));
	}

	hx::ExitGCFreeZone();
}

void hx::thread::CountingSemaphore_obj::release()
{
	if (0 != sem_post(&impl->semaphore))
	{
		hx::Throw(HX_CSTRING("Failed to release semaphore"));
	}
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

	if (hx::gPauseForCollect)
		__hxcpp_gc_safe_point();
	double seconds = timeout.Default(0);
	if (!(seconds > 0))
	{
		return 0 == sem_trywait(&impl->semaphore);
	}
	else
	{
		hx::EnterGCFreeZone();

		struct timespec t;
		#ifdef HX_SEM_CLOCKWAIT
		clock_gettime(CLOCK_MONOTONIC, &t);
		#else
		clock_gettime(CLOCK_REALTIME, &t);
		#endif
		HxAddSeconds(t, seconds);

		// Retry on EINTR - the absolute deadline keeps the timeout intact
		int r;
		do
		{
			#ifdef HX_SEM_CLOCKWAIT
			r = sem_clockwait(&impl->semaphore, CLOCK_MONOTONIC, &t);
			#else
			r = sem_timedwait(&impl->semaphore, &t);
			#endif
		}
		while (0 != r && EINTR == errno);
		int error = errno;

		if (0 == r)
		{
			hx::ExitGCFreeZone();
			return true;
		}
		else
		{
			hx::ExitGCFreeZone();
			if (error == ETIMEDOUT)
			{
				return false;
			}
			else
			{
				return hx::Throw(HX_CSTRING("Failed to wait for semaphore"));
			}
		}
	}
}
