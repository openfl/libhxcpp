// libhxcpp #468: array builtins where the cppia interpreter and JIT disagreed.
// Each line is printed by both, and is what the other targets print.
class Obj {
	public function new() {}

	public function toString() {
		Script.calls++;
		return "obj";
	}
}

class Script {
	public static var calls = 0;
	static var got = 0;

	static function main() {
		anyCompound();
		anyCrement();
		anyOthers();
		anyBools();
		nullIntBits();
		nullIntArith();
		searchAsFloat();
		statements();
		outOfRange();
		dynamicCrement();
	}

	static function anyCompound() {
		var a:Array<Dynamic> = [1, 2, 3, 4, 5];
		a[0] += 10;
		a[1] *= 5;
		a[2] |= 8;
		a[3] -= 1;
		a[4] <<= 2;
		Sys.println("Array<Dynamic> compound: " + a);
	}

	static function anyCrement() {
		var a:Array<Dynamic> = [1, 2, 3];
		a[0]++;
		var x = a[1]--;
		var y = ++a[2];
		Sys.println("Array<Dynamic> crement: " + a + " x=" + x + " y=" + y);
	}

	static function anyOthers() {
		var a:Array<Dynamic> = [1, 2];
		a.insert(0, "z");
		Sys.println("Array<Dynamic> insert: " + a);
		var s:String = a.toString();
		Sys.println("Array<Dynamic> toString: " + s);
	}

	static function anyBools() {
		var flag = true;
		var b:Array<Any> = [];
		b.push(flag);
		b.unshift(false);
		b[2] = true;
		Sys.println("Array<Any> bools: " + b + " " + [for (x in b) Std.isOfType(x, Bool)]);
	}

	static function nullIntBits() {
		var n:Array<Null<Int>> = [1, 2, 3, 16, 16, 7];
		n[0] |= 4;
		n[1] <<= 2;
		n[2] ^= 1;
		n[3] >>= 2;
		n[4] >>>= 1;
		n[5] &= 3;
		Sys.println("Array<Null<Int>> bits: " + n);
	}

	static function nullIntArith() {
		var n:Array<Null<Int>> = [1, 2, 3];
		n[0] += 4;
		n[1] *= 3;
		n[2] -= 1;
		Sys.println("Array<Null<Int>> arith: " + n);
	}

	static function searchAsFloat() {
		var a = [1, 2, 3];
		var f = 0.5 + a.indexOf(3);
		var g = 0.5 + a.lastIndexOf(1);
		var h:Float = a.indexOf(2);
		Sys.println("indexOf as a Float: " + f + " " + g + " " + h);
	}

	static function arr():Array<Obj> {
		got++;
		return [new Obj()];
	}

	static function statements() {
		arr().join(",");
		Sys.println("join as a statement: receiver evaluated " + got + ", toString calls " + calls);
		arr().toString();
		Sys.println("toString as a statement: receiver evaluated " + got + ", toString calls " + calls);
	}

	static function outOfRange() {
		var ints = [1, 2];
		var floats = [1.5];
		Sys.println("out of range as a String: " + ints[5] + " " + floats[3]);
	}

	static function dynamicCrement() {
		var d:Dynamic = [1, 2];
		++d[0];
		d[1]++;
		Sys.println("++ on a Dynamic array as a statement: " + d);
	}
}
