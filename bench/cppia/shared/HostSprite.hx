package;

/**
	A display object of the host, as OpenFL's DisplayObject is to a script:
	Float properties whose setters mark it changed. Script classes extend it.
**/
@:keep
class HostSprite {
	public var x(get, set):Float;
	public var y(get, set):Float;
	public var changes:Int = 0;

	var _x:Float = 0;
	var _y:Float = 0;

	public function new() {}

	function get_x():Float
		return _x;

	function set_x(value:Float):Float {
		if (value != _x) {
			_x = value;
			changes++;
		}
		return value;
	}

	function get_y():Float
		return _y;

	function set_y(value:Float):Float {
		if (value != _y) {
			_y = value;
			changes++;
		}
		return value;
	}
}
