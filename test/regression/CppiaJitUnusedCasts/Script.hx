// The same cases as Compiled in Main.hx, run as a script
interface First {}
interface Second {}

class Plain implements First {
	public function new() {}
}

class Both extends Plain implements Second {
	public function new() {
		super();
	}
}

class Script {
	static function attempt(label:String, f:Void->Void) {
		try {
			f();
			Sys.println('$label passes');
		} catch (e:Dynamic) {
			Sys.println('$label throws');
		}
	}

	// A cast whose value goes unused, as a statement of a method
	static function toSecond(o:Dynamic) {
		cast(o, Second);
	}

	static function toBoth(o:Dynamic) {
		cast(o, Both);
	}

	static function main() {
		var plain = new Plain();
		var both = new Both();
		attempt("plain to first in a closure", function() cast(plain, First));
		attempt("plain to second in a closure", function() cast(plain, Second));
		attempt("both to second in a closure", function() cast(both, Second));
		attempt("plain to both in a closure", function() cast(plain, Both));
		attempt("plain to second in a method", () -> toSecond(plain));
		attempt("both to second in a method", () -> toSecond(both));
		attempt("plain to both in a method", () -> toBoth(plain));
		attempt("both to both in a method", () -> toBoth(both));
		attempt("plain to second, used", function() {
			var s:Second = cast(plain, Second);
			Sys.println('used ${s == null}');
		});
		var after = 0;
		attempt("a statement after a failed cast", function() {
			cast(plain, Second);
			after++;
		});
		Sys.println('ran on past it $after');
	}
}
