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

	// Keeps a zone the caller is in, as Mutex does
	hx::AutoGCFreeZone zone;

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
		zone.close();
		hx::Throw(HX_CSTRING("Failed to wait on semaphore"));
	}
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
	// -1 waits with no limit: Haxe 5.0.0-preview.1's Lock.wait() passes it when
	// given no timeout. Any other negative timeout has run out already, as in
	// tryAcquire(deadline - now), and returns at once like 0, as does NaN; it
	// waited for ever before (libhxcpp #461)
	if (timeout.Default(0) == -1)
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
		hx::AutoGCFreeZone zone;

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
			return true;
		}
		else
		{
			zone.close();
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
