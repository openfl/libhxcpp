class Counter implements Stepper {
	public var count:Int;
	public var label:String;

	public function new(start:Int) {
		count = start;
		label = "counter";
	}

	public function step():Int
		return count += 10;

	public function version():String
		return "v2 " + label;
}

class Doubler extends Counter {
	override public function step():Int
		return count += 20;
}

class Reshaped {
	public var a:Int;
	public var b:Int;

	public function new()
		a = 1;

	public function version():String
		return "v2";
}

class Script {
	public static function main() {}
}
