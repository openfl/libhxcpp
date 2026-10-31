import haxe.Timer;
import sys.thread.Lock;
import sys.thread.Semaphore;
import sys.thread.Thread;

@:cppFileCode('#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
static void startWatchdog(int seconds)
{
	std::thread([seconds]() {
		std::this_thread::sleep_for(std::chrono::seconds(seconds));
		std::printf("timed out\\n");
		std::fflush(stdout);
		std::_Exit(3);
	}).detach();
}')
class Main {
	static function releaseLater(release:Void->Void) {
		Thread.create(() -> {
			Sys.sleep(0.05);
			release();
		});
	}

	static function check(name:String, ok:Bool) {
		Sys.println('$name ${ok ? "ok" : "failed"}');
	}

	static function attempt(f:Void->Bool):Bool {
		try {
			return f();
		} catch (e:Dynamic) {
			Sys.println('  threw $e');
			return false;
		}
	}

	static function main() {
		untyped __cpp__("startWatchdog(60)");
		var empty = new Semaphore(0);
		var full = new Semaphore(2);

		// A negative timeout waits with no limit, which Haxe 5's Lock.wait() relies on
		releaseLater(() -> empty.release());
		check("semaphore negative", attempt(() -> empty.tryAcquire(-0.5) && full.tryAcquire(-0.5)));
		check("semaphore nan", attempt(() -> !empty.tryAcquire(Math.NaN) && full.tryAcquire(Math.NaN)));

		var huge = true;
		for (timeout in [3e9, 1e10, 4294967.296]) {
			releaseLater(() -> empty.release());
			huge = huge && attempt(() -> empty.tryAcquire(timeout));
		}
		check("semaphore huge", huge);

		var start = Timer.stamp();
		var acquired = attempt(() -> empty.tryAcquire(0.0005));
		check("semaphore submillisecond", !acquired && Timer.stamp() - start >= 0.0005);

		var lock = new Lock();
		releaseLater(() -> lock.release());
		check("lock no timeout", lock.wait());

		releaseLater(() -> lock.release());
		check("lock huge", lock.wait(3e9));

		start = Timer.stamp();
		check("lock submillisecond", !lock.wait(0.0005) && Timer.stamp() - start >= 0.0005);
	}
}
