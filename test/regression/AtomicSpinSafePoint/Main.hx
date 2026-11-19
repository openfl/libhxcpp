import cpp.vm.Gc;
import haxe.atomic.AtomicInt;
import sys.thread.Thread;

// libhxcpp #455. A thread spins on haxe.atomic.AtomicInt.load(), waiting for a
// flag the main thread sets after a collection. Haxe 4's AtomicInt calls the
// _hx_atomic_* functions, which were not safe points, so the collection waited
// for the spinning thread for ever. A native watchdog, which the collector
// does not know, ends the test if that happens again.
@:cppFileCode('
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstdlib>
static std::atomic<bool> gCollected(false);
static void startWatchdog()
{
   std::thread([]{
      std::this_thread::sleep_for(std::chrono::seconds(60));
      if (!gCollected)
      {
         fputs("no collection after 60 s\\n", stdout);
         fflush(stdout);
         std::_Exit(1);
      }
   }).detach();
}
')
class Main {
	static var flag = new AtomicInt(0);
	static var started = new AtomicInt(0);
	static var spins = new AtomicInt(0);

	static function main() {
		untyped __cpp__("startWatchdog()");
		var done = new sys.thread.Lock();
		Thread.create(() -> {
			started.store(1);
			while (flag.load() == 0)
				spins.add(1);
			done.release();
		});
		while (started.load() == 0)
			Sys.sleep(0.001);
		Sys.sleep(0.1);
		Gc.run(true);
		untyped __cpp__("gCollected = true");
		Sys.println("collected while a thread spun on an AtomicInt");
		flag.store(1);
		done.wait();
		Sys.println("the spinning thread saw the flag");
	}
}
