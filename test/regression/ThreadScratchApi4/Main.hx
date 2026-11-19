import cpp.vm.Gc;
import sys.thread.Lock;
import sys.thread.Thread;

// Each thread object held a 64 KB scratch buffer, which only Haxe 5's
// hx::thread::Scratch uses. Below API level 500 nothing reads it, yet every
// Haxe thread, and every native thread that asked for its Thread, kept one.
function main() {
	#if (hxcpp_api_level >= 500)
	Sys.println("each running thread holds less than 16 KB: true");
	#else
	var count = 64;
	Gc.run(true);
	var before = Gc.memInfo64(Gc.MEM_INFO_USAGE);
	var started = new Lock();
	var stop = new Lock();
	var stopped = new Lock();
	for (i in 0...count)
		Thread.create(() -> {
			started.release();
			stop.wait();
			stopped.release();
		});
	for (i in 0...count)
		started.wait();
	Gc.run(true);
	var perThread = (Gc.memInfo64(Gc.MEM_INFO_USAGE) - before) / count;
	Sys.println("each running thread holds less than 16 KB: " + (perThread < 16384));
	for (i in 0...count)
		stop.release();
	for (i in 0...count)
		stopped.wait();
	#end
}
