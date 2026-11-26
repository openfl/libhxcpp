class Key {
	public function new() {}
}

class Script {
	static function check(name:String, d:Dynamic) {
		Sys.println(name + " " + Std.isOfType(d, Bool) + " " + (d == true) + " " + Std.string(d));
	}

	static function main() {
		var yes = true;
		var no = false;

		// The standard library gives scripts a setBool on each of these, which
		// calls the Int setter (HaxeFoundation/haxe#13246)
		var byString = new Map<String, Bool>();
		byString.set("yes", yes);
		byString.set("no", no);
		check("string", byString.get("yes"));
		check("string false", byString.get("no"));
		Sys.println("string compared " + (byString.get("yes") == true) + " " + (byString.get("no") == false));
		var typed:Bool = byString.get("yes");
		var typedFalse:Bool = byString.get("no");
		Sys.println("string typed " + typed + " " + typedFalse);
		Sys.println("string missing " + byString.get("other"));
		for (value in byString) {
			if (value) {
				check("string iterated", value);
			}
		}

		var one = new Map<String, Bool>();
		one.set("a", yes);
		Sys.println("string toString " + one.toString());
		one.set("a", no);
		check("string overwritten", one.get("a"));
		one["a"] = yes;
		check("string index", one["a"]);
		check("string literal", ["a" => true].get("a"));

		var byInt = new Map<Int, Bool>();
		byInt.set(1, yes);
		byInt.set(2, no);
		check("int", byInt.get(1));
		check("int false", byInt.get(2));
		Sys.println("int compared " + (byInt.get(1) == true) + " " + (byInt.get(2) == false));
		var typedInt:Bool = byInt.get(1);
		Sys.println("int typed " + typedInt);
		Sys.println("int toString " + [7 => true].toString());

		var key = new Key();
		var byObject = new Map<Key, Bool>();
		byObject.set(key, yes);
		check("object", byObject.get(key));
		Sys.println("object compared " + (byObject.get(key) == true));
		var typedObject:Bool = byObject.get(key);
		Sys.println("object typed " + typedObject);
		byObject.set(key, no);
		check("object false", byObject.get(key));

		// A map of any value takes a Bool the same way, beside values that
		// must stay what they are
		var mixed = new Map<String, Dynamic>();
		mixed.set("bool", yes);
		mixed.set("int", 1);
		mixed.set("zero", 0);
		mixed.set("float", 1.5);
		mixed.set("string", "s");
		check("mixed bool", mixed.get("bool"));
		check("mixed int", mixed.get("int"));
		check("mixed zero", mixed.get("zero"));
		check("mixed float", mixed.get("float"));
		check("mixed string", mixed.get("string"));

		var nullable = new Map<String, Null<Bool>>();
		nullable.set("a", yes);
		check("nullable", nullable.get("a"));

		// Ints go on being Ints
		var ints = new Map<String, Int>();
		ints.set("one", 1);
		ints.set("zero", 0);
		check("int value", ints.get("one"));
		check("int value zero", ints.get("zero"));
		var intKeys = new Map<Int, Int>();
		intKeys.set(1, 1);
		check("int key int value", intKeys.get(1));
	}
}
