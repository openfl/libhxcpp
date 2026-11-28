import sys.thread.Lock;
import sys.thread.Thread;

// Threads where the platform cannot detach one. devkitA64's newlib on Switch
// has no pthread_detach hook, so it returns ENOSYS, std::thread::detach
// throws, and the joinable std::thread being destroyed ended the process
// with std::terminate at the first Thread.create, or when the collector
// started a worker. On glibc this program replaces pthread_detach with one
// that does the same. Elsewhere it cannot, and prints what is expected.
@:cppFileCode('
#if defined(__linux__)
#include <pthread.h>
#include <errno.h>
#endif
#include <atomic>

#if defined(__linux__) && defined(__GLIBC__)
static std::atomic<int> refusedDetaches(0);

extern "C" __attribute__((visibility("default"))) int pthread_detach(pthread_t) __THROW
{
	refusedDetaches++;
	return ENOSYS;
}

static bool detachRefused() { return refusedDetaches > 0; }
#else
static bool detachRefused() { return true; }
#endif
')
class Main {
	static function main() {
		final count = 32;
		final done = new Lock();
		var ran = 0;
		final mutex = new sys.thread.Mutex();
		for (i in 0...count) {
			Thread.create(() -> {
				// Allocates enough for collections to run while threads come and go
				var keep = [];
				for (j in 0...2000) {
					keep.push([j, i]);
				}
				cpp.vm.Gc.run(false);
				mutex.acquire();
				ran++;
				mutex.release();
				done.release();
			});
		}
		var all = true;
		for (i in 0...count) {
			if (!done.wait(30)) {
				all = false;
				break;
			}
		}
		cpp.vm.Gc.run(true);
		final refused:Bool = untyped __cpp__("detachRefused()");
		Sys.println("threads created after a refused detach run to their end: " + (all && ran == count));
		Sys.println("the platform refused to detach them: " + refused);
	}
}
