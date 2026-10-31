// Keeps the memory the script's address-only builtins use alive
var memory = haxe.io.Bytes.alloc(16);

function main() {
	untyped __global__.__hxcpp_memory_select(memory.getData());
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
