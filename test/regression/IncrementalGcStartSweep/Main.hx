import cpp.vm.Gc;
#if HXCPP_GC_INCREMENTAL
import hxcpp.gc.Incremental;
#end

class Node {
	public var value:Int;
	public function new(value:Int) this.value = value;
}

class Main {
	static var kept:Array<Node>;

	// One object in eight stays, so its rows stay marked and the dead ones beside
	// it keep their start bits until something clears them. The holes left are
	// mostly too small to use, so the free space is far less than the free rows.
	static function churn(inObjects:Int) {
		for (i in 0...inObjects) {
			var node = new Node(i);
			if ((i & 7) == 0)
				kept[(i >> 3) % kept.length] = node;
		}
	}

	static function main() {
		#if HXCPP_GC_INCREMENTAL
		kept = [for (i in 0...200000) new Node(i)];
		Gc.run(true);
		churn(4000000);

		var fullReclaims:Int = untyped __cpp__("__hxcpp_gc_incremental_test(11, nullptr)");
		var start:Float = Incremental.getStats().cycles;
		var oldest = 0;
		var cycles = 0.0;
		// Past two turns of the 16 mark ids
		for (_ in 0...5000) {
			churn(2000000);
			var age:Int = untyped __cpp__("__hxcpp_gc_incremental_test(10, nullptr)");
			if (age > oldest)
				oldest = age;
			var now:Float = Incremental.getStats().cycles;
			cycles = now - start;
			if (cycles >= 40)
				break;
		}
		var after:Int = untyped __cpp__("__hxcpp_gc_incremental_test(11, nullptr)");
		var valid = cycles >= 40;
		for (i in 0...kept.length)
			if (kept[i] == null)
				valid = false;
		Sys.println("start bits go before their mark id returns: " + (oldest <= 14 && valid));
		Sys.println("steady allocation needs no full reclaim: " + (after == fullReclaims));
		#else
		Sys.println("start bits go before their mark id returns: true");
		Sys.println("steady allocation needs no full reclaim: true");
		#end
	}
}
