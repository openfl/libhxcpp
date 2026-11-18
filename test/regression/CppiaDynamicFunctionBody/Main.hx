// A dynamic function's value is a closure of its function, made as its class
// boots or an instance is made. A malformed cppia file can give one a bare
// RETURN for its body, as an abstract function has, which is no function: the
// file loaded, and the closure of nothing crashed the next collection that
// marked it, or a call of it. Such a file fails to load now.
class Main {
	// Strings "new" and "f"; class Bar with the field given, and a main
	static function bar(field:String, main:String) {
		return 'CPPIA 2 3 new 1 f 2 0  3 Bar 1 CLASS 1 0 0 1 $field MAIN 0 0 $main RESOURCES 0 ';
	}

	static function check(name:String, code:String) {
		try {
			var module = cpp.cppia.Module.fromString(code);
			module.boot();
			module.run();
			cpp.vm.Gc.run(true);
			Sys.println('$name loaded and ran');
			untyped module.release();
		} catch (e:Dynamic) {
			Sys.println('$name threw: $e');
		}
	}

	static function main() {
		final makeBar = "NEW 1 0";
		final callF = "CALLMEMBER 1 1 0 0 0 NEW 1 0";
		for (jit in [false, true]) {
			cpp.cppia.Host.enableJit(jit);
			Sys.println(jit ? "jit" : "interpreted");
			check("static dynamic function that is a return", bar("FUNCTION 1 1 1 0 0 0 0 RETURN", makeBar));
			check("dynamic function that is a return", bar("FUNCTION 0 1 1 0 0 0 0 RETURN", callF));
			check("static dynamic function", bar("FUNCTION 1 1 1 0 0 0 0 FUN 0 0 0 0 BLOCK 0", makeBar));
			check("dynamic function", bar("FUNCTION 0 1 1 0 0 0 0 FUN 0 0 0 0 BLOCK 0", callF));
		}
	}
}
