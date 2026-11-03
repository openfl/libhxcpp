import cpp.vm.Gc;
#if HXCPP_GC_INCREMENTAL
import hxcpp.gc.Incremental;
#end

class Node {
	public var value:Int;
	public function new(value:Int) this.value = value;
}

class Main {
	static inline var COUNT = 262144;

	static function fill():Array<Node> {
		var values = [for (i in 0...COUNT) new Node(i)];
		Gc.run(true);
		return values;
	}

	#if HXCPP_GC_INCREMENTAL
	static function queuedChunks():Int {
		return untyped __cpp__("__hxcpp_gc_incremental_test(9, nullptr)");
	}

	// Stops a cycle with chunks of the array still waiting for a marker
	static function stopWithChunksQueued() {
		untyped __cpp__("__hxcpp_gc_incremental_test(0, nullptr)");
		for (_ in 0...10000) {
			Incremental.step(0.1);
			if (queuedChunks() > 0)
				break;
		}
		if (queuedChunks() == 0 || !Incremental.isActive())
			throw "no chunks were left queued";
		untyped __cpp__("__hxcpp_gc_incremental_test(4, nullptr)");
	}

	static function finishCycle() {
		var unfinished = true;
		for (_ in 0...10000) {
			unfinished = Incremental.step(0.1);
			if (!unfinished)
				break;
		}
		if (unfinished)
			throw "cycle did not finish";
	}
	#end

	static function main() {
		Gc.enable(false);

		// Chunks past the new end must find nothing, and the rest must find the
		// storage the array has now, not the one it had when they were queued
		var values = fill();
		#if HXCPP_GC_INCREMENTAL
		stopWithChunksQueued();
		#end
		values.splice(COUNT >> 2, COUNT >> 1);
		for (i in 0...COUNT)
			values.push(new Node(COUNT + i));
		#if HXCPP_GC_INCREMENTAL
		finishCycle();
		#else
		Gc.run(true);
		#end
		var valid = values.length == COUNT + (COUNT >> 1);
		for (i in 0...COUNT >> 2)
			if (values[i].value != i)
				valid = false;
		for (i in COUNT >> 2...COUNT >> 1)
			if (values[i].value != i + (COUNT >> 1))
				valid = false;
		for (i in 0...COUNT)
			if (values[(COUNT >> 1) + i].value != COUNT + i)
				valid = false;
		Sys.println("chunks read the array's current storage: " + valid);

		// A cycle given up with chunks queued must leave none for the next
		values = fill();
		#if HXCPP_GC_INCREMENTAL
		stopWithChunksQueued();
		untyped __cpp__("__hxcpp_gc_incremental_test(3, nullptr)");
		Incremental.step(0.1);
		#end
		Gc.run(true);
		valid = values.length == COUNT;
		for (i in 0...COUNT)
			if (values[i].value != i)
				valid = false;
		#if HXCPP_GC_INCREMENTAL
		if (queuedChunks() != 0)
			valid = false;
		#end
		Sys.println("a failed cycle drops its queued chunks: " + valid);
		Gc.enable(true);
	}
}
