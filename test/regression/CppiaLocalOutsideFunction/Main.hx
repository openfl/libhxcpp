// A local variable or a return needs a function's frame. A malformed cppia
// file can put one outside any function, as a static var's initial value,
// where linking it read the frame layout through a null pointer. Such a file
// fails to load with an error now. The compiler moves an initial value that
// declares locals into the class's init function, so those still run.
function check(name:String, init:String) {
	var code = 'CPPIA 1 1 x 2 0  3 Foo 1 CLASS 1 0 0 1 VAR 1 N N 0 0 0 1 $init NOMAIN RESOURCES 0 ';
	try {
		var module = cpp.cppia.Module.fromString(code);
		Sys.println('$name loaded');
		untyped module.release();
	} catch (e:Dynamic) {
		Sys.println('$name threw: $e');
	}
}

function main() {
	check("variable reference", "0 0 VAR 7");
	check("variable declaration", "0 0 TVARS 1 VARDECLI 0 7 0 0 0 0 0 i 1");
	check("return", "0 0 RETVAL 0 0 0 i 1");
	check("constant", "0 0 i 5");

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
