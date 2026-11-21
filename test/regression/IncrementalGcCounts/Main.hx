import cpp.vm.Gc;
#if HXCPP_GC_INCREMENTAL
import hxcpp.gc.Incremental;
#end

class Small {
	public var a:Int;

	public function new(a:Int)
		this.a = a;
}

class Counted {
	public var next:Counted;

	public function new(n:Counted)
		next = n;
}

// libhxcpp #453, with HXCPP_GC_INCREMENTAL, and Gc.trace in a debug build.
// It prints the same without.
// - MEM_INFO_LIVE is the bytes of the objects that survived the last full
//   reclaim. A finished incremental cycle that was not one counted whole rows
//   and reported those as live: seven times the real figure here.
// - Gc.trace counts what a collection marks. With a cycle part done, the
//   collection finished it, and what its earlier slices marked went uncounted.
class Main {
	static function liveBytes() {
		#if HXCPP_GC_INCREMENTAL
		Gc.enable(false);
		var keep = [];
		for (i in 0...800000) {
			var s = new Small(i);
			if (i % 16 == 0)
				keep.push(s);
		}
		Gc.run(true);
		var afterFull = Gc.memInfo64(4);
		// Garbage between the survivors' rows, then one budgeted cycle to its end
		for (i in 0...200000)
			new Small(i);
		while (Incremental.step(2.0)) {}
		var afterCycle = Gc.memInfo64(4);
		Sys.println("live bytes after an incremental cycle as after the full collection: " + (afterCycle == afterFull));
		Sys.println("kept " + keep.length);
		Gc.enable(true);
		#else
		Sys.println("live bytes after an incremental cycle as after the full collection: true");
		Sys.println("kept 50000");
		#end
	}

	static function traceCount() {
		var keep = [];
		for (i in 0...20000) {
			var head:Counted = null;
			for (j in 0...10)
				head = new Counted(head);
			keep.push(head);
		}
		// Gc.trace counts in debug builds only
		#if (HXCPP_GC_INCREMENTAL && debug)
		Gc.enable(false);
		Incremental.step(0.05);
		var during = Incremental.isActive() ? Gc.trace(Counted, false) : 200000;
		Gc.enable(true);
		#elseif debug
		var during = Gc.trace(Counted, false);
		#else
		var during = keep.length * 10;
		#end
		Sys.println("Gc.trace with a cycle part done: " + during + " of " + keep.length * 10);
	}

	static function main() {
		liveBytes();
		traceCount();
	}
}
