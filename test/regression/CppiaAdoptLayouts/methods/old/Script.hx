// Old generation: Foo.a and Foo.b take the first two script vtable slots
class Foo {
	public var v:Int = 7;
	public function new() {}
	public function a():String return "old a " + v;
	public function b():String return "old b " + v;
}

class Script {
	public static var foo:Foo;
	// Old code calling methods on an instance that adoptInstances moves
	public static function before():String return foo.b() + " / " + foo.a();
	public static function after():String {
		var s = foo.b();
		Sys.println("  old code foo.b() -> " + s);
		var t = foo.a();
		Sys.println("  old code foo.a() -> length " + t.length);
		return s + " / " + t;
	}
	static function main() foo = new Foo();
}
