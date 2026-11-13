#include <hxcpp.h>

#if (HXCPP_API_LEVEL<500)

#include <hx/Thread.h>
#include <time.h>
#include <hx/thread/ConditionVariable.hpp>
#include <hx/thread/RecursiveMutex.hpp>
#include <hx/thread/CountingSemaphore.hpp>
#include "thread/ThreadImpl.hpp"
#include <atomic>

// --- Deque ----------------------------------------------------------

struct Deque : public Array_obj<Dynamic>
{
	Deque() : Array_obj<Dynamic>(0,0), head(0) { }

	static Deque *Create()
	{
		Deque *result = new Deque();
		result->mFinalizer = new hx::InternalFinalizer(result,clean);
		return result;
	}

	// Consumed prefix.  PopFront advances this instead of shift()ing,
	// which memmoved the entire remaining queue per pop - draining an
	// n-deep mailbox was O(n^2) bytes moved while holding the lock
	int head;

	inline bool hasItems() const { return length > head; }

	Dynamic popHead()
	{
		if (length <= head)
			return null();
		Dynamic *items = (Dynamic *)mBase;
		Dynamic result = items[head];
		items[head] = null();
		head++;
		if (head == length)
		{
			// Drained - recycle the buffer from the start, or give back what a
			// burst grew it to, which an idle mailbox would otherwise hold for
			// as long as it lives
			length = 0;
			head = 0;
			if (mAlloc > keepSlots)
				__SetSizeExact(0);
		}
		else if (head > 64 && head >= length - head)
		{
			// Compact once the consumed prefix dominates - O(1) amortized.
			// An incremental cycle may have scanned the slots the items move
			// to and not those they come from, so it scans the queue again
			HX_INCREMENTAL_MUTATION(this);
			int remaining = length - head;
			::memmove(items, items + head, remaining * sizeof(Dynamic));
			::memset(items + remaining, 0, head * sizeof(Dynamic));
			length = remaining;
			head = 0;
		}
		return result;
	}

	// Storage kept when the queue drains: enough for a busy mailbox's usual
	// backlog without growing again, little enough to hold while idle
	static const int keepSlots = 16384;

	void pushHead(Dynamic inValue)
	{
		if (head == 0)
			openFront();
		Dynamic *items = (Dynamic *)mBase;
		head--;
		items[head] = inValue;
		HX_OBJ_WB_GET(this, inValue.mPtr);
	}

	// Move the items up to leave free slots in front, half as many as there
	// are items.  unshift moved every item for each push at the front, so
	// building an n-deep queue that way was O(n^2) while holding the lock
	void openFront()
	{
		// The items move, as for a compaction in popHead
		HX_INCREMENTAL_MUTATION(this);
		int count = length;
		int gap = count >> 1;
		if (gap < 16)
			gap = 16;
		if (count > 0x7fffffff - gap)
			hx::Throw( HX_CSTRING("Array allocation too large") );
		reserve(count + gap);
		Dynamic *items = (Dynamic *)mBase;
		::memmove(items + gap, items, count * sizeof(Dynamic));
		::memset(items, 0, gap * sizeof(Dynamic));
		length = count + gap;
		head = gap;
	}
	static void clean(hx::Object *inObj)
	{
		Deque *d = dynamic_cast<Deque *>(inObj);
		if (d) d->Clean();
	}
	void Clean()
	{
		#ifdef HX_WINDOWS
		mMutex.Clean();
		#endif
		mSemaphore.Clean();
	}

   #ifdef HXCPP_VISIT_ALLOCS
	void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE
	{
		Array_obj<Dynamic>::__Visit(__inCtx);
		mFinalizer->Visit(__inCtx);
	}
   #endif


	// Try-lock first: the uncontended case skips two fence-bearing
	// GC-free-zone transitions per message (the same fast path the
	// sys.thread primitives use)
	template<typename LOCKABLE>
	inline void lockFor(LOCKABLE &inMutex)
	{
		// As for the sys.thread primitives: a consumer polling pop(false),
		// allocating nothing, would never reach a safe point otherwise.
		if (hx::gPauseForCollect)
			__hxcpp_gc_safe_point();
		if (!inMutex.TryLock())
		{
			hx::EnterGCFreeZone();
			inMutex.Lock();
			hx::ExitGCFreeZone();
		}
	}

	#ifndef HX_THREAD_SEMAPHORE_LOCKABLE
	HxMutex     mMutex;
	void PushBack(Dynamic inValue)
	{
		lockFor(mMutex);
		push(inValue);
		mSemaphore.Set();
		mMutex.Unlock();
	}
	void PushFront(Dynamic inValue)
	{
		lockFor(mMutex);
		pushHead(inValue);
		mSemaphore.Set();
		mMutex.Unlock();
	}


