// The same cases as Compiled in Main.hx, run as a script
class Holder {
	public var value:Int;
	public var callback:Int->Int;

	public function new(value:Int) {
		this.value = value;
		callback = x -> x + this.value;
	}

	public function scaled(x:Int):Int
		return x * value;

	public function describe(prefix:String, x:Float):String
		return prefix + (x + value);
}

class Script {
	static function show(label:String, value:Dynamic) {
		var kind = if (value == null) "null" else if (Std.isOfType(value, Bool)) "Bool" else if (Std.isOfType(value, Int)) "Int" else
			if (Std.isOfType(value, Float)) "Float" else if (Std.isOfType(value, String)) "String" else "other";
		Sys.println('$label $value $kind');
	}

	static function early(f:Int->Int->Int, stop:Bool):Int {
		var r = f(stop ? return -1 : 1, 2);
		return r * 10;
	}

	static function main() {
		var addInt = (a:Int, b:Int) -> a + b;
		var half = (x:Float) -> x / 2;
		var shout = (s:String) -> s + "!";
		var not = (b:Bool) -> !b;
		var isBig = (i:Int) -> i > 3;
		var seen = 0;
		var nothing = (i:Int) -> {
			seen += i;
		};
		var asDynamic = (x:Float) -> (x : Dynamic);
		var takesDynamic = (d:Dynamic) -> d;
		var takesInts = (a:Array<Int>) -> a.length * 100 + a[0];
		var six = (a:Int, b:Int, c:Int, d:Int, e:Int, f:Int) -> a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6;
		var inc = (x:Int) -> {
			x++;
			return x;
		};

		var i:Int = addInt(2, 3);
		var f:Float = addInt(2, 3);
		var s:String = "" + addInt(2, 3);
		var d:Dynamic = addInt(2, 3);
		addInt(2, 3);
		Sys.println('Int result as Int $i, Float $f, String $s, Dynamic $d');
		show("Int result as Dynamic", addInt(200, 100));

		var hi:Int = Std.int(half(9));
		var hf:Float = half(9);
		var hs:String = "" + half(9);
		var hd:Dynamic = half(9);
		Sys.println('Float result as Int $hi, Float $hf, String $hs, Dynamic $hd');
		show("whole Float result as Dynamic", half(8));
		show("negative zero result", 1 / half(-0.0));

		Sys.println('String result ${shout("hey")} ${shout(null)} ${shout("")}');
		var sd:Dynamic = shout("x");
		show("String result as Dynamic", sd);

		show("Bool result as Dynamic", not(false));
		show("Bool result from Int", isBig(5));
		var bi:Bool = isBig(2);
		Sys.println('Bool result as Bool $bi, String ${"" + isBig(9)}');
		nothing(4);
		nothing(5);
		Sys.println('Void result $seen');

		show("Int to Dynamic", takesDynamic(7));
		show("big Int to Dynamic", takesDynamic(100000));
		show("Bool to Dynamic", takesDynamic(true));
		show("Float to Dynamic", takesDynamic(2.5));
		show("whole Float to Dynamic", takesDynamic(3.0));
		show("String to Dynamic", takesDynamic("s"));
		show("Float to Dynamic and back", asDynamic(4.0));
		show("Int to Float", half(7));

		var ints = [4, 5, 6];
		var intsDynamic:Dynamic = ints;
		Sys.println('Array argument ${takesInts(ints)} ${takesInts(intsDynamic)}');

		Sys.println('six arguments ${six(1, 2, 3, 4, 5, 6)}');
		var v = 5;
		Sys.println('argument changed in the callee ${inc(v)} $v');
		Sys.println('nested ${addInt(addInt(1, 2), addInt(addInt(3, 4), 5))}');

		var holder = new Holder(10);
		var bound = holder.scaled;
		var dynamicHolder:Dynamic = holder;
		var fields:Array<Int->Int> = [bound, holder.callback, x -> x - 1];
		Sys.println('bound ${bound(3)} ${holder.callback(3)} ${dynamicHolder.scaled(4)} ${dynamicHolder.describe("d", 0.5)} ${[for (g in fields) g(7)]}');

		var hostAbs:Float->Float = Math.abs;
		var hostString:Dynamic->String = Std.string;
		Sys.println('host functions ${hostAbs(-3.5)} ${hostString(12)}');

		var none:Int->Int = null;
		var outcome = try Std.string(none(1)) catch (e:Dynamic) "null function throws";
		Sys.println(outcome);

		var thrower = (x:Int) -> x > 2 ? throw "too big" : x;
		var total = 1;
		for (k in 0...5)
			try {
				total += thrower(k);
			} catch (e:String) {
				total += 100;
			}
		Sys.println('throws $total');

		Sys.println('returns from an argument ${early(addInt, false)} ${early(addInt, true)}');

		function fib(n:Int):Int
			return n < 2 ? n : fib(n - 1) + fib(n - 2);
		var fibValue:Int->Int = fib;
		Sys.println('recursive ${fibValue(15)}');

		var count = 0;
		var counter = () -> ++count;
		for (k in 0...3)
			counter();
		Sys.println('no arguments ${counter()} $count');
	}
}
