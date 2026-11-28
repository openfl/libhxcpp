package;

/** Compiled into the host and imported by the script using dll_import. **/
@:keep
class CppiaBridge {
	public static var name:String;
	public static var workload:CppiaWorkload;
	/** Read and written by the host_statics workload. **/
	public static var values:Array<Int> = [for (i in 0...16) i];
	public static var last:Array<Int>;

	@:noInline
	public static function step(value:Int):Int {
		return value ^ (value >>> 3) ^ 0x1357;
	}

	@:noInline
	public static function repeat(count:Int, callback:Int->Int):Int {
		var sum = 0;
		for (i in 0...count)
			sum += callback(i);
		return sum;
	}
}