	Dynamic PopFront(bool inBlock)
	{
		lockFor(mMutex);
		if (inBlock)
		{
			// Ok - wait for something on stack...
			while(!hasItems())
			{
				mSemaphore.Reset();
				mMutex.Unlock();
				hx::EnterGCFreeZone();
				mSemaphore.Wait();
				hx::ExitGCFreeZone();
				lockFor(mMutex);
			}
		}
		// Re-signal while items remain, like the posix branch below -
		// otherwise two pushes whose Set calls coalesce on the auto-reset
		// event leave a second blocked consumer asleep with the item queued
		Dynamic result = popHead();
		if (hasItems())
			mSemaphore.Set();
		else if (inBlock)
			mSemaphore.Reset();
		mMutex.Unlock();
		return result;
	}
	#else
	void PushBack(Dynamic inValue)
	{
		lockFor(mSemaphore.mMutex);
		push(inValue);
		mSemaphore.QSet();
		mSemaphore.mMutex.Unlock();
	}
	void PushFront(Dynamic inValue)
	{
		lockFor(mSemaphore.mMutex);
		pushHead(inValue);
		mSemaphore.QSet();
		mSemaphore.mMutex.Unlock();
	}


	Dynamic PopFront(bool inBlock)
	{
		lockFor(mSemaphore.mMutex);
		if (inBlock && !hasItems())
		{
			hx::EnterGCFreeZone();
			while(!hasItems())
				mSemaphore.QWait();
			hx::ExitGCFreeZone();
		}
		Dynamic result = popHead();
		if (hasItems())
			mSemaphore.QSet();
		mSemaphore.mMutex.Unlock();
		return result;
	}
	#endif

	hx::InternalFinalizer *mFinalizer;
	HxSemaphore mSemaphore;
};

Dynamic __hxcpp_deque_create()
{
	return Deque::Create();
}

void __hxcpp_deque_add(Dynamic q,Dynamic inVal)
{
	Deque *d = dynamic_cast<Deque *>(q.mPtr);
	if (!d)
		throw HX_INVALID_OBJECT;
	d->PushBack(inVal);
}

void __hxcpp_deque_push(Dynamic q,Dynamic inVal)
{
	Deque *d = dynamic_cast<Deque *>(q.mPtr);
	if (!d)
		throw HX_INVALID_OBJECT;
	d->PushFront(inVal);
}

Dynamic __hxcpp_deque_pop(Dynamic q,bool block)
{
	Deque *d = dynamic_cast<Deque *>(q.mPtr);
	if (!d)
		throw HX_INVALID_OBJECT;
	return d->PopFront(block);
}

// --- Thread ----------------------------------------------------------

Dynamic __hxcpp_thread_create(Dynamic inStart)
{
	return hx::thread::Thread_obj::create(inStart);
}

Dynamic __hxcpp_thread_current()
{
	return hx::thread::Thread_obj::current();
}

void __hxcpp_thread_send(Dynamic inThread, Dynamic inMessage)
{
	hx::Throw(HX_CSTRING("Thread messages need Haxe 4.2 or later"));
}

Dynamic __hxcpp_thread_read_message(bool inBlocked)
{
	return hx::Throw(HX_CSTRING("Thread messages need Haxe 4.2 or later"));
}

bool __hxcpp_is_current_thread(hx::Object *inThread)
{
   return inThread == hx::thread::Thread_obj::current();
}

// --- TLS ------------------------------------------------------------

Dynamic __hxcpp_tls_get(int inID)
{
	return reinterpret_cast<hx::thread::ThreadImpl_obj*>(hx::thread::Thread_obj::current().GetPtr())->getSlot(inID);
}

void __hxcpp_tls_set(int inID,Dynamic inVal)
{
	reinterpret_cast<hx::thread::ThreadImpl_obj*>(hx::thread::Thread_obj::current().GetPtr())->setSlot(inID, inVal);
}

// --- Mutex ------------------------------------------------------------

Dynamic __hxcpp_mutex_create()
{
	return new hx::thread::RecursiveMutex_obj();
}
void __hxcpp_mutex_acquire(Dynamic inMutex)
{
	auto mutex = inMutex.Cast<hx::thread::RecursiveMutex>();

	mutex->acquire();
}
bool __hxcpp_mutex_try(Dynamic inMutex)
{
	auto mutex = inMutex.Cast<hx::thread::RecursiveMutex>();

	return mutex->tryAcquire();
}
void __hxcpp_mutex_release(Dynamic inMutex)
{
	auto mutex = inMutex.Cast<hx::thread::RecursiveMutex>();

	mutex->release();
}

// --- Semaphore ------------------------------------------------------------

Dynamic __hxcpp_semaphore_create(int value) {
	return new hx::thread::CountingSemaphore_obj(value);
}
void __hxcpp_semaphore_acquire(Dynamic inSemaphore) {
	auto semaphore = inSemaphore.Cast<hx::thread::CountingSemaphore>();

	semaphore->acquire();
}
bool __hxcpp_semaphore_try_acquire(Dynamic inSemaphore, double timeout) {
	auto semaphore = inSemaphore.Cast<hx::thread::CountingSemaphore>();

	return semaphore->tryAcquire(timeout);
}
void __hxcpp_semaphore_release(Dynamic inSemaphore) {
	auto semaphore = inSemaphore.Cast<hx::thread::CountingSemaphore>();

	semaphore->release();
}

// --- Condition ------------------------------------------------------------

