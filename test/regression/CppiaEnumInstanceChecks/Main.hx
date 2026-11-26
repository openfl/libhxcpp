// A typed catch of one enum caught a value of any enum in compiled code, and
// Std.isOfType, is and cast agreed with it. Compiled code, the cppia
// interpreter and the cppia JIT must give the answers Haxe's interpreter gives,
// for host enums and script enums each way. openfl/libhxcpp#491
import HostEnums;

class Compiled {
	static function aThenB(value:Dynamic):String {
		return try {
			throw value;
		} catch (e:HostA) {
			"HostA";
		} catch (e:HostB) {
			"HostB";
		} catch (e:Dynamic) {
			"Dynamic";
		}
	}

	static function attempt(f:Void->Dynamic):String {
		return try {
			Std.string(f());
		} catch (e:Dynamic) {
			"throws";
		}
	}

	static function check(label:String, value:Dynamic) {
		Sys.println("  " + label + ": caught " + aThenB(value) + ", isOfType HostA " + Std.isOfType(value, HostA) + ", HostB "
			+ Std.isOfType(value, HostB) + ", is HostA " + (value is HostA) + ", cast HostA " + attempt(() -> cast(value, HostA)) + ", cast HostB "
			+ attempt(() -> cast(value, HostB)));
	}

	public static function run() {
		Sys.println("compiled");
		check("HostB.HostOnlyB", HostB.HostOnlyB);
		check("HostB.HostWith(1)", HostB.HostWith(1));
		check("HostA.HostOnlyA", HostA.HostOnlyA);
		check("a String", "text");
	}
}

function main() {
	Compiled.run();
	var script = haxe.io.Path.join([haxe.io.Path.directory(Sys.programPath()), "script.cppia"]);
	for (jit in [false, true]) {
		Sys.println(jit ? "jit" : "interpreted");
		cpp.cppia.Host.enableJit(jit);
		try {
			cpp.cppia.Host.runFile(script);
		} catch (e:Dynamic) {
			Sys.println("error: " + e);
		}
	}
}
