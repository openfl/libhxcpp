import cpp.vm.Gc;
import sys.thread.Lock;
import sys.thread.Thread;

// Haxe 5.0.0-preview.1's haxe.atomic.AtomicInt keeps its value in an Int field
// and passes the field's address, a plain int *, to the _hx_atomic_*
// functions. That chose the runtime's volatile int * forms, which never stop
// for a collection, so a thread spinning on one held up every collection, as
// Haxe 4's AtomicInt did before libhxcpp #455. This spins on each operation in
// turn, called as preview.1 calls them, while the main thread collects. Only
// the operation under test can stop the thread: it watches a native flag. A
// native watchdog, which the collector does not know, ends the test if a
// collection never finishes.
class Data {
	public var value:Int;

	public function new(value:Int)
		this.value = value;
}

@:cppFileCode('
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstdlib>
static std::atomic<bool> gStop(false);
static std::atomic<bool> gSpinning(false);
static std::atomic<int> gOperation(-1);
static std::atomic<bool> gFinished(false);
static const char *gNames[] = { "add", "sub", "and", "or", "xor", "compare_exchange", "exchange", "load", "store" };
static void startWatchdog()
{
	std::thread([]{
		std::this_thread::sleep_for(std::chrono::seconds(60));
		if (!gFinished)
		{
			int op = gOperation;
			printf("no collection after 60 s while a thread spun on _hx_atomic_%s\\n", op>=0 ? gNames[op] : "?");
			fflush(stdout);
			std::_Exit(1);
		}
	}).detach();
}
')
class Main {
	static final names = ["add", "sub", "and", "or", "xor", "compare_exchange", "exchange", "load", "store"];

	static function apply(op:Int, data:Data):Int {
		return switch op {
			case 0: untyped __cpp__("_hx_atomic_add(&{0}, 1)", data.value);
			case 1: untyped __cpp__("_hx_atomic_sub(&{0}, 1)", data.value);
			case 2: untyped __cpp__("_hx_atomic_and(&{0}, 0xff)", data.value);
			case 3: untyped __cpp__("_hx_atomic_or(&{0}, 0x100)", data.value);
			case 4: untyped __cpp__("_hx_atomic_xor(&{0}, 1)", data.value);
			case 5: untyped __cpp__("_hx_atomic_compare_exchange(&{0}, 0, 1)", data.value);
			case 6: untyped __cpp__("_hx_atomic_exchange(&{0}, 2)", data.value);
			case 7: untyped __cpp__("_hx_atomic_load(&{0})", data.value);
			case _: untyped __cpp__("_hx_atomic_store(&{0}, 3)", data.value);
		}
	}

	static function main() {
		untyped __cpp__("startWatchdog()");
		final data = new Data(0);
		for (op in 0...names.length) {
			untyped __cpp__("gOperation = {0}", op);
			untyped __cpp__("gStop = false");
			untyped __cpp__("gSpinning = false");
			final done = new Lock();
			Thread.create(() -> {
				untyped __cpp__("gSpinning = true");
				while (untyped __cpp__("!gStop"))
					apply(op, data);
				done.release();
			});
			while (untyped __cpp__("!gSpinning"))
				Sys.sleep(0.001);
			Sys.sleep(0.02);
			Gc.run(true);
			untyped __cpp__("gStop = true");
			done.wait();
			Sys.println('collected while a thread spun on _hx_atomic_${names[op]}(&field)');
		}
		untyped __cpp__("gFinished = true");
	}
}
