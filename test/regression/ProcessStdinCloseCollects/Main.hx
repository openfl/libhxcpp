import cpp.vm.Gc;
import haxe.io.Bytes;
import sys.io.Process;
import sys.thread.Lock;
import sys.thread.Thread;

// libhxcpp #454. One thread is blocked writing to a child's stdin, which the
// child never reads, and another closes that stdin. On Windows, CloseHandle
// waits for the WriteFile in progress, and stdin.close called it outside a
// GC-free zone, so a collection started meanwhile, and the close itself,
// waited until the child exited. Process.close cancelled pending pipe I/O
// first (#261); stdin.close now does too.
class Main {
	static function main() {
		if (Sys.args().length > 0) {
			Sys.sleep(Std.parseFloat(Sys.args()[0]));
			return;
		}
		// Elsewhere close does not wait for a write in progress, and closing a
		// descriptor another thread is writing to is a race of the test's own
		if (Sys.systemName() != "Windows") {
			Sys.println("collected while the child ran: true");
			Sys.println("stdin closed while the child ran: true");
			return;
		}
		var p = new Process(Sys.programPath(), ["30"]);
		var writerDone = new Lock();
		Thread.create(() -> {
			try {
				p.stdin.write(Bytes.alloc(1 << 22));
			} catch (e:Dynamic) {}
			writerDone.release();
		});
		Sys.sleep(0.5);
		var closed = new Lock();
		var closedWhileRunning = false;
		Thread.create(() -> {
			p.stdin.close();
			closedWhileRunning = p.exitCode(false) == null;
			closed.release();
		});
		Sys.sleep(0.5);
		Gc.run(true);
		var collectedWhileRunning = p.exitCode(false) == null;
		closed.wait();
		Sys.println("collected while the child ran: " + collectedWhileRunning);
		Sys.println("stdin closed while the child ran: " + closedWhileRunning);
		p.kill();
		writerDone.wait();
		p.close();
	}
}
