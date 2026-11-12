import sys.thread.Deque;
import sys.thread.Thread;

// HXCPP_PTHREAD_FAST_TLS keeps the collector's thread context in
// pthread_getspecific on Linux, where it is otherwise a __thread pointer.
// Every file has to agree on which, so the define has to reach the compiler
// for the runtime, the std libraries and the generated code alike: before it
// was passed on, it did not reach any of them. Threads then allocate and
// collect through the context.
@:cppFileCode('
static bool defineReachesCompiler()
{
#ifdef HXCPP_PTHREAD_FAST_TLS
	return true;
#else
	return false;
#endif
}
')
class Main {
	static function main() {
		Sys.println("define reaches the compiler: " + (untyped __cpp__("defineReachesCompiler()") : Bool));

		var done = new Deque<Int>();
		for (t in 0...4) {
			Thread.create(() -> {
				var total = 0;
				for (i in 0...20000) {
					total += (Std.string(i) + "x").length;
					if (i % 5000 == 0)
						cpp.vm.Gc.run(false);
				}
				done.add(total);
			});
		}
		var totals = [for (t in 0...4) done.pop(true)];
		Sys.println("each thread's total: " + totals.join(","));
	}
}
