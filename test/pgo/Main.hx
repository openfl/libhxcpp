class Main {
	static function main() {
		var n = Std.parseInt(Sys.args()[0] == null ? "100000" : Sys.args()[0]);
		var counts = new Map<Int, Int>();
		for (i in 0...n) {
			var key = (i * 7919) % 10007;
			counts.set(key, (counts.exists(key) ? counts.get(key) : 0) + 1);
		}
		var total = 0;
		for (count in counts)
			total += count;
		Sys.println('total=$total');
	}
}
