import sys.thread.Thread;

// Threads let go after the static destructors, as the process exits. A thread
// that ended then used to run into the runtime's statics after their
// destructors: the root set, and with stack traces or the debugger the stack
// map and the debugger's lists (ThreadEndsAtExitDebugger builds this program
// that way). On Windows the fault came inside the loader lock, so the exit
// never finished. Since #423 the exit parks every other thread that runs Haxe
// code, so a thread let go then leaves its GC-free zone into that park and
// runs no further, and the process still ends cleanly.
//
// The threads are let go from a function the C runtime calls after the static
// destructors (a .CRT$XPU entry with MSVC, a destructor attribute elsewhere),
// and each marks when it gets past the zone. A moment later the function
// reports how many did.
@:cppFileCode('
#include <stdio.h>
#ifdef HX_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
	const int WORKERS = 2;
	volatile int sRegistered = 0;
	volatile int sGo[WORKERS] = { 0, 0 };
	volatile int sRan[WORKERS] = { 0, 0 };

	// The flags are set by one thread and polled by another, so they are read
	// and written atomically
	void setFlag(volatile int *ioFlag)
	{
		#ifdef HX_WINDOWS
		InterlockedExchange((volatile LONG *)ioFlag, 1);
		#else
		__sync_lock_test_and_set(ioFlag, 1);
		__sync_synchronize();
		#endif
	}

	int readFlag(volatile int *inFlag)
	{
		#ifdef HX_WINDOWS
		return InterlockedCompareExchange((volatile LONG *)inFlag, 0, 0);
		#else
		return __sync_fetch_and_add(inFlag, 0);
		#endif
	}

	void registerWorker()
	{
		#ifdef HX_WINDOWS
		InterlockedIncrement((volatile LONG *)&sRegistered);
		#else
		__sync_fetch_and_add(&sRegistered, 1);
		#endif
	}

	int registeredWorkers() { return readFlag(&sRegistered); }

	void pause(int inMs)
	{
		#ifdef HX_WINDOWS
		Sleep(inMs);
		#else
		usleep(inMs * 1000);
		#endif
	}

	void waitToEnd(int inIndex)
	{
		while (!readFlag(&sGo[inIndex]))
			pause(1);
	}

	void ranOn(int inIndex) { setFlag(&sRan[inIndex]); }

	void endThreadsLate()
	{
		for (int i = 0; i < WORKERS; i++)
			setFlag(&sGo[i]);
		pause(200);
		int ran = 0;
		for (int i = 0; i < WORKERS; i++)
			ran += readFlag(&sRan[i]);
		printf("%d of %d threads ran on after the static destructors\\n", ran, WORKERS);
		fflush(stdout);
	}

	#ifdef _MSC_VER
	typedef void (__cdecl *LateExitFunc)(void);
	#pragma section(".CRT$XPU", long, read)
	__declspec(allocate(".CRT$XPU")) LateExitFunc sLateExit = endThreadsLate;
	// Referred to from main, so nothing discards the entry
	bool lateExitInstalled() { return sLateExit != 0; }
	#else
	__attribute__((destructor)) void lateExit() { endThreadsLate(); }
	bool lateExitInstalled() { return true; }
	#endif
}
')
class Main {
	static final WORKERS = 2;

	static function main() {
		for (i in 0...WORKERS) {
			Thread.create(() -> {
				untyped __cpp__("registerWorker()");
				cpp.vm.Gc.enterGCFreeZone();
				untyped __cpp__("waitToEnd({0})", i);
				cpp.vm.Gc.exitGCFreeZone();
				untyped __cpp__("ranOn({0})", i);
			});
		}
		var registered:Int = 0;
		while (registered < WORKERS) {
			Sys.sleep(0.001);
			registered = untyped __cpp__("registeredWorkers()");
		}
		var installed:Bool = untyped __cpp__("lateExitInstalled()");
		Sys.println(installed ? "main returns" : "no late exit");
	}
}
