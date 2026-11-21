import cpp.vm.Gc;
import haxe.atomic.AtomicInt;
import sys.thread.Thread;

// libhxcpp #457.
// - Sys.sleep, Sys.command and Sys.getch entered and left a GC-free zone with
//   plain calls, so called in a zone the caller had entered, as
//   docs/ThreadsAndStacks.md suggests around a blocking call, their exit ended
//   the caller's zone, and the caller's own exit stopped the process with
//   "GCFree Zone mismatch".
// - CFFI's gc_safe_point paused for a collection even in a zone, between
//   gc_enter_blocking and gc_exit_blocking, which ended the process with "Bad
//   Allocation while collecting" or waited for ever.
@:cppFileCode('
extern "C" void gc_enter_blocking();
extern "C" void gc_exit_blocking();
extern "C" void gc_safe_point();
')
class Main {
	static var count = new AtomicInt(0);
	static var stop = new AtomicInt(0);

	static function main() {
		Gc.enterGCFreeZone();
		Sys.sleep(0.01);
		Gc.exitGCFreeZone();
		Sys.println("Sys.sleep in a zone: ok");

		Gc.enterGCFreeZone();
		var code = Sys.command("exit 3");
		Gc.exitGCFreeZone();
		Sys.println("Sys.command in a zone: " + code);

		var done = new sys.thread.Lock();
		Thread.create(() -> {
			while (stop.load() == 0) {
				untyped __cpp__("gc_enter_blocking()");
				untyped __cpp__("gc_safe_point()");
				untyped __cpp__("gc_exit_blocking()");
				count.add(1);
			}
			done.release();
		});
		var stalled = false;
		for (round in 0...100) {
			var junk = [for (i in 0...2000) [i]];
			Gc.run(false);
			var before = count.load();
			Sys.sleep(0.005);
			if (count.load() == before) {
				Sys.sleep(2.0);
				if (count.load() == before) {
					stalled = true;
					break;
				}
			}
		}
		stop.store(1);
		if (!stalled)
			done.wait();
		Sys.println("gc_safe_point in a zone, through 100 collections: " + (stalled ? "stalled" : "ok"));
	}
}
