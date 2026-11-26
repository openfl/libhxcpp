// A 32-bit float joined straight to a String went through its own formatting,
// still "%.15g", so "" + bytes.getFloat(0) printed 0.333333343267441 where the
// same value held in a Float printed 0.3333333432674408, as cppia and the
// other targets print it. Each pair of lines here must be the same.
function show(name:String, bytes:haxe.io.Bytes) {
	// The read is a C++ float, and is joined as one
	Sys.println(name + " " + bytes.getFloat(0));
	var held:Float = bytes.getFloat(0);
	Sys.println(name + " " + held);
}

function main() {
	var bytes = haxe.io.Bytes.alloc(4);
	for (value in [1 / 3, 0.1, 16777217.0, 1e-7, 123456.789, -2.5, 3.4028234663852886e38, 1.401298464324817e-45, 0.0]) {
		bytes.setFloat(0, value);
		show("float", bytes);
	}
	bytes.setInt32(0, 0x80000000);
	show("negative zero", bytes);
	bytes.setInt32(0, 0x7f800000);
	show("infinity", bytes);
	bytes.setInt32(0, 0xff800000);
	show("negative infinity", bytes);
	bytes.setInt32(0, 0x7fc00000);
	show("nan", bytes);

	// With a format set, a float takes it as a Float does
	bytes.setFloat(0, 1 / 3);
	untyped __global__.__hxcpp_set_float_format("%.3f");
	show("format", bytes);
}
