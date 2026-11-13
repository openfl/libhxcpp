// Under the JIT, an exception from blit, setSize or resize called directly in
// a try went past the script's catch and ended the host, on a typed array
// and on Array<Dynamic>. Each one throws here, and the script catches it and
// finds the array as it was. A valid blit is the control.
class Script {
	static function main() {
		var dest = [7, 8];
		var source = [1, 2];
		var values = [7, 8];
		var anyDest:Array<Dynamic> = [7, "eight"];
		var anySource:Array<Dynamic> = [1, "two"];
		var any:Array<Dynamic> = [7, "eight"];
		try {
			cpp.NativeArray.blit(dest, 1, source, 0, 1);
			Sys.println("valid blit: ran, " + dest);
		} catch (e:Dynamic) {
			Sys.println("valid blit: caught " + e + ", " + dest);
		}
		try {
			cpp.NativeArray.blit(dest, -1, source, 0, 1);
			Sys.println("blit: ran, " + dest);
		} catch (e:Dynamic) {
			Sys.println("blit: caught " + e + ", " + dest);
		}
		try {
			cpp.NativeArray.setSize(values, -1);
			Sys.println("setSize: ran, " + values);
		} catch (e:Dynamic) {
			Sys.println("setSize: caught " + e + ", " + values);
		}
		try {
			values.resize(0x40000000);
			Sys.println("resize: ran, " + values);
		} catch (e:Dynamic) {
			Sys.println("resize: caught " + e + ", " + values);
		}
		try {
			cpp.NativeArray.blit(anyDest, -1, anySource, 0, 1);
			Sys.println("dynamic blit: ran, " + anyDest);
		} catch (e:Dynamic) {
			Sys.println("dynamic blit: caught " + e + ", " + anyDest);
		}
		try {
			cpp.NativeArray.setSize(any, -1);
			Sys.println("dynamic setSize: ran, " + any);
		} catch (e:Dynamic) {
			Sys.println("dynamic setSize: caught " + e + ", " + any);
		}
		try {
			any.resize(0x40000000);
			Sys.println("dynamic resize: ran, " + any);
		} catch (e:Dynamic) {
			Sys.println("dynamic resize: caught " + e + ", " + any);
		}
		Sys.println("continued");
	}
}
