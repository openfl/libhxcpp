// With HXCPP_CHECK_POINTER, a call on a null Array<Dynamic> in cppia is a Null
// Object Reference, as it is on a typed array and in compiled code. push,
// contains, remove, indexOf, lastIndexOf, join, toString, sort, insert and
// unshift read the array without checking it, interpreted and under the JIT,
// and ended the process.
function main() {
	var script = haxe.io.Path.join([haxe.io.Path.directory(Sys.programPath()), "script.cppia"]);
	for (jit in [false, true]) {
		cpp.cppia.Host.enableJit(jit);
		Sys.println(jit ? "jit" : "interpreted");
		cpp.cppia.Host.runFile(script);
	}
}
