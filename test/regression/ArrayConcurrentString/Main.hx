class Main {
	static function main() {
		var small = [1];
		var expected = Std.string(small);
		var inside = new sys.thread.Lock();
		var resume = new sys.thread.Lock();
		var finished = new sys.thread.Lock();
		var leaf:Dynamic = {toString: () -> {
			inside.release();
			resume.wait(5);
			return "leaf";
		}};
		var deep:Array<Dynamic> = [[[[[leaf]]]]];
		sys.thread.Thread.create(() -> {
			Std.string(deep);
			finished.release();
		});
		var reached = inside.wait(5);
		var during = Std.string(small);
		resume.release();
		var joined = finished.wait(5);
		Sys.println("worker reached the leaf: " + reached);
		Sys.println("other thread keeps its array string: " + (during == expected));
		Sys.println("worker completed: " + joined);
		var throwing:Array<Dynamic> = [{toString: () -> throw "no"}];
		for (i in 0...6) try Std.string(throwing) catch (_:Dynamic) {}
		Sys.println("depth restored after exceptions: " + (Std.string(small) == expected));
	}
}
