class Script {
	// Each default follows an Int, so it sits 4 bytes past an 8-byte boundary
	static function str(a:Int, ?b:String = "default") return '$a $b';
	static function dyn(a:Int, ?b:Dynamic = 5) return '$a $b';
	static function nullFloat(a:Int, ?b:Float = 1.5) return '$a $b';
	static function float(a:Int, b:Float = 1.5) return '$a $b';
	static function int(a:Int, b:Int = 7) return '$a $b';
	static function bool(a:Int, b:Bool = true) return '$a $b';

	static function main() {
		Sys.println("str: " + str(1) + ", " + str(2, "given"));
		Sys.println("dyn: " + dyn(1) + ", " + dyn(2, "given"));
		Sys.println("nullFloat: " + nullFloat(1) + ", " + nullFloat(2, 2.25));
		Sys.println("float: " + float(1) + ", " + float(2, 2.25));
		Sys.println("int: " + int(1) + ", " + int(2, 9));
		Sys.println("bool: " + bool(1) + ", " + bool(2, false));
	}
}
