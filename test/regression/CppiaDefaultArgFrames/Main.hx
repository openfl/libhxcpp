// The cppia interpreter fills in an argument's default value in the function's
// frame, which only keeps 4-byte alignment. It read the argument's String or
// object slot in place, and wrote a Float or Dynamic default the same way, so
// on 64-bit, where a default after an Int is at an odd multiple of 4, that was
// undefined behaviour (UBSan reports it) and a fault on a CPU that requires
// alignment. The jit compiles defaults into the function instead, so the jit
// run is here for comparison.
function main() {
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
