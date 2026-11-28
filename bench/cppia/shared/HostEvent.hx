package;

/** An event of the host, as OpenFL's Event is to a script. **/
@:keep
class HostEvent {
	public var type:String;
	public var target:HostDispatcher;

	public function new(type:String) {
		this.type = type;
	}
}
