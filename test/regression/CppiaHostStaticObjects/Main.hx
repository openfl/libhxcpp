// A cppia script reads and writes a host class's static object fields: an
// object, an Array, a function and a typed object. They were read and written
// by name, through __GetStatic and __SetStatic, on every access in Haxe 4
// builds; they are direct references now, as an object member is and as a
// host static Int or String already was. The objects stored are young and
// only the statics hold them, through minor and full collections.
function main() {
	var script = haxe.io.Path.join([haxe.io.Path.directory(Sys.programPath()), "script.cppia"]);
	for (jit in [false, true]) {
		cpp.cppia.Host.enableJit(jit);
		try {
			cpp.cppia.Host.runFile(script);
		} catch (e:Dynamic) {
			Sys.println("error: " + e);
		}
	}
}
