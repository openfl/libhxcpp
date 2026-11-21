class Script {
	public static var objects:Array<Dynamic> = [];

	static function main() {
		for (i in 0...2000)
			objects.push(Type.createInstance(Type.resolveClass('C$i'), []));
	}
}
