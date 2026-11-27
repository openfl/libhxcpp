// The same cases as Compiled in Main.hx, run as a script
class Counter {
	var count = 0;
	var step:Int;

	public function new(step:Int)
		this.step = step;

	public function adder():Int->Int {
		return function(x:Int) {
			count += step;
			return x + count;
		};
	}

	public function label(prefix:String):Void->String
		return () -> prefix + ":" + count;
}

class Script {
	static var doubler = function(x:Int) return x * 2;

	static function fails():Int {
		throw "from a callee";
	}

	static function main() {
		var bias = 7;
		var step = function(v:Int):Int {
			bias = (bias + 3) & 1023;
			return (v & 1023) + bias;
		};
		var sum = 0;
		for (i in 0...1000)
			sum += step(i);
		Sys.println('captured Int $sum $bias');

		var counter = new Counter(5);
		var add = counter.adder();
		Sys.println('this ${add(1)} ${add(1)} ${add(10)} ${counter.label("n")()}');

		function outer(a:Int)
			return function(b:Int) return function(c:Int) return a * 100 + b * 10 + c;
		Sys.println('nested ${outer(1)(2)(3)} ${outer(4)(5)(6)}');

		function fib(n:Int):Int
			return n < 2 ? n : fib(n - 1) + fib(n - 2);
		Sys.println('recursive ${fib(20)}');

		var half = (x:Float) -> x * 1.5;
		var shout = (s:String) -> s + "!";
		var big = (i:Int) -> i > 3;
		var boxed = (i:Int) -> (i : Dynamic);
		var seen = [];
		var note = (i:Int) -> {
			seen.push(i);
		};
		note(1);
		note(2);
		var bigBox:Dynamic = big(5);
		Sys.println('returns ${half(3)} ${shout("hey")} ${big(2)} ${big(4)} $bigBox ${boxed(9)} $seen');

		function opt(a:Int, ?b:Int = 5, ?s:String = "d")
			return a + b + s;
		var optDynamic:Dynamic = opt;
		Sys.println('defaults ${opt(1)} ${opt(1, 2)} ${opt(1, 2, "x")} ${optDynamic(1)} ${Reflect.callMethod(null, opt, [3])}');

		var thrower = function(x:Int):Int {
			if (x > 2)
				throw "big " + x;
			return x;
		};
		var caught = [];
		for (i in 0...5)
			try {
				caught.push(Std.string(thrower(i)));
			} catch (e:String) {
				caught.push(e);
			}
		var guarded = (x:Int) -> try thrower(x) catch (e:String) -1;
		var callsFails = () -> {
			var r = fails();
			caught.push("ran on past the throw");
			return r;
		};
		try {
			callsFails();
		} catch (e:String) {
			caught.push(e);
		}
		try {
			[1, 2].map(x -> x > 1 ? throw "in a callback " + x : x);
		} catch (e:String) {
			caught.push(e);
		}
		Sys.println('throws $caught ${guarded(1)} ${guarded(7)}');

		var squares = [for (i in 0...4) () -> i * i];
		Sys.println('loop ${[for (f in squares) f()]}');

		var values = [3, 1, 2, 5, 4];
		var scaled = values.map(x -> x * 10);
		var odd = values.filter(x -> x & 1 == 1);
		values.sort((a, b) -> b - a);
		var total = Lambda.fold(values, (x, acc) -> acc + x, 100);
		Sys.println('host calls $scaled $odd $values $total');

		Sys.println('initializer ${doubler(21)}');

		var amount = 0.5;
		var addFloat = (x:Float) -> amount += x;
		addFloat(1.25);
		addFloat(2);
		var log = "";
		var append = (s:String) -> log += s;
		append("a");
		append("bc");
		Sys.println('captured Float and String $amount $log');

		var mixed:Dynamic = (a:Int, b:Float) -> a + b;
		Sys.println('dynamic call ${mixed(3, 1.5)} ${Reflect.callMethod(null, mixed, [2, 0.25])}');
	}
}
