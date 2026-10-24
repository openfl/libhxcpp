class Main {
	static function make():Array<Dynamic> {
		var result:Array<Dynamic> = [];
		for (i in 0...1024) result.push(i);
		return result;
	}
	static function storage(array:Array<Dynamic>):Int {
		return untyped __cpp__("(int){0}->store", array);
	}
	static function main() {
		for (operation in ["contains", "indexOf", "lastIndexOf", "remove"]) {
			var array = make();
			var before = storage(array);
			var correct = switch (operation) {
				case "contains": !array.contains("missing");
				case "indexOf": array.indexOf("missing") == -1;
				case "lastIndexOf": array.lastIndexOf("missing") == -1;
				case "remove": !array.remove("missing");
				default: false;
			};
			Sys.println(operation + " preserves storage: " + (storage(array) == before));
			if (!correct || array.length != 1024 || array[1023] != 1023) throw "Lookup changed values";
		}
				var numeric = make();
		var beforeType = Type.typeof(numeric[1000]);
		var missed = numeric.indexOf(1.5) == -1;
		Sys.println("missing Float search preserves element type: " + (missed && Type.enumEq(Type.typeof(numeric[1000]), beforeType)));
        Sys.println("lookup results and values preserved: true");
	}
}
