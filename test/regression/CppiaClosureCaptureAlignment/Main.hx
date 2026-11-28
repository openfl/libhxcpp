// A cppia closure packs the variables it captures at 4-byte offsets, after
// the 8-byte this on 64-bit, so a String or object captured after an Int sits
// at an odd multiple of 4. The collector marked and visited those captures in
// place: undefined behaviour (UBSan reports it) and a fault on a CPU that
// requires alignment. With HXCPP_GC_DEBUG_ALWAYS_MOVE every collection moves
// objects, so the visit must also write each moved capture back, or the
// closure reads a moved object. Interpreted and jitted script closures are the
// same objects, so both runs reach it.
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
