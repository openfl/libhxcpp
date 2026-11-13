// Haxe 5's Bytes.ofString kept an array of three bytes for each unit of a
// string that was not all ASCII (openfl/libhxcpp#405). Haxe 4's allocates what
// it needs, which this checks too.
class Main {
	static function main() {
		var correct = true;
		var bounded = true;
		var asciiExact = true;
		var plain = haxe.io.Bytes.alloc(1 << 20);
		plain.fill(0, plain.length, 97);
		var ascii = plain.toString();
		var text = ascii + "\u0100";
		var encoded = haxe.io.Bytes.ofString(text);
		var data = encoded.getData();
		correct = encoded.length == plain.length + 2 && encoded.toString() == text;
		for (_ in 0...3) cpp.vm.Gc.run(true);
		var capacity:Int = untyped __cpp__("{0}->capacity()", data);
		bounded = capacity <= encoded.length + 65536;
		var control = haxe.io.Bytes.ofString(ascii);
		var controlData = control.getData();
		var controlCapacity:Int = untyped __cpp__("{0}->capacity()", controlData);
		asciiExact = controlCapacity <= control.length + 65536 && control.toString() == ascii;
		if (Sys.args().length > 0)
			Sys.println(haxe.Json.stringify({length: encoded.length, capacity: capacity, asciiLength: control.length, asciiCapacity: controlCapacity}));
		Sys.println("UTF8 encoding preserves the complete payload: " + correct);
		Sys.println("retained UTF8 storage stays close to the output length: " + bounded);
		Sys.println("ASCII control storage stays close to the output length: " + asciiExact);
	}
}
