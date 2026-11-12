function main() {
	var div = cpp.NativeMath.idiv;
	var dyn:Dynamic = cpp.NativeMath.idiv;
	var fns = [cpp.NativeMath.idiv, cpp.NativeMath.imod];
	Sys.println(div(7, 2));
	Sys.println(dyn(7, 2));
	Sys.println(Reflect.callMethod(null, dyn, [9, 2]));
	Sys.println([for (f in fns) f(17, 5)].join(" "));
	Sys.println(Reflect.isFunction(dyn));
	var compact = cpp.NativeGc.compact;
	compact();
	var memInfo:Dynamic = cpp.NativeGc.memInfo;
	Sys.println(memInfo(0) > 0);
}
