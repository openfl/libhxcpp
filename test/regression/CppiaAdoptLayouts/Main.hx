import haxe.io.Path;

// libhxcpp #464. adoptInstances moved instances whose methods were not in the
// same vtable slots, so old code still running called the wrong method, or
// read its return as another type, and moved instances whose fields held
// instances that stayed, which new code read with the new layout. Neither
// moves now, and the report says why.
class Main {
	static function scenario(dir:String, name:String) {
		var load = (which:String) -> cpp.cppia.Module.fromData(sys.io.File.getBytes(Path.join([dir, '$name-$which.cppia'])).getData());
		var old = load("old");
		old.boot();
		old.run();
		var oldCls = old.resolveClass("Script");
		Sys.println("  before: " + Reflect.callMethod(oldCls, Reflect.field(oldCls, "before"), []));
		var next = load("new");
		next.boot();
		var report:Array<Dynamic> = untyped next.adoptInstances(old);
		for (r in report)
			Sys.println("  adopt " + r.name + ": moved " + r.moved + ", " + r.reason);
		Sys.println("  after, old code: " + Reflect.callMethod(oldCls, Reflect.field(oldCls, "after"), []));
		cpp.vm.Gc.run(true);
	}

	static function main() {
		var dir = Path.directory(Sys.programPath());
		for (jit in [false, true]) {
			cpp.cppia.Host.enableJit(jit);
			Sys.println(jit ? "jit" : "interpreted");
			for (name in ["methods", "fields"]) {
				Sys.println(name);
				try {
					scenario(dir, name);
				} catch (e:Dynamic) {
					Sys.println("  error: " + e);
				}
			}
		}
	}
}
