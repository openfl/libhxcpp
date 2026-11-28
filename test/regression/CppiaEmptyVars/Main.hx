// A var statement in a cppia file lists its declarations with a count, and
// the loader accepts a count of 0. The interpreter then ran the statement
// from &vars[0] of the empty list, which is undefined behaviour, and a
// standard library with checked indexing stops there. Nothing here checks
// indexing, so this passed before too; it keeps both engines running the
// empty statement.
function run(name:String, code:String) {
	try {
		final module = cpp.cppia.Module.fromString(code);
		module.boot();
		module.run();
		Sys.println('$name ran');
	} catch (e:Dynamic) {
		Sys.println('$name threw $e');
	}
}

function main() {
	for (jit in [false, true]) {
		cpp.cppia.Host.enableJit(jit);
		final engine = jit ? "jit" : "interpreter";
		run('$engine: an empty var statement', "CPPIA 0 1 4 void 0 MAIN 0 0 TVARS 0 RESOURCES 0 ");
		run('$engine: two in a block', "CPPIA 0 1 4 void 0 MAIN 0 0 BLOCK 2 0 0 TVARS 0 0 0 TVARS 0 RESOURCES 0 ");
	}
}
