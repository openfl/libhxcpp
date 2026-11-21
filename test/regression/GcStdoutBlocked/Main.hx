import cpp.vm.Gc;
import sys.io.Process;
import sys.thread.Lock;
import sys.thread.Thread;

// libhxcpp #454. A child process whose stdout nobody reads: one of its threads
// prints until the pipe is full and the write blocks, then its main thread
// collects. trace and Sys.println wrote outside a GC-free zone, so the
// collection waited for the blocked writer, and every other thread with it.
// Sys.stdout().writeString blocked inside one and was fine.
class Main {
	static function main() {
		var args = Sys.args();
		if (args.length == 0)
			parent();
		else
			child(args[0]);
	}

	static function parent() {
		for (mode in ["write", "println", "trace"]) {
			var p = new Process(Sys.programPath(), [mode]);
			var got = new Lock();
			var line = "(nothing)";
			Thread.create(() -> {
				try {
					line = p.stderr.readLine();
				} catch (e:Dynamic) {}
				got.release();
			});
			if (got.wait(20.0))
				Sys.println('$mode: child said $line');
			else
				Sys.println('$mode: no collection after 20 s');
			p.kill();
			p.close();
		}
	}

	static function child(mode:String) {
		var text = StringTools.lpad("", "x", 1000);
		Thread.create(() -> {
			while (true) {
				switch (mode) {
					case "trace": trace(text);
					case "println": Sys.println(text);
					default: Sys.stdout().writeString(text + "\n");
				}
			}
		});
		// Long enough for the pipe to fill
		Sys.sleep(1.0);
		Gc.run(true);
		Sys.stderr().writeString('collected\n');
		Sys.stderr().flush();
		Sys.sleep(60);
	}
}
