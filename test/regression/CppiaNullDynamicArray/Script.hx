class Script {
	static function attempt(name:String, run:Void->Dynamic) {
		try {
			run();
			Sys.println(name + ": ran");
		} catch (e:Dynamic) {
			Sys.println(name + ": " + e);
		}
	}

	static function main() {
		var a:Array<Dynamic> = null;
		attempt("push", () -> a.push(1));
		attempt("contains", () -> a.contains(1));
		attempt("remove", () -> a.remove(1));
		attempt("indexOf", () -> a.indexOf(1));
		attempt("lastIndexOf", () -> a.lastIndexOf(1));
		attempt("join", () -> a.join(","));
		attempt("toString", () -> a.toString());
		attempt("sort", () -> { a.sort((x, y) -> 0); null; });
		attempt("insert", () -> { a.insert(0, 1); null; });
		attempt("unshift", () -> { a.unshift(1); null; });
		attempt("pop", () -> a.pop());
		attempt("read", () -> a[0]);
	}
}
