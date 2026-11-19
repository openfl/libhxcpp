import haxe.crypto.Base64;

// libhxcpp #467: malformed cppia files that crashed the host instead of
// failing to load. SECURITY.md promises a load error. Each is loaded and run
// interpreted, then under the JIT compiling every function as it loads.
class Main {
	static function main() {
		for (jit in [false, true]) {
			cpp.cppia.Host.enableJit(jit);
			untyped __cpp__("::hx::gLazyJit = false");
			Sys.println(jit ? "jit" : "interpreted");
			for (f in Files.all) {
				var result = try {
					var module = cpp.cppia.Module.fromData(Base64.decode(f.data).getData());
					module.boot();
					module.run();
					"ran";
				} catch (e:Dynamic) {
					"failed to load";
				}
				Sys.println('  ${f.name}: $result');
			}
		}
		// Referenced so --dce no keeps what the scripts link against
		var keep:Array<Dynamic> = [haxe.ds.StringMap, haxe.ds.IntMap, haxe.ds.ObjectMap, haxe.io.Bytes, haxe.Timer, cpp.vm.Gc, Lambda, StringTools,
			EReg, Reflect, Type, Std, Math, Date, StringBuf, Sys, haxe.iterators.ArrayIterator];
	}
}
