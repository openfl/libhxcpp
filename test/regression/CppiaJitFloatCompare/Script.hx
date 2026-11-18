class Script {
	// A NaN makes every comparison false but !=, as a value, as a branch and
	// negated. The jit compares Floats with sljit, which leaves what its plain
	// comparisons do with a NaN unspecified
	static function values(a:Float, b:Float):String {
		return flags([a < b, a <= b, a > b, a >= b, a == b, a != b]);
	}

	static function branches(a:Float, b:Float):String {
		var r = "";
		r += if (a < b) "T" else "F";
		r += if (a <= b) "T" else "F";
		r += if (a > b) "T" else "F";
		r += if (a >= b) "T" else "F";
		r += if (a == b) "T" else "F";
		r += if (a != b) "T" else "F";
		return r;
	}

	static function negated(a:Float, b:Float):String {
		var r = "";
		r += if (!(a < b)) "T" else "F";
		r += if (!(a <= b)) "T" else "F";
		r += if (!(a > b)) "T" else "F";
		r += if (!(a >= b)) "T" else "F";
		r += if (!(a == b)) "T" else "F";
		r += if (!(a != b)) "T" else "F";
		return r;
	}

	static function loops(a:Float, b:Float):String {
		var n = 0;
		while (a < b && n < 3)
			n++;
		var m = 0;
		while (!(a >= b) && m < 3)
			m++;
		return n + "" + m;
	}

	static function switched(a:Float):String {
		return switch (a) {
			case 1.0: "one";
			case 2.0: "two";
			default: "other";
		}
	}

	static function flags(v:Array<Bool>):String {
		var s = "";
		for (x in v)
			s += x ? "T" : "F";
		return s;
	}

	static function name(f:Float):String {
		return Math.isNaN(f) ? "nan" : Std.string(f);
	}

	static function main() {
		var all = [1.0, 2.0, Math.NaN];
		for (a in all)
			for (b in all)
				Sys.println(name(a) + " " + name(b) + ": " + values(a, b) + " " + branches(a, b) + " " + negated(a, b) + " " + loops(a, b) + " "
					+ switched(a));
	}
}
