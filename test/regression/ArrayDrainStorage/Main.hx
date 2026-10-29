import cpp.vm.Gc;

// An array that drained after a burst kept the storage the burst grew it
// to for as long as it lived: Haxe 5's sys.thread.Deque is an Array that
// pop shifts, so a thread mailbox that once held a million messages held
// 8 MB, as an array used as a queue or a stack did under any Haxe
class Main {
	static function live():Float {
		Gc.run(true);
		Gc.run(true);
		// MEM_INFO_LIVE: the bytes that survived the last full collection
		return Gc.memInfo64(4);
	}

	static function check(name:String, count:Int, drain:Array<Float>->Bool) {
		var a:Array<Float> = [];
		var before = live();
		for (i in 0...count)
			a.push(i);
		var drained = drain(a) && a.length == 0;
		var after = live();
		// The storage is 8 bytes an item, and what is kept is far less
		Sys.println('$name gives back the burst storage: ${after - before < count * 4}');

		// What is kept past the length is empty, and the array grows again
		a.resize(16000);
		var empty = true;
		for (i in 0...a.length)
			if (a[i] != 0)
				empty = false;
		a.resize(0);
		for (i in 0...count)
			a.push(i);
		var refilled = a.length == count && a[0] == 0 && a[count - 1] == count - 1;
		Sys.println('$name leaves it empty and reusable: ${drained && empty && refilled}');
	}

	static function main() {
		check("draining by pop", 1000000, a -> {
			var ok = true;
			var i = a.length;
			while (a.length > 0)
				if (a.pop() != --i)
					ok = false;
			ok;
		});
		// shift moves the rest of the array, so a long queue takes a while
		check("draining by shift", 100000, a -> {
			var ok = true;
			var i = 0;
			while (a.length > 0)
				if (a.shift() != i++)
					ok = false;
			ok;
		});
		check("draining by splice", 1000000, a -> {
			var ok = true;
			var i = 0;
			while (a.length > 0)
				for (v in a.splice(0, 1000))
					if (v != i++)
						ok = false;
			ok;
		});
		check("draining by remove", 100000, a -> {
			var ok = true;
			while (a.length > 0)
				if (!a.remove(a[0]))
					ok = false;
			ok;
		});

		// A large array that shrinks and grows again keeps its items
		var a:Array<Float> = [for (i in 0...100000) i];
		var ok = true;
		for (round in 0...10) {
			for (i in 0...80000)
				if (a.pop() != 99999 - i)
					ok = false;
			for (i in 0...80000)
				a.push(20000 + i);
		}
		for (i in 0...a.length)
			if (a[i] != i)
				ok = false;
		Sys.println('a large array swinging in size keeps its items: ${ok && a.length == 100000}');
	}
}
