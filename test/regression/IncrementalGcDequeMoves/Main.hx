import cpp.vm.Gc;
import sys.thread.Deque;
#if HXCPP_GC_INCREMENTAL
import hxcpp.gc.Incremental;
#end

class Node {
	public var value:Int;
	public function new(value:Int) this.value = value;
}

// A Deque is an array that moves its items itself: a pop compacts the queue
// once the consumed part dominates, and a push at the front opens room by
// moving the items up. An incremental cycle scans a long array in chunks, so
// an item moved into a chunk already scanned has to be found some other way.
class Main {
	static inline var COUNT = 32768;
	static var queue:Deque<Node>;

	static function fill() {
		queue = new Deque<Node>();
		for (i in 0...COUNT)
			queue.add(new Node(i));
		Gc.run(true);
	}

	#if HXCPP_GC_INCREMENTAL
	static function queuedChunks():Int {
		return untyped __cpp__("__hxcpp_gc_incremental_test(9, nullptr)");
	}

	// Stops a cycle when markers have taken some of the queue's first batch of
	// chunks and the rest wait, so the second half has not been queued yet
	static function stopPartway() {
		untyped __cpp__("__hxcpp_gc_incremental_test(0, nullptr)");
		var first = 0;
		for (_ in 0...10000) {
			Incremental.step(0.1);
			var queued = queuedChunks();
			if (queued > first)
				first = queued;
			else if (queued > 0 && queued < first)
				break;
		}
		var queued = queuedChunks();
		if (queued == 0 || queued >= first || !Incremental.isActive())
			throw "the queue was not partly scanned";
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

	static function drain(first:Int):Bool {
		var valid = true;
		var expected = first;
		while (true) {
			var node = queue.pop(false);
			if (node == null)
				break;
			if (node.value != expected++)
				valid = false;
		}
		return valid && expected == COUNT;
	}

	static function main() {
		Gc.enable(false);

		// Popping half the queue moves the second half, which no chunk has
		// reached, down over chunks that are done
		fill();
		#if HXCPP_GC_INCREMENTAL
		stopPartway();
		#end
		var valid = true;
		for (i in 0...COUNT >> 1)
			if (queue.pop(false).value != i)
				valid = false;
		#if HXCPP_GC_INCREMENTAL
		finishCycle();
		#end
		Gc.run(true);
		Sys.println("items a pop moved survive: " + (drain(COUNT >> 1) && valid));

		// Pushing at the front moves every item up, past chunks done already
		fill();
		#if HXCPP_GC_INCREMENTAL
		stopPartway();
		#end
		queue.push(new Node(-1));
		#if HXCPP_GC_INCREMENTAL
		finishCycle();
		#end
		Gc.run(true);
		valid = queue.pop(false).value == -1;
		Sys.println("items a push at the front moved survive: " + (drain(0) && valid));

		Gc.enable(true);
	}
}
