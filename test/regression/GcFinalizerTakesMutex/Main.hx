import sys.thread.Mutex;

class Resource {
	public function new() {}
}

// libhxcpp #452. A finalizer runs on the collecting thread, inside the
// collection. Mutex stops at a pending collection on its uncontended paths,
// and that stop took the collecting thread for one that had to wait, ending
// the process with "Bad Allocation while collecting". Stock hxcpp took the
// lock in a GC-free zone, which the collecting thread can enter, so a
// finalizer could take a Mutex. It still must not allocate.
class Main {
	static var lock = new Mutex();
	static var counted = 0;
	static var tried = 0;

	static function onFinalize(r:Resource):Void {
		lock.acquire();
		counted++;
		lock.release();
		if (lock.tryAcquire()) {
			tried++;
			lock.release();
		}
	}

	static function make() {
		for (i in 0...1000) {
			var r = new Resource();
			cpp.vm.Gc.setFinalizer(r, cpp.Callable.fromStaticFunction(onFinalize));
		}
	}

	static function main() {
		make();
		cpp.vm.Gc.run(true);
		cpp.vm.Gc.run(true);
		// The conservative stack scan may keep a few alive
		Sys.println("finalizers took the mutex: " + (counted > 900));
		Sys.println("finalizers tried it: " + (tried == counted));
	}
}
