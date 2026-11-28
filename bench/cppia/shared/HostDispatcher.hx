package;

/** An event dispatcher of the host, as OpenFL's EventDispatcher is: listeners by type, called in order. **/
@:keep
class HostDispatcher {
	var listeners:Map<String, Array<HostEvent->Void>> = null;

	public function new() {}

	public function addEventListener(type:String, listener:HostEvent->Void):Void {
		if (listeners == null)
			listeners = new Map();
		var list = listeners.get(type);
		if (list == null)
			listeners.set(type, list = []);
		list.push(listener);
	}

	public function hasEventListener(type:String):Bool {
		return listeners != null && listeners.exists(type);
	}

	public function dispatchEvent(event:HostEvent):Bool {
		if (listeners == null)
			return false;
		var list = listeners.get(event.type);
		if (list == null)
			return false;
		event.target = this;
		for (i in 0...list.length)
			list[i](event);
		return true;
	}
}
