// The JIT evaluated the receiver of resize() and cpp.NativeArray.setSize() on
// an Array<Dynamic> twice, so a receiver with side effects ran twice, and the
// array it returned the second time was the one resized.
class Script {
	static var calls = 0;
	static var first:Array<Dynamic>;
	static var second:Array<Dynamic>;

	static function receiver():Array<Dynamic> {
		calls++;
		return calls == 1 ? first : second;
	}

	static function reset() {
		calls = 0;
		first = [1, "two", 3];
		second = [4, "five", 6];
	}

	static function main() {
		reset();
		receiver().resize(1);
		Sys.println('resize: calls=$calls first=$first second=$second');
		reset();
		cpp.NativeArray.setSize(receiver(), 1);
		Sys.println('setSize: calls=$calls first=$first second=$second');
	}
}
