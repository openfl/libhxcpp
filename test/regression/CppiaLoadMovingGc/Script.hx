// Types enough that collections land while the module makes them
class Script {
	static var names = new Map<String, Array<Int>>();
	static var values = new haxe.ds.IntMap<String>();
	static var list = new haxe.ds.List<Float>();
	static var buffer = new StringBuf();
	static var bytes = haxe.io.Bytes.alloc(4);
	static var date = Date.now();
	static var reg = ~/a+/;

	public static function main() {
		names.set("a", [1]);
		values.set(1, "b");
		list.add(1.5);
		buffer.add(haxe.Json.stringify({a: 1}));
		bytes.set(0, 1);
		Sys.println(names.get("a")[0] + values.get(1) + list.first() + buffer.toString() + bytes.get(0) + (date != null) + reg.match("aa"));
	}
}
