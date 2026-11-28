package;

/**
	A display object container of the host, as OpenFL's DisplayObjectContainer
	is, and the frame loop a stage runs over it: ENTER_FRAME to every object
	with a listener, then a render that reads each object's transform.
**/
@:keep
class HostContainer extends HostDisplayObject {
	var children:Array<HostDisplayObject> = [];

	public var numChildren(get, never):Int;

	public function new() {
		super();
	}

	function get_numChildren():Int
		return children.length;

	public function addChild(child:HostDisplayObject):HostDisplayObject {
		if (child.parent != null)
			child.parent.removeChild(child);
		@:privateAccess child.parent = this;
		children.push(child);
		return child;
	}

	public function removeChild(child:HostDisplayObject):HostDisplayObject {
		if (children.remove(child))
			@:privateAccess child.parent = null;
		return child;
	}

	public function getChildAt(index:Int):HostDisplayObject
		return children[index];

	/** Runs frames as a stage does, and returns what the renders read. **/
	@:noInline
	public static function runFrames(root:HostContainer, frames:Int):Int {
		var sum = 0;
		for (f in 0...frames) {
			broadcast(root, new HostEvent("enterFrame"));
			sum = (sum * 31 + render(root)) | 0;
		}
		return sum;
	}

	static function broadcast(object:HostDisplayObject, event:HostEvent) {
		if (object.hasEventListener(event.type))
			object.dispatchEvent(event);
		if (Std.isOfType(object, HostContainer)) {
			var container:HostContainer = cast object;
			for (i in 0...container.children.length)
				broadcast(container.children[i], event);
		}
	}

	static function render(object:HostDisplayObject):Int {
		var sum = object.renderValue();
		if (Std.isOfType(object, HostContainer)) {
			var container:HostContainer = cast object;
			for (i in 0...container.children.length)
				sum += render(container.children[i]);
		}
		return sum;
	}
}
