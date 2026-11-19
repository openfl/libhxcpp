// A super constructor call is linked to the constructor of the class it
// names. A class with none, in it or in a script class above it, has none to
// link to, which only a malformed cppia file asks for, and the loader read
// through the null constructor. A constructor whose body is not a function,
// as a malformed file can give, loaded, and a super call into it crashed the
// loader the same way, and a new of its class crashed when it ran, as a new of
// a script interface did. Such files fail to load now. Scripts whose classes
// take a constructor from above, from a script class or from the host, still
// load and run.
class Main {
	// Strings "new" and "main"; types Foo and Bar; Foo.main's body is the
	// expression given
	static function withMain(main:String, bar:String) {
		return 'CPPIA 2 3 new 4 main 3 0  3 Foo 3 Bar 2 CLASS 1 0 0 1 FUNCTION 1 0 1 0 0 0 0 FUN 0 0 0 0 $main $bar NOMAIN RESOURCES 0 ';
	}

	// Bar alone, and a main that makes one
	static function newBar(bar:String) {
		return 'CPPIA 2 3 new 4 main 3 0  3 Foo 3 Bar 1 $bar MAIN 0 0 NEW 2 0 RESOURCES 0 ';
	}

	static function check(name:String, code:String) {
		try {
			var module = cpp.cppia.Module.fromString(code);
			module.boot();
			module.run();
			Sys.println('$name loaded and ran');
			untyped module.release();
		} catch (e:Dynamic) {
			Sys.println('$name threw: $e');
		}
	}

	static function main() {
		Sys.println(new HostBase(0).made);
		var script = haxe.io.Path.join([haxe.io.Path.directory(Sys.programPath()), "script.cppia"]);
		for (jit in [false, true]) {
			cpp.cppia.Host.enableJit(jit);
			Sys.println(jit ? "jit" : "interpreted");
			check("super call into a class with no constructor", withMain("CALLSUPERNEW 2 0", "CLASS 2 0 0 0"));
			check("super call into an interface", withMain("CALLSUPERNEW 2 0", "INTERFACE 2 0 0 0"));
			check("super call into a constructor that is a return", withMain("CALLSUPERNEW 2 0", "CLASS 2 0 0 1 FUNCTION 1 0 0 0 0 0 0 RETURN"));
			check("super call into an interface with a constructor", withMain("CALLSUPERNEW 2 0", "INTERFACE 2 0 0 1 FUNCTION 1 0 0 0 0"));
			check("super call into a constructor", withMain("CALLSUPERNEW 2 0", "CLASS 2 0 0 1 FUNCTION 1 0 0 0 0 0 0 FUN 0 0 0 0 BLOCK 0"));
			check("new of a constructor that is a return", newBar("CLASS 2 0 0 1 FUNCTION 1 0 0 0 0 0 0 RETURN"));
			check("new of an interface", newBar("INTERFACE 2 0 0 0"));
			check("new of a class with no constructor", newBar("CLASS 2 0 0 0"));
			check("new of a constructor", newBar("CLASS 2 0 0 1 FUNCTION 1 0 0 0 0 0 0 FUN 0 0 0 0 BLOCK 0"));
			try {
				cpp.cppia.Host.runFile(script);
			} catch (e:Dynamic) {
				Sys.println("script threw: " + e);
			}
		}
	}
}
