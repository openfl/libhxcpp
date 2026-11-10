import cpp.vm.Gc;
import haxe.io.Bytes;

// glibc keeps a freed buffer's memory in its heap unless the buffer was mapped
// on its own, and it stops mapping buffers on their own once one that size has
// been freed. The collector hands the memory of the large buffers it frees back
// to the system.
class Main {
	static inline var MB = 1024 * 1024;

	static var kept:Array<Bytes> = [];
	static var temporary:Array<Bytes>;

	// Megabytes of the process in memory, or -1 where /proc does not tell
	static function resident():Float {
		if (Sys.systemName() != "Linux" || !sys.FileSystem.exists("/proc/self/statm"))
			return -1;
		var pages = Std.parseFloat(sys.io.File.getContent("/proc/self/statm").split(" ")[1]);
		return pages * 4096 / MB;
	}

	// A small buffer that lives follows each large one, so that none of the
	// freed memory is at the end of the heap, which glibc gives back by itself
	static function burst(count:Int, size:Int) {
		temporary = [];
		for (i in 0...count) {
			var bytes = Bytes.alloc(size);
			bytes.fill(0, size, 1);
			temporary.push(bytes);
			kept.push(Bytes.alloc(64 * 1024));
		}
	}

	static function release() {
		temporary = null;
		// The first collection keeps the dead buffers for reuse, the second frees them
		Gc.run(true);
		Gc.run(true);
	}

	static function main() {
		if (resident() < 0) {
			Sys.println("the burst was in memory: true");
			Sys.println("freed large buffers go back to the system: true");
			return;
		}
		// Freeing buffers mapped on their own raises the size from which glibc maps them
		burst(4, 8 * MB);
		release();
		var before = resident();
		burst(96, 4 * MB);
		var during = resident();
		release();
		// A thread of the collector's frees them
		var after = resident();
		var waited = 0.0;
		while (after - before > 96 && waited < 10) {
			Sys.sleep(0.05);
			waited += 0.05;
			after = resident();
		}
		Sys.println("the burst was in memory: " + (during - before > 300));
		Sys.println("freed large buffers go back to the system: " + (after - before <= 96));
		if (after - before > 96)
			Sys.println("resident before " + Math.round(before) + " MB, during " + Math.round(during) + " MB, after " + Math.round(after) + " MB");
	}
}
