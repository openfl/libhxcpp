class Counter implements Stepper {
	public var count:Int;
	public var label:String;

	public function new(start:Int) {
		count = start;
		label = "counter";
	}

	public function step():Int
		return count += 1;

	public function version():String
		return "v1 " + label;
}

class Doubler extends Counter {
	override public function step():Int
		return count += 2;
}

// v2 adds a field, so its instances cannot move
class Reshaped {
	public var a:Int;

	public function new()
		a = 1;

	public function version():String
		return "v1";
}

class Script {
	public static function main() {}
}
