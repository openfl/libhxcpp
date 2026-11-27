package;

/** Compiled into the host and imported by the script using dll_import. **/
@:keep
class CppiaBridge {
	public static var name:String;
	public static var workload:CppiaWorkload;
	// Builds HostSprite into the host, for script classes to extend
	public static var spriteClass:Class<HostSprite> = HostSprite;

	@:noInline
	public static function step(value:Int):Int {
		return value ^ (value >>> 3) ^ 0x1357;
	}

	// As a stage dispatches ENTER_FRAME to a listener
	@:noInline
	public static function frames(count:Int, onFrame:Int->Void):Void {
		for (i in 0...count)
			onFrame(i);
	}

	@:noInline
	public static function repeat(count:Int, callback:Int->Int):Int {
		var sum = 0;
		for (i in 0...count)
			sum += callback(i);
		return sum;
	}
}
