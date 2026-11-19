// cppia negated an Int as a Float wherever the result was not an Int: as
// Dynamic, in a String or as a Float. -0 printed as "-0",
// -(-2147483648) gave 2147483648 and Type.typeof gave TFloat, interpreted
// and under the JIT
class Script {
	static var zero = 0;
	static var five = 5;
	static var min = -2147483648;

	static function describe(d:Dynamic):String
		return Std.string(d) + " " + Type.typeof(d);

	public static function main() {
		Sys.println(describe(-zero) + ", " + describe(-five) + ", " + describe(-min));
		var asDynamic:Dynamic = -zero;
		var row:Array<Dynamic> = [-zero, -min];
		Sys.println(describe(asDynamic) + ", " + row);
		Sys.println("" + (-zero) + " " + Std.string(-five) + " " + (-min));
		var f:Float = -min;
		Sys.println(f);
	}
}
