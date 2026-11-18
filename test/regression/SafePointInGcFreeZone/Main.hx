import cpp.vm.Gc;
import sys.thread.Condition;
import sys.thread.Lock;
import sys.thread.Mutex;
import sys.thread.Thread;

// Mutex and Condition take their locks inside a GC-free zone the caller already
// entered, and their uncontended fast paths stop at a pending collection. That
// stop paused the thread as if it were outside a zone: during a collection the
// process ended with "Bad Allocation while collecting", and otherwise the thread
// waited for a release the collector never sends to a thread in a zone. A
// thread in a zone is already safe, so the stop now does nothing there.
class Main {
	static var count = 0;
	static var stop = false;

	static function main() {
		var finished = new Lock();
		var mutex = new Mutex();
		var condition = new Condition();
		Thread.create(() -> {
			while (!stop) {
				Gc.enterGCFreeZone();
				mutex.acquire();
				mutex.release();
				if (mutex.tryAcquire())
					mutex.release();
				condition.acquire();
				condition.release();
				if (condition.tryAcquire())
					condition.release();
				Gc.exitGCFreeZone();
				count++;
			}
			finished.release();
		});
		var stalled = false;
		for (round in 0...200) {
			var junk = [];
			for (i in 0...2000)
				junk.push([i]);
			Gc.run(false);
			var before = count;
			Sys.sleep(0.005);
			if (count == before) {
				Sys.sleep(2.0);
				if (count == before) {
					stalled = true;
					break;
				}
			}
		}
		stop = true;
		if (stalled) {
			Sys.println("the thread in the GC-free zone stopped");
			Sys.exit(1);
		}
		finished.wait();
		Sys.println("the thread in the GC-free zone ran through 200 collections");
	}
}
