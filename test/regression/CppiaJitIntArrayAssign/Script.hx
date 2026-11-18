class Hasher {
	public var parts = [17, 17];

	public function new() {}

	public function add(c:Int) {
		parts[0] *= 31;
		parts[0] += c;
		parts[1] -= c * 1000003;
	}
}

// Int array *= and -= under the JIT went through Float arithmetic, so a
// result past the Int range converted to -2147483648 instead of wrapping as
// compiled code and the interpreter do
class Script {
	static var values = [2147483647, -2147483648];

	public static function main() {
		var h = new Hasher();
		for (c in "hash me please".split(""))
			h.add(c.charCodeAt(0));
		Sys.println(h.parts);

		var a = [0, 2147483647];
		var i = 1;
		var r = (a[i] *= 3);
		Sys.println(r + " " + a[i]);
		a = [-2147483648];
		a[0] -= 3;
		Sys.println(a[0]);

		values[0] *= 2;
		values[1] -= 1;
		Sys.println(values);
	}
}
