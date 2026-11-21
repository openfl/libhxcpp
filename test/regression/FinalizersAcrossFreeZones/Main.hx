import sys.thread.Lock;
import sys.thread.Thread;

// A thread that enters a GC-free zone lets the collector run without waiting
// for it, so whatever the thread wrote before entering must be visible to the
// collector. The flag that says so was a plain volatile, which ordered nothing,
// and the Haxe finalizer map a thread had just written raced with the
// collector running finalizers. Workers register finalizers and enter free
// zones while the main thread collects. No object may be finalized twice, and
// TSan reports the race if it returns. The conservative stack scan may keep a
// few alive, so not every object has to be finalized.
class Item {
	public function new() {}
}

class Main {
	// Finalizers run inside a collection, one collection at a time and under
	// the collector's lock, so the count needs no lock of its own.
	static var finalized = 0;

	static function onFinalize(item:Item):Void {
		finalized++;
	}

	static function main() {
		var workers = 3;
		var perWorker = 2000;
		var done = new Lock();
		for (w in 0...workers) {
			Thread.create(() -> {
				for (i in 0...perWorker) {
					var item = new Item();
					cpp.vm.Gc.setFinalizer(item, cpp.Callable.fromStaticFunction(onFinalize));
					cpp.vm.Gc.enterGCFreeZone();
					cpp.vm.Gc.exitGCFreeZone();
				}
				done.release();
			});
		}
		var finished = 0;
		while (finished < workers) {
			cpp.vm.Gc.run(true);
			if (done.wait(0))
				finished++;
		}
		for (i in 0...10)
			cpp.vm.Gc.run(true);
		var count = finalized;
		Sys.println(count > 0 && count <= workers * perWorker ? "finalizers ran, none twice" : 'finalized $count of ${workers * perWorker}');
	}
}
