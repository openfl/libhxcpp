// A public abstract over a private class, as openfl.Vector is over the classes
// that hold its values. The inline functions put calls to the private class
// into a script that is compiled against this file.
abstract Store(StoreImpl) {
	public inline function new() {
		this = new StoreImpl();
	}

	@:arrayAccess public inline function get(index:Int):Float {
		return this.get(index);
	}

	@:arrayAccess public inline function set(index:Int, value:Float):Float {
		return this.set(index, value);
	}

	public inline function push(value:Float):Int {
		return this.push(value);
	}

	public inline function count():Int {
		return this.count();
	}

	public var length(get, never):Int;

	inline function get_length():Int {
		return this.length;
	}

	// delete is a C++ keyword, so the function has another name in C++
	public inline function delete(index:Int):Bool {
		return this.delete(index);
	}

	// A private interface has no typed entries: this call is still made by name
	public inline function countAsInterface():Int {
		var counted:Counted = this;
		return counted.count();
	}

	public inline function isEmpty():Bool {
		return this.isEmpty();
	}

	public inline function clear():Void {
		this.clear();
	}

	public inline function describe(prefix:String, brackets:Bool, scale:Float = 1):String {
		return this.describe(prefix, brackets, scale);
	}

	public inline function sumWith(other:Array<Float>):Float {
		return this.sumWith(other);
	}

	public inline function copyTo(other:Array<Float>):Array<Float> {
		return this.copyTo(other);
	}

	public inline function inherited():String {
		return this.inherited();
	}

	public inline function overridden():String {
		return this.overridden();
	}

	public inline function fail(message:String):Int {
		return this.fail(message);
	}

	public inline function narrow(value:Single):Single {
		return this.narrow(value);
	}

	public inline function anything(value:Dynamic):Dynamic {
		return this.anything(value);
	}

	// Not inline, so a static function of the class behind the abstract
	public function total():Float {
		var sum = 0.0;
		for (i in 0...this.count()) {
			sum += this.get(i);
		}
		return sum;
	}

	public static function filled(length:Int, value:Float):Store {
		var store = new Store();
		for (i in 0...length) {
			store.push(value);
		}
		return store;
	}
}

private class StoreBase {
	public function new() {}

	public function inherited():String {
		return "from the base";
	}

	public function overridden():String {
		return "base";
	}
}

private interface Counted {
	function count():Int;
}

private class StoreImpl extends StoreBase implements Counted {
	var values:Array<Float>;

	public var length(get, never):Int;

	public function new() {
		super();
		values = [];
	}

	public function get(index:Int):Float {
		return values[index];
	}

	public function set(index:Int, value:Float):Float {
		return values[index] = value;
	}

	public function push(value:Float):Int {
		return values.push(value);
	}

	public function count():Int {
		return values.length;
	}

	public function isEmpty():Bool {
		return values.length == 0;
	}

	function get_length():Int {
		return values.length;
	}

	public function delete(index:Int):Bool {
		if (index < 0 || index >= values.length) {
			return false;
		}
		values.splice(index, 1);
		return true;
	}

	public function clear():Void {
		values = [];
	}

	public function describe(prefix:String, brackets:Bool, scale:Float = 1):String {
		var text = [for (value in values) Std.string(value * scale)].join(",");
		return prefix + (brackets ? "[" + text + "]" : text);
	}

	public function sumWith(other:Array<Float>):Float {
		var sum = 0.0;
		for (value in values) {
			sum += value;
		}
		for (value in other) {
			sum += value;
		}
		return sum;
	}

	public function copyTo(other:Array<Float>):Array<Float> {
		for (value in values) {
			other.push(value);
		}
		return other;
	}

	override public function overridden():String {
		return "store";
	}

	public function fail(message:String):Int {
		throw message;
	}

	public function narrow(value:Single):Single {
		return value;
	}

	public function anything(value:Dynamic):Dynamic {
		return value;
	}
}

// A public class whose inline functions lead to a private class's statics
class Tools {
	public static inline function twice(value:Int):Int {
		return Hidden.twice(value);
	}

	public static inline function half(value:Float):Float {
		return Hidden.half(value);
	}

	public static inline function shout(value:String):String {
		return Hidden.shout(value);
	}

	public static inline function not(value:Bool):Bool {
		return Hidden.not(value);
	}

	public static inline function calls():Int {
		return Hidden.calls;
	}
}

private class Hidden {
	public static var calls = 0;

	public static function twice(value:Int):Int {
		calls++;
		return value * 2;
	}

	public static function half(value:Float):Float {
		calls++;
		return value / 2;
	}

	public static function shout(value:String):String {
		calls++;
		return value.toUpperCase() + "!";
	}

	public static function not(value:Bool):Bool {
		calls++;
		return !value;
	}
}
