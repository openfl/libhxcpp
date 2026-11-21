// libhxcpp #462. A script interface's first method that the host does not
// know got the interface slot of the host's last interface method, which in
// this host is IMap's clear, so a class implementing both had fooOnly and
// clear in one slot, and calls through one interface ran the other's method.
interface IFoo {
	function fooOnly():String;
}

interface IBar {
	function barOnly():String;
}

class Both implements IFoo implements IBar implements haxe.Constraints.IMap<String, Int> {
	public var log = "";

	public function new() {}

	public function fooOnly():String
		return "fooOnly";

	public function barOnly():String
		return "barOnly";

	public function get(k:String):Null<Int>
		return 1;

	public function set(k:String, v:Int):Void {}

	public function exists(k:String):Bool
		return true;

	public function remove(k:String):Bool
		return false;

	public function keys():Iterator<String>
		return [].iterator();

	public function iterator():Iterator<Int>
		return [].iterator();

	public function keyValueIterator():KeyValueIterator<String, Int>
		return null;

	public function copy():haxe.Constraints.IMap<String, Int>
		return this;

	public function toString():String
		return "Both";

	public function clear():Void {
		log += "clear ";
	}
}

class Script {
	static function main() {
		var b = new Both();
		var f:IFoo = b;
		var g:IBar = b;
		var m:haxe.Constraints.IMap<String, Int> = b;
		Sys.println("IFoo.fooOnly: " + f.fooOnly());
		Sys.println("IBar.barOnly: " + g.barOnly());
		m.clear();
		Sys.println("after IMap.clear: " + b.log);
		Sys.println("IMap.exists: " + m.exists("k"));
	}
}
