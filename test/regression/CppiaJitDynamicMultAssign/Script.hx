class Script {
	static function five():Dynamic {
		return 5;
	}

	static function main() {
		// x *= y on a Dynamic: the jit computed y into a float register, then
		// converted x with a native call, which can change that register. On
		// x86-64 Windows it did, and 3 *= 5 gave 9
		var x:Dynamic = 3;
		x *= 5;
		Sys.println("int " + x);
		var f:Dynamic = 3.5;
		f *= 2;
		Sys.println("float " + f);
		var c:Dynamic = 3;
		c *= five();
		Sys.println("call " + c);
		var o:{v:Dynamic} = {v: 4};
		o.v *= 2.5;
		Sys.println("field " + o.v);
		var a:Array<Dynamic> = [6];
		a[0] *= 7;
		Sys.println("array " + a[0]);
		var w:Dynamic = 3;
		w = w * 5;
		Sys.println("binary " + w);
	}
}
