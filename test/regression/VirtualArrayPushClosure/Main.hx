import cpp.vm.Gc;
import cpp.vm.WeakRef;

class Main {
	static final weak:Array<WeakRef<Array<Int>>> = [];

	// An Array<T> of a type parameter is a cpp.VirtualArray, and its push as a
	// value is the closure that VirtualArray.push_dyn makes
	static function pusher<T>(a:Array<T>):T->Int {
		return a.push;
	}

	static function make():Int->Int {
		var a = [1, 2, 3];
		weak.push(new WeakRef(a));
		return pusher(a);
	}

	static function churn() {
		var keep = [];
		for (i in 0...20000)
			keep.push([i, i + 1, i + 2]);
		return keep.length;
	}

	static function main() {
		// Many of them, so a stale register holding one array proves nothing
		var pushes = [for (i in 0...50) make()];
		var ok = true;
		for (round in 0...5) {
			Gc.run(true);
			churn();
			for (i in 0...pushes.length)
				ok = ok && weak[i].get() != null && pushes[i](round) == 4 + round;
		}
		Sys.println('push closures keep their arrays: $ok');
	}
}
