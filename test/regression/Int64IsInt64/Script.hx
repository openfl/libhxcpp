import haxe.Int64;

class Script {
	static function main() {
		final int:Dynamic = 100;
		Sys.println('script: isInt64 of the Int 100 is false: ${!Int64.isInt64(int)}');
		Sys.println('script: isInt64 of the Int64 100 is true: ${Int64.isInt64(Int64.ofInt(100))}');
		Sys.println('script: isInt64 of the Int64 2^32 is true: ${Int64.isInt64(Int64.make(1, 0))}');
	}
}
