// New generation: the same fields, so Foo's instances move, but a method added
// first shifts a and b one vtable slot on ("Methods can differ in any way")
class Foo {
	public var v:Int = 7;
	public function new() {}
	public function x():Int return 12345;
	public function a():String return "new a " + v;
	public function b():String return "new b " + v;
}

class Script {
	static function main() {}
}
