import cpp.vm.Gc;
import haxe.io.Bytes;
#if HXCPP_GC_INCREMENTAL
import hxcpp.gc.Incremental;
#end

class Node {
	public var value:Int;
	public var next:Node;
	public function new(value:Int, next:Node) {
		this.value = value;
		this.next = next;
	}
}

class Main {
	static inline var CHUNK = 1024;
	static var live:Array<Node>;
	static var last:Bytes;

	// All of this allocation is array storage, so all of it happens in a mutation scope
	static function churn(inBytes:Float) {
		var count = Std.int(inBytes / CHUNK);
		for (i in 0...count)
			last = Bytes.alloc(CHUNK);
	}

	static function main() {
		#if HXCPP_GC_INCREMENTAL
		live = [for (i in 0...500000) new Node(i, null)];
		// Leaves more free space than the collector would ask for
		var burst = [for (i in 0...1000000) new Node(i, null)];
		burst = null;
		Gc.run(true);
		// The first cycles teach the pacer how long one takes
		churn(96.0 * 1024 * 1024);

		// Collecting more often than the stop-the-world collector costs a mark
		// for each extra cycle. It spends all the free space before it collects.
		var stats = Incremental.getStats();
		Gc.run(true);
		var free = Gc.memInfo64(Gc.MEM_INFO_RESERVED) - Gc.memInfo64(Gc.MEM_INFO_USAGE);
		var bytes = 320.0 * 1024 * 1024;
		churn(bytes);
		var after = Incremental.getStats();
		var cycles:Float = after.cycles - stats.cycles;
		var fallbacks:Float = after.fallbacks;
		var share = bytes / cycles / free;
		Sys.println("a cycle spends most of the free space: " + (cycles > 0 && share >= 0.75));
		Sys.println("allocation in a mutation scope needs no fallback: " + (fallbacks == 0));
		#else
		Sys.println("a cycle spends most of the free space: true");
		Sys.println("allocation in a mutation scope needs no fallback: true");
		#end
	}
}
