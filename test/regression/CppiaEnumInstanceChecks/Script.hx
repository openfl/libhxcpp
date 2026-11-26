import HostEnums;

enum ScriptA {
	ScriptOnlyA;
}

enum ScriptB {
	ScriptOnlyB;
	ScriptWith(n:Int);
}

class Script {
	static function hostAThenHostB(value:Dynamic):String {
		return try {
			throw value;
		} catch (e:HostA) {
			"HostA";
		} catch (e:HostB) {
			"HostB";
		} catch (e:ScriptA) {
			"ScriptA";
		} catch (e:ScriptB) {
			"ScriptB";
		} catch (e:Dynamic) {
			"Dynamic";
		}
	}

	static function scriptAThenScriptB(value:Dynamic):String {
		return try {
			throw value;
		} catch (e:ScriptA) {
			"ScriptA";
		} catch (e:ScriptB) {
			"ScriptB";
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
		Sys.println("  " + label + ": caught by host first " + hostAThenHostB(value) + ", by script first " + scriptAThenScriptB(value));
		Sys.println("    isOfType HostA " + Std.isOfType(value, HostA) + ", HostB " + Std.isOfType(value, HostB) + ", ScriptA "
			+ Std.isOfType(value, ScriptA) + ", ScriptB " + Std.isOfType(value, ScriptB) + ", Enum " + Std.isOfType(value, Enum));
		Sys.println("    is HostA " + (value is HostA) + ", is ScriptB " + (value is ScriptB));
		Sys.println("    cast HostA " + attempt(() -> cast(value, HostA)) + ", cast HostB " + attempt(() -> cast(value, HostB))
			+ ", cast ScriptA " + attempt(() -> cast(value, ScriptA)) + ", cast ScriptB " + attempt(() -> cast(value, ScriptB)));
	}

	static function main() {
		check("HostB.HostOnlyB", HostB.HostOnlyB);
		check("HostB.HostWith(1)", HostB.HostWith(1));
		check("HostA.HostOnlyA", HostA.HostOnlyA);
		check("ScriptB.ScriptOnlyB", ScriptB.ScriptOnlyB);
		check("ScriptB.ScriptWith(1)", ScriptB.ScriptWith(1));
		check("ScriptA.ScriptOnlyA", ScriptA.ScriptOnlyA);
		check("a String", "text");
		var typed:Bool = try {
			throw HostB.HostOnlyB;
		} catch (e:HostA) {
			false;
		} catch (e:HostB) {
			true;
		}
		Sys.println("  typed throw of HostB.HostOnlyB reaches the HostB catch: " + typed);
		Sys.println("  getEnum(HostB.HostOnlyB) == HostB: " + (Type.getEnum(HostB.HostOnlyB) == HostB) + ", getEnum(ScriptB.ScriptOnlyB) == ScriptB: "
			+ (Type.getEnum(ScriptB.ScriptOnlyB) == ScriptB));
	}
}
