// The interpreter took reverse() and resize() on an Array<Dynamic> but did
// neither, and never evaluated resize's argument. The JIT and compiled code
// do both. A typed array is the control.
class Script {
	static var calls = 0;

	static function size(n:Int):Int {
		calls++;
		return n;
	}

	static function main() {
		var typed = [1, 2, 3];
		typed.reverse();
		typed.resize(2);
		Sys.println("typed: " + typed.join(","));

		var a:Array<Dynamic> = [1, "two", 3];
		a.reverse();
		Sys.println("reversed: " + a.join(","));
		a.resize(size(1));
		Sys.println("shrunk: " + a.join(",") + " calls=" + calls);
		a.resize(size(3));
		Sys.println("grown: " + a.length + " " + a.join(",") + " calls=" + calls);
		a.resize(size(0));
		Sys.println("emptied: " + a.length + " calls=" + calls);
	}
}
