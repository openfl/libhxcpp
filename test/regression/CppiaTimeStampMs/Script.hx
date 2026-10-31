class Script {
	static function main() {
		var before = now();
		Sys.sleep(0.05);
		var elapsed = now() - before;
		Sys.println(elapsed >= 40 && elapsed < 60000);
		var boxed:Dynamic = now();
		Sys.println(haxe.Int64.isInt64(boxed));
	}

	// Haxe 5's haxe.Timer.milliseconds() calls __time_stamp_ms, which cppia
	// didn't know
	static function now():haxe.Int64 {
		#if (haxe_ver >= 5)
		return haxe.Timer.milliseconds();
		#else
		return untyped __global__.__time_stamp_ms();
		#end
	}
}
