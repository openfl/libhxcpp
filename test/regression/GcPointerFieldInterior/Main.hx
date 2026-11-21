// libhxcpp #451. A cpp.Pointer field keeps what it points into, but only kept
// it within 64 bytes of the allocation's start (32 on 32-bit), the limit for a
// stack word. A pointer to element 100 of an Int array, 400 bytes in, or into
// the middle of a large array, kept nothing, and the array was freed under it.
class Holder {
	public var near:cpp.Pointer<Int>;
	public var far:cpp.Pointer<Int>;

	public function new() {}
}

class Main {
	static inline var COUNT = 2000;

	static var holders:Array<Holder>;
	static var large:cpp.Pointer<Int>;

	static function fill() {
		for (i in 0...COUNT) {
			var a = [for (j in 0...500) i * 1000 + j];
			holders[i].near = cpp.Pointer.arrayElem(a, 10);
			var b = [for (j in 0...500) i * 1000 + j];
			holders[i].far = cpp.Pointer.arrayElem(b, 100);
		}
		// 400 KB, a large allocation, pointed to 200 KB in
		var big = [for (j in 0...100000) j];
		large = cpp.Pointer.arrayElem(big, 50000);
	}

	static function churn() {
		var junk = [];
		for (i in 0...20000)
			junk.push([for (j in 0...100) 0x5a5a5a5a]);
		var bigJunk = [for (i in 0...20) [for (j in 0...100000) 0x5a5a5a5a]];
		return junk.length + bigJunk.length;
	}

	static function main() {
		holders = [for (i in 0...COUNT) new Holder()];
		fill();
		for (round in 0...4) {
			cpp.vm.Gc.run(true);
			churn();
		}
		cpp.vm.Gc.run(true);
		var wrongNear = 0;
		var wrongFar = 0;
		for (i in 0...COUNT) {
			if (holders[i].near.value != i * 1000 + 10)
				wrongNear++;
			if (holders[i].far.value != i * 1000 + 100)
				wrongFar++;
		}
		Sys.println('pointers 40 bytes in, wrong: $wrongNear of $COUNT');
		Sys.println('pointers 400 bytes in, wrong: $wrongFar of $COUNT');
		Sys.println('pointer 200 KB into a large array: ' + (large.value == 50000 ? "right" : "wrong"));
	}
}
