class HostStatics {
	public static var obj:Dynamic;
	public static var items:Array<Int> = [0];
	public static var fn:Void->Int;
	public static var node:HostNode;
	public static var count:Int = 0;

	public static function check():Bool {
		return (obj : HostNode).value == -15000 && fn() == 15000 && items[1] == 15001 && node.value == 15000 && count == 4;
	}
}
