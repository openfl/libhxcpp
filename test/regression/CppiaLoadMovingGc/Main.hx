// A collection that moves objects visits every loaded module to update its
// pointers, the module that is loading too. That one's list of types is made
// before its entries, and visiting an entry not yet made crashed. With
// HXCPP_GC_DEBUG_ALWAYS_MOVE every collection moves, and in a debug build
// __hxcpp_spam_collects collects every few allocations, so collections land
// while the module loads
function main() {
	var path = haxe.io.Path.join([haxe.io.Path.directory(Sys.programPath()), "script.cppia"]);
	var bytes = sys.io.File.getBytes(path);
	for (jit in [false, true]) {
		cpp.cppia.Host.enableJit(jit);
		for (i in 0...10) {
			untyped __global__.__hxcpp_spam_collects(3);
			var module = cpp.cppia.Module.fromData(bytes.getData());
			untyped __global__.__hxcpp_spam_collects(0);
			module.boot();
			if (i == 0)
				module.run();
			untyped module.release();
		}
	}
	Sys.println("loaded 20 times");
}
