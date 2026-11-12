import cpp.vm.Gc;
import sys.thread.Deque;

// A deque that drained after a burst of a million messages kept the
// storage the burst grew it to, 8 MB on 64-bit, for as long as the deque
// lived (libhxcpp #316). Haxe 5's Deque is the std's, an Array that pop
// shifts, so there this checks what the Array gives back as it drains
class Main {
	static inline var BURST = 1000000;

	static function live():Float {
		Gc.run(true);
		Gc.run(true);
		// MEM_INFO_LIVE: the bytes that survived the last full collection
		return Gc.memInfo64(4);
	}

	static function burst(q:Deque<Dynamic>):Bool {
		for (i in 0...BURST)
			q.add(7);
		for (i in 0...BURST)
			if (q.pop(false) != 7)
				return false;
		return q.pop(false) == null;
	}

	static function main() {
		var q = new Deque<Dynamic>();
		var before = live();
		var drained = burst(q);
		var after = live();

		var reusable = drained;
		q.add(1);
		q.push(0);
		q.add(2);
		for (i in 0...3)
			if (q.pop(false) != i)
				reusable = false;
		if (q.pop(false) != null)
			reusable = false;
		Sys.println('drained queue remains empty and reusable: $reusable');
		Sys.println('drained queue gives back the burst storage: ${after - before < 1024 * 1024}');

		// Bursts that stay small keep their storage
		var small = new Deque<Dynamic>();
		var ok = true;
		for (b in 0...100) {
			for (i in 0...1000)
				small.add(i);
			for (i in 0...1000)
				if (small.pop(false) != i)
					ok = false;
		}
		Sys.println('repeated small bursts keep order: ${ok && small.pop(false) == null}');
	}
}
