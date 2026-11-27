// A script reaches a private host class through the inline functions of a
// public abstract or class. The Haxe compiler writes script functions for
// public classes only, so such a call was made by name with boxed arguments.
// The host now has a typed entry for each of them. A typed call and a call by
// name give the same results, so the test also asks the runtime which host
// functions the script called by name: only the one made through a private
// interface, which has no entries. Math's functions, which the runtime
// implements, are typed calls the same way.
import Store.Tools;

function main() {
	// Keeps the classes in the host
	var keep = new Store();
	keep.push(Tools.twice(1));

	var script = haxe.io.Path.join([haxe.io.Path.directory(Sys.programPath()), "script.cppia"]);
	for (jit in [false, true]) {
		cpp.cppia.Host.enableJit(jit);
		try {
			cpp.cppia.Host.runFile(script);
		} catch (e:Dynamic) {
			Sys.println("error: " + e);
		}
	}

	var byName:Array<String> = untyped __global__.__scriptable_cppia_calls_by_name();
	Sys.println("called by name: " + byName.join(", "));
}
