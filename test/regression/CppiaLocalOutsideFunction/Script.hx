// Initial values that declare locals, which the compiler moves into the
// class's init function
class Script {
	static var block = {
		var a = Std.random(1) + 3;
		a * 2;
	};
	static var list = [for (i in 0...Std.random(1) + 3) i * 2];
	static var caught = try {
		throw "x";
		1;
	} catch (e:String) {
		e.length + 1;
	};
	static var twice = function(x:Int) {
		var y = x + 1;
		return y * 2;
	};
	static var fromInit:String;

	// A class's __init__ is not a function, but it has a frame for its locals
	// (HaxeFoundation/haxe's unit.issues.Issue7499 declares one there)
	static function __init__() {
		var unused:String;
		var name = "init";
		var count = Std.random(1) + 2;
		var join = () -> name + count;
		fromInit = join();
	}

	static function main() {
		Sys.println('block $block, list $list, caught $caught, twice ${twice(2)}, init $fromInit');
	}
}
