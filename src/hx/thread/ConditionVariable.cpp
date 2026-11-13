#include <hxcpp.h>
#include "hx/thread/ConditionVariable.hpp"
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>

struct hx::thread::ConditionVariable_obj::Impl
{
	std::mutex mutex;
	std::condition_variable_any condition;
	// Signals and broadcasts so far, which tell a timed wait that one came
	std::atomic<unsigned int> signals{ 0 };
};

hx::thread::ConditionVariable_obj::ConditionVariable_obj() : impl(new Impl())
{
	GCSetFinalizer(this, [](hx::Object* obj)
	{
		auto condition = reinterpret_cast<ConditionVariable_obj*>(obj);

		delete condition->impl;
	});
}

void hx::thread::ConditionVariable_obj::acquire()
{
	// Uncontended fast path - skip the GC free zone unless we will block
	// The free zone below was this thread's handshake with the collector. A
	// thread that only ever takes the lock uncontended, allocating nothing,
	// otherwise never reaches a safe point, and a collection waits for it
	// for ever.
	if (hx::gPauseForCollect)
		__hxcpp_gc_safe_point();
	if (impl->mutex.try_lock())
		return;

	hx::AutoGCFreeZone zone;

	impl->mutex.lock();
}

bool hx::thread::ConditionVariable_obj::tryAcquire()
{
	if (hx::gPauseForCollect)
		__hxcpp_gc_safe_point();
	return impl->mutex.try_lock();
}

void hx::thread::ConditionVariable_obj::release()
{
	impl->mutex.unlock();
}

void hx::thread::ConditionVariable_obj::wait()
{
	hx::AutoGCFreeZone zone;

	impl->condition.wait(impl->mutex);
}

// Haxe's Condition.timedWait: true once a signal or broadcast came, false when
//  the time ran out. NaN, zero and a negative time do not wait, and the wait is
//  capped at 1e9 seconds, which keeps the deadline inside the clock's range;
//  Haxe counts a capped wait that ran out as a spurious wakeup. MSVC's
//  wait_for and wait_until can return no_timeout just before the deadline with
//  nothing signalled, so a wakeup counts only when the count of signals moved,
//  and one without waits again until the deadline
bool hx::thread::ConditionVariable_obj::timedWait(double seconds)
{
	if (!(seconds > 0))
		seconds = 0;
	else if (seconds > 1e9)
		seconds = 1e9;

	auto deadline = std::chrono::steady_clock::now() +
		std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
	unsigned int before = impl->signals.load();

	hx::AutoGCFreeZone zone;

	while (true)
	{
		bool ranOut = impl->condition.wait_until(impl->mutex, deadline) == std::cv_status::timeout;
		if (impl->signals.load() != before)
			return true;
		if (ranOut || std::chrono::steady_clock::now() >= deadline)
			return false;
	}
}

void hx::thread::ConditionVariable_obj::signal()
{
	impl->signals++;
	impl->condition.notify_one();
}

void hx::thread::ConditionVariable_obj::broadcast()
{
	impl->signals++;
	impl->condition.notify_all();
}
