// Haxe 5 gives Int64s a ValueType of their own; either way it must not be TInt
function typeName(v:Dynamic):String {
	var name = Std.string(Type.typeof(v));
	return name == "TInt64" ? "TClass(cpp::Int64)" : name;
}

function main() {
	var small:Dynamic = haxe.Int64.ofInt(5);
	var big:Dynamic = haxe.Int64.make(1, 0);
	Sys.println(typeName(big));
	Sys.println(typeName(small));
}
