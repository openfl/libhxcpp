import haxe.Int64;

// haxe.Int64.isInt64 asks cpp.Int64's handler whether a value is one, and the
// handler said yes to a boxed Int as well, so isInt64(100) was true here and
// false on the other targets. It took Ints because small Int64 values used to
// box as Ints; they have had boxes of their own since 6.0.0, so every Int64
// boxes as an Int64, and an Int is not one. A cppia script asks the same.
function claim(text:String, value:Bool)
	Sys.println('$text: $value');

function main() {
	final int:Dynamic = 100;
	final zero:Dynamic = 0;
	final float:Dynamic = 1.0;
	claim("isInt64 of the Int 100 is false", !Int64.isInt64(int));
	claim("isInt64 of the Int 0 is false", !Int64.isInt64(zero));
	claim("isInt64 of 1.0 is false", !Int64.isInt64(float));
	claim("isInt64 of null is false", !Int64.isInt64(null));
	claim("isInt64 of a String is false", !Int64.isInt64("1"));
	for (value in [Int64.ofInt(100), Int64.ofInt(-1), Int64.ofInt(255), Int64.ofInt(-2), Int64.ofInt(256), Int64.make(1, 0)])
		claim('isInt64 of the Int64 $value is true', Int64.isInt64(value));

	// isInt64 asks the handler with Haxe 4.2 and cpp.Int64's class from 4.3
	final int64:Dynamic = Int64.ofInt(100);
	claim("the handler says the Int 100 is not an Int64", !untyped __cpp__("::cpp::Int64Struct::is({0})", int));
	claim("the handler says the Int64 100 is one", untyped __cpp__("::cpp::Int64Struct::is({0})", int64));
	claim("the class says the Int 100 is not an Int64", !Std.isOfType(int, untyped __cpp__("::hx::ClassOf< ::cpp::Int64 >()")));
	claim("the class says the Int64 100 is one", Std.isOfType(int64, untyped __cpp__("::hx::ClassOf< ::cpp::Int64 >()")));
	claim("the Int64 100 is still an Int", Std.isOfType(int64, Int));

	final script = haxe.io.Path.join([haxe.io.Path.directory(Sys.programPath()), "script.cppia"]);
	for (jit in [false, true]) {
		cpp.cppia.Host.enableJit(jit);
		cpp.cppia.Host.runFile(script);
	}
}
