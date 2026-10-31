// Calls each global builtin cppia knows. Under the JIT most of them had no
// code and only traced their name, and the int setters with a buffer stored
// their values as floats.
class Script {
	static function main() {
		var bytes = haxe.io.Bytes.alloc(24);
		var d = bytes.getData();
		untyped __global__.__hxcpp_memory_set_byte(d, 0, 0x71);
		untyped __global__.__hxcpp_memory_set_i16(d, 2, -2);
		untyped __global__.__hxcpp_memory_set_ui16(d, 4, 0xfffe);
		untyped __global__.__hxcpp_memory_set_i32(d, 8, -3);
		untyped __global__.__hxcpp_memory_set_ui32(d, 12, 5);
		untyped __global__.__hxcpp_memory_set_float(d, 16, 1.5);
		Sys.println([for (i in 0...16) bytes.get(i)].join(","));
		Sys.println([
			untyped __global__.__hxcpp_memory_get_byte(d, 0),
			untyped __global__.__hxcpp_memory_get_i16(d, 2),
			untyped __global__.__hxcpp_memory_get_ui16(d, 4),
			untyped __global__.__hxcpp_memory_get_i32(d, 8),
			untyped __global__.__hxcpp_memory_get_ui32(d, 12)
		]);
		var f:Float = untyped __global__.__hxcpp_memory_get_float(d, 16);
		untyped __global__.__hxcpp_memory_set_double(d, 16, 0.25);
		var g:Float = untyped __global__.__hxcpp_memory_get_double(d, 16);
		Sys.println([f, g]);

		// The host selected the memory these use
		untyped __global__.__hxcpp_memory_set_byte(0, 0x7f);
		untyped __global__.__hxcpp_memory_set_i16(2, -4);
		untyped __global__.__hxcpp_memory_set_ui16(4, 0xfffc);
		untyped __global__.__hxcpp_memory_set_i32(8, -6);
		untyped __global__.__hxcpp_memory_set_ui32(12, 9);
		Sys.println([
			untyped __global__.__hxcpp_memory_get_byte(0),
			untyped __global__.__hxcpp_memory_get_i16(2),
			untyped __global__.__hxcpp_memory_get_ui16(4),
			untyped __global__.__hxcpp_memory_get_i32(8),
			untyped __global__.__hxcpp_memory_get_ui32(12)
		]);
		untyped __global__.__hxcpp_memory_set_float(0, 2.5);
		var f:Float = untyped __global__.__hxcpp_memory_get_float(0);
		untyped __global__.__hxcpp_memory_set_double(8, 0.125);
		var g:Float = untyped __global__.__hxcpp_memory_get_double(8);
		Sys.println([f, g]);

		var lock = new sys.thread.Lock();
		untyped __global__.__hxcpp_thread_create(function() lock.release());
		Sys.println(lock.wait(5));
	}
}
