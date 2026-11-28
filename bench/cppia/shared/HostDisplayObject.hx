package;

/**
	A display object of the host, as OpenFL's DisplayObject is: transform
	properties whose setters mark it for the next render, in a container.
**/
@:keep
class HostDisplayObject extends HostDispatcher {
	public var x(get, set):Float;
	public var y(get, set):Float;
	public var rotation(get, set):Float;
	public var alpha(get, set):Float;
	public var parent(default, null):HostContainer;

	var _x:Float = 0;
	var _y:Float = 0;
	var _rotation:Float = 0;
	var _alpha:Float = 1;
	var transformDirty = false;

	public function new() {
		super();
	}

	function get_x():Float
		return _x;

	function set_x(value:Float):Float {
		if (value != _x) {
			_x = value;
			transformDirty = true;
		}
		return value;
	}

	function get_y():Float
		return _y;

	function set_y(value:Float):Float {
		if (value != _y) {
			_y = value;
			transformDirty = true;
		}
		return value;
	}

	function get_rotation():Float
		return _rotation;

	function set_rotation(value:Float):Float {
		if (value != _rotation) {
			_rotation = value;
			transformDirty = true;
		}
		return value;
	}

	function get_alpha():Float
		return _alpha;

	function set_alpha(value:Float):Float {
		_alpha = value;
		return value;
	}

	/** What a render reads of it, and clears. **/
	public function renderValue():Int {
		var v = Std.int(_x) + Std.int(_y) * 3 + Std.int(_rotation) * 7 + Std.int(_alpha * 100) + (transformDirty ? 1 : 0);
		transformDirty = false;
		return v;
	}
}
