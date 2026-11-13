// The JIT ignored fromIndex in indexOf and lastIndexOf on an Array<Dynamic>,
// and never evaluated it, so calls and exceptions in it were lost. The
// interpreter and compiled code use it. A typed array is the control.
class Script {
	static var calls = 0;

	static function from(n:Int):Int {
		calls++;
		return n;
	}

	static function fail():Int {
		throw "bad index";
	}

	static function main() {
		var typed = ["a", "b", "a", "b", "a"];
		Sys.println("typed: " + typed.indexOf("a", 1) + " " + typed.lastIndexOf("a", 3));

		var a:Array<Dynamic> = ["a", "b", "a", "b", "a"];
		Sys.println("omitted: " + a.indexOf("a") + " " + a.lastIndexOf("a"));
		Sys.println("from 2: " + a.indexOf("a", 2) + " " + a.lastIndexOf("a", 2));
		Sys.println("negative: " + a.indexOf("a", -2) + " " + a.lastIndexOf("a", -2));
		Sys.println("past the end: " + a.indexOf("a", 9) + " " + a.lastIndexOf("a", 9));
		Sys.println("evaluated: " + a.indexOf("a", from(3)) + " " + a.lastIndexOf("a", from(3)) + " calls=" + calls);
		try {
			a.indexOf("a", fail());
			Sys.println("throwing fromIndex: not thrown");
		} catch (e:Dynamic) {
			Sys.println("throwing fromIndex: " + e);
		}
	}
}
