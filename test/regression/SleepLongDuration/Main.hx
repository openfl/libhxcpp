import sys.thread.Lock;
import sys.thread.Thread;

// Sys.sleep returned at once for a duration that did not fit the native call:
// on Windows from 2^32 milliseconds, about 49.7 days, which wrapped to a short
// sleep, and on POSIX past 2^31 seconds. Infinity, a way to sleep for good,
// was one of them. Each sleep runs in a thread, which is still asleep later.
function main() {
	for (seconds in [4294967.3, 3e9, Math.POSITIVE_INFINITY]) {
		var woke = new Lock();
		Thread.create(() -> {
			Sys.sleep(seconds);
			woke.release();
		});
		Sys.println('Sys.sleep($seconds) still sleeping after 0.3 s: ${!woke.wait(0.3)}');
	}
	var t0 = haxe.Timer.stamp();
	Sys.sleep(0.05);
	Sys.println("Sys.sleep(0.05) took at least 0.05 s: " + (haxe.Timer.stamp() - t0 >= 0.049));
	Sys.sleep(0);
	Sys.sleep(-1);
	Sys.sleep(Math.NaN);
	Sys.println("zero, negative and NaN return");
}
