import haxe.atomic.AtomicInt;

class Script {
	static function main() {
		final a = new AtomicInt(1);
		Sys.println([a.add(2), a.compareExchange(5, 9), a.compareExchange(3, 9), a.load()]);
	}
}
