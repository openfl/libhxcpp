// Classes that take their constructor from above: Middle from Top, a script
// class, and HostMiddle from Main's HostBase, a host class
class Top {
	public var made:String;

	public function new(n:Int) {
		made = 'Top $n';
	}
}

class Middle extends Top {}

class Bottom extends Middle {
	public function new() {
		super(1);
		made += " then Bottom";
	}
}

class HostMiddle extends HostBase {}

class HostBottom extends HostMiddle {
	public function new() {
		super(2);
		made += " then HostBottom";
	}
}

class Script {
	static function main() {
		Sys.println(new Bottom().made);
		Sys.println(new Middle(3).made);
		Sys.println(new HostBottom().made);
		Sys.println(new HostMiddle(4).made);
	}
}