Dynamic __hxcpp_condition_create(void)
{
	return new hx::thread::ConditionVariable_obj();
}
void __hxcpp_condition_acquire(Dynamic inCond)
{
	auto condition = inCond.Cast<hx::thread::ConditionVariable>();

	condition->acquire();
}
bool __hxcpp_condition_try_acquire(Dynamic inCond)
{
	auto condition = inCond.Cast<hx::thread::ConditionVariable>();

	return condition->tryAcquire();
}
void __hxcpp_condition_release(Dynamic inCond)
{
	auto condition = inCond.Cast<hx::thread::ConditionVariable>();

	condition->release();
}
void __hxcpp_condition_wait(Dynamic inCond)
{
	auto condition = inCond.Cast<hx::thread::ConditionVariable>();

	condition->wait();
}
bool __hxcpp_condition_timed_wait(Dynamic inCond, double timeout)
{
	return hx::Throw(HX_CSTRING("Not Implemented"));
}
void __hxcpp_condition_signal(Dynamic inCond)
{
	auto condition = inCond.Cast<hx::thread::ConditionVariable>();

	condition->signal();
}
void __hxcpp_condition_broadcast(Dynamic inCond)
{
	auto condition = inCond.Cast<hx::thread::ConditionVariable>();

	condition->broadcast();
}

// --- Lock ------------------------------------------------------------

class hxLock : public hx::Object
{
public:

	hxLock()
	{
		// Explicit rather than relying on GC allocations being zeroed
		mAvailable = 0;
		mFinalizer = new hx::InternalFinalizer(this);
		mFinalizer->mFinalizer = clean;
	}

   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdLock };

   #ifdef HXCPP_VISIT_ALLOCS
	void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE { mFinalizer->Visit(__inCtx); }
   #endif

	hx::InternalFinalizer *mFinalizer;

	#if defined(__SNC__)
	double Now()
	{
		return (double)clock()/CLOCKS_PER_SEC;
	}
	#else
	double Now()
	{
		// Monotonic, 64-bit and fine-grained: haxe.Timer.stamp's clock.
		// clock() is a 32-bit millisecond count on MSVC, which wraps
		// negative after ~24.8 days of process uptime and breaks every
		// timed wait from then on.  GetTickCount64 does not wrap, but it
		// moves only on the ~15.6ms system tick, so a deadline taken from
		// it came due up to a tick late: a 1ms wait took 15ms.  The wall
		// clock moves when the system time is changed
		return __time_stamp();
	}
	#endif

	static void clean(hx::Object *inObj)
	{
		hxLock *l = dynamic_cast<hxLock *>(inObj);
		if (l)
		{
			l->mNotEmpty.Clean();
			l->mAvailableLock.Clean();
		}
	}
	bool Wait(double inTimeout)
	{
		if (hx::gPauseForCollect)
			__hxcpp_gc_safe_point();
		double stop = 0;
		if (inTimeout>=0)
			stop = Now() + inTimeout;
		while(1)
		{
			mAvailableLock.Lock();
			if (mAvailable)
			{
				--mAvailable;
		      if (mAvailable>0)
               mNotEmpty.Set();
				mAvailableLock.Unlock();
				return true;
			}
			mAvailableLock.Unlock();
			double wait = 0;
			if (inTimeout>=0)
			{
				wait = stop-Now();
				if (wait<=0)
					return false;
			}

			hx::EnterGCFreeZone();
			if (inTimeout<0)
				mNotEmpty.Wait( );
			else
				mNotEmpty.WaitSeconds(wait);
			hx::ExitGCFreeZone();
		}
	}
	void Release()
	{
		AutoLock lock(mAvailableLock);
		mAvailable++;
		mNotEmpty.Set();
	}


	HxSemaphore mNotEmpty;
   HxMutex     mAvailableLock;
	int         mAvailable;
};



Dynamic __hxcpp_lock_create()
{
	return new hxLock;
}
bool __hxcpp_lock_wait(Dynamic inlock,double inTime)
{
	hxLock *lock = dynamic_cast<hxLock *>(inlock.mPtr);
	if (!lock)
		throw HX_INVALID_OBJECT;
	return lock->Wait(inTime);
}
void __hxcpp_lock_release(Dynamic inlock)
{
	hxLock *lock = dynamic_cast<hxLock *>(inlock.mPtr);
	if (!lock)
		throw HX_INVALID_OBJECT;
	lock->Release();
}


int __hxcpp_GetCurrentThreadNumber()
{
	return hx::thread::Thread_obj::id();
}

#endif

// --- Atomic ---

bool _hx_atomic_exchange_if(::cpp::Pointer<cpp::AtomicInt> inPtr, int test, int  newVal )
{
   return _hx_atomic_compare_exchange(inPtr, test, newVal) == test;
}

int _hx_atomic_inc(::cpp::Pointer<cpp::AtomicInt> inPtr )
{
   return _hx_atomic_add(inPtr, 1);
}

int _hx_atomic_dec(::cpp::Pointer<cpp::AtomicInt> inPtr )
{
   return _hx_atomic_sub(inPtr, 1);
}
