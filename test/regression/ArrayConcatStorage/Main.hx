class Main {
	static function make(values:Array<Int>):Array<Dynamic> {
		var result:Array<Dynamic>=[];
		for(value in values) result.push(value);
		return result;
	}
	static function main() {
		var source=make([1,2,3]);
		var typedTail:Array<Int>=[4,5];
		var combined=source.concat(typedTail);
		Sys.println("concat values: "+(combined.join(",")=="1,2,3,4,5"));
		Sys.println("source remains unchanged: "+(source.join(",")=="1,2,3"));
		source.push("later");
		Sys.println("source accepts later string: "+(source[3]=="later"));
		combined.push("combined");
		Sys.println("result accepts later string: "+(combined[5]=="combined"));
		var ints=make([1000,2000,3000]);
		var floats:Array<Dynamic>=[]; floats.push(1.5);
		ints.concat(floats);
		Sys.println("concat preserves source element type: "+(Type.typeof(ints[0])==TInt));
	}
}