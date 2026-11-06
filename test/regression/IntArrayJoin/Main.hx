// An Array<Int> writes its digits straight into the joined string
// (openfl/libhxcpp#415), which must give what joining the elements' own
// strings gave, with any separator
class Main {
	static function expected(values:Array<Int>, separator:String):String {
		var b = new StringBuf();
		for (i in 0...values.length) {
			if (i > 0)
				b.add(separator);
			b.add(Std.string(values[i]));
		}
		return b.toString();
	}

	static function main() {
		var values:Array<Int> = [0, -1, 1, 9, 10, -10, 99, 100, 2147483647, -2147483648, -2147483647];
		for (i in 0...1000)
			values.push((i * 7919) ^ (i << 20));
		var same = true;
		for (separator in [",", "", ", ", " - ", "é", "\u{1F600}"])
			if (values.join(separator) != expected(values, separator))
				same = false;
		Sys.println("joined integers match their strings: " + same);
		Sys.println("one: " + [42].join(",") + ", none: [" + ([] : Array<Int>).join(",") + "]");
		Sys.println("printed: " + Std.string([-3, 0, 12]) + " " + [[1, -2], [30]]);
	}
}
