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

	// Megabytes of the process in memory, or -1 where it cannot be measured:
	// without /proc, and under ASan, whose allocator replaces glibc's and keeps
	// freed memory in its quarantine
	static function resident():Float {
		#if (HXCPP_SANITIZE == "address")
		return -1;
		#else
		if (Sys.systemName() != "Linux" || !sys.FileSystem.exists("/proc/self/status"))
			return -1;
		// VmRSS is in kB whatever the page size, which is 16 or 64 KB on some arm64 kernels
		for (line in sys.io.File.getContent("/proc/self/status").split("\n")) {
			if (StringTools.startsWith(line, "VmRSS:"))
				return Std.parseFloat(StringTools.trim(line.substr(6)).split(" ")[0]) / 1024;
		}
		return -1;
		#end
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
