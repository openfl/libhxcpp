import haxe.atomic.AtomicInt;

function main() {
	// Scripts reach haxe.atomic.AtomicInt through the host's copy of the abstract
	new AtomicInt(0).add(1);

	var script = haxe.io.Path.join([haxe.io.Path.directory(Sys.programPath()), "script.cppia"]);
	for (jit in [false, true]) {
		cpp.cppia.Host.enableJit(jit);
		try {
			cpp.cppia.Host.runFile(script);
		} catch (e:Dynamic) {
			Sys.println("error: " + e);
		}
	}
}
