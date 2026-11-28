class Script {
	// The closure captures count first, so text and list follow a 4-byte Int
	static function make(n:Int, s:String, values:Array<Int>):Void->String {
		var count = n;
		var text = s + "!";
		var list = values;
		return function() return count + " " + text + " " + list.length + " " + list[0];
	}

	static function main() {
		var ok = true;
		var checked = 0;
		for (round in 0...5) {
			var all = [for (i in 0...20000) make(i, "text" + i, [i, round])];
			// Keep one closure in fifty, so the blocks holding what they captured
			// are mostly garbage and the collector moves them
			var kept = [for (i in 0...all.length) if (i % 50 == 0) all[i]];
			all = null;
			cpp.vm.Gc.run(true);
			// Fill the space the moved objects left
			var refill = [for (i in 0...20000) Std.string(i * round)];
			for (k in 0...kept.length) {
				var i = k * 50;
				if (kept[k]() != '$i text$i! 2 $i')
					ok = false;
				checked++;
			}
			refill = null;
		}
		Sys.println("captures after moving collections: " + ok);
		Sys.println("closures checked: " + checked);
	}
}
