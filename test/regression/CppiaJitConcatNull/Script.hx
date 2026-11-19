// Under the JIT, concat with a null array, which throws Null Object Reference
// with HXCPP_CHECK_POINTER, went past the script's catch and ended the host,
// on a typed array and on Array<Any>. Interpreted, the script catches it.
class Script {
	static var noInts:Array<Int> = null;
	static var noStrings:Array<String> = null;
	static var noAny:Array<Any> = null;

	static function main() {
		var ints = [1, 2];
		var strings = ["a"];
		var any:Array<Any> = [1, "b"];
		try {
			Sys.println("ints: ran, " + ints.concat(noInts));
		} catch (e:Dynamic) {
			Sys.println("ints: caught " + e);
		}
		try {
			Sys.println("strings: ran, " + strings.concat(noStrings));
		} catch (e:Dynamic) {
			Sys.println("strings: caught " + e);
		}
		try {
			Sys.println("any: ran, " + any.concat(noAny));
		} catch (e:Dynamic) {
			Sys.println("any: caught " + e);
		}
		Sys.println("valid: " + ints.concat([3]) + " " + any.concat([true]));
	}
}
