// libhxcpp #463: array builtins that wrote to the wrong memory.
// - Under the JIT, a[i]++ used as a Float stored its result through whatever
//   the third argument register held.
// - Under the JIT, a typed push made room before running its argument, so an
//   argument that grew the array left too little room.
// - Interpreted, a[i] = rhs found the element before running rhs, so an rhs
//   that grew the array stored into the old buffer.
class Script {
	static var spy:Array<Int>;
	static var arr:Array<Int>;

	static function main() {
		crements();
		pushes();
		stores();
	}

	static function crements() {
		var f:Array<Float> = [1.5, 2.5];
		var x:Float = f[0]++;
		var y:Float = ++f[1];
		Sys.println("float crement: x=" + x + " y=" + y + " " + f);
		var a = [10, 20];
		var x:Float = a[0]++;
		var y:Float = --a[1];
		Sys.println("int crement: x=" + x + " y=" + y + " " + a);
	}

	// Grows a to exactly its capacity, then puts spy's buffer right after it
	static function grow(a:Array<String>):String {
		a.resize(a.length + 100);
		spy.resize(4);
		return "pushed";
	}

	static function growInt(a:Array<Int>):Int {
		a.resize(a.length + 101);
		spy.resize(4);
		return 0x7777;
	}

	static function pushes() {
		spy = [];
		var a = ["x"];
		a.push(grow(a));
		Sys.println("push string: length=" + a.length + " last=" + a[a.length - 1] + " spy=" + spy);
		spy = [];
		var b = [1];
		b.push(growInt(b));
		Sys.println("push int: length=" + b.length + " last=" + b[b.length - 1] + " spy=" + spy);
	}

	static function growAndGet():Int {
		for (i in 0...1000)
			arr.push(i);
		return 42;
	}

	static function stores() {
		arr = [1, 2, 3];
		arr[0] = growAndGet();
		Sys.println("store: " + arr[0] + " length=" + arr.length);
		arr = [1, 2, 3];
		arr[0] += growAndGet();
		Sys.println("compound store: " + arr[0]);
		arr = [1, 2, 3];
		arr[arr.length] = arr.length;
		Sys.println("store past the end: " + arr);
	}
}
