import haxe.atomic.AtomicInt;
import sys.thread.Thread;

// haxe.atomic.AtomicInt: Haxe 4 and 5.0.0-preview.1 build it on the runtime's
// atomic intrinsics, later Haxe 5 on hx::atomic::AtomicInt. test/native checks
// AtomicObject, which C++ only has from that later Haxe 5 on
class Main {
	static function main() {
		final a = new AtomicInt(1);
		Sys.println([a.add(2), a.sub(1), a.and(6), a.or(1), a.xor(4), a.load()]);
		// A failed exchange returns the value it found, not the one expected
		Sys.println([a.compareExchange(5, 9), a.load(), a.compareExchange(7, 9), a.load()]);
		Sys.println([a.exchange(11), a.store(12), a.load()]);

		final counter = new AtomicInt(0);
		final finished = new AtomicInt(0);
		for (t in 0...4) {
			Thread.create(() -> {
				for (i in 0...20000)
					counter.add(1);
				finished.add(1);
			});
		}
		while (finished.load() < 4)
			Sys.sleep(0.001);
		Sys.println(counter.load());
	}
}
