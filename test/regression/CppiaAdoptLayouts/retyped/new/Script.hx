// New generation: the same methods in the same slots, but a now returns an
// Int, which old code calling a moved instance would read as a String
class Foo {
	public var v:Int = 7;
	public function new() {}
	public function a():Int return 123;
	public function b():String return "new b " + v;
}

class Script {
	static function main() {}
}
