// New generation: Parent is laid out the same, so its instances move, but Child
// gained a String before n, so its instances stay old. The moved Parent's c is
// still an old Child, which the new code reads with the new Child's layout
class Child {
	public var s:String = "new child";
	public var n:Int = 2;
	public function new() {}
}

class Parent {
	public var c:Child;
	public function new() c = new Child();
	public function get():Int {
		Sys.println("  new code reads c.s -> " + (c.s == null ? "null" : "length " + c.s.length));
		return c.n;
	}
}

class Script {
	static function main() {}
}
