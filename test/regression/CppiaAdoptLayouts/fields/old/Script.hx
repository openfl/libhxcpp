// Old generation: Child holds an Int only
class Child {
	public var n:Int = 1;
	public function new() {}
}

class Parent {
	public var c:Child;
	public function new() c = new Child();
	public function get():Int return c.n;
}

class Script {
	public static var p:Parent;
	public static function before():String return "p.get() = " + p.get();
	// Old code handing the moved Parent to new code, as a host callback would
	public static function after():String {
		var get = Reflect.field(p, "get");
		return "p.get() = " + Reflect.callMethod(p, get, []);
	}
	static function main() p = new Parent();
}
