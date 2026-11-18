import cpp.vm.Gc;
import haxe.ds.WeakMap;

class Key {
	public var a:Int;
	public var b:Int;
	public var dead = false;

	public function new(i:Int) {
		a = i;
		b = i;
	}

	public function toString() {
		if (dead)
			Main.deadReached++;
		return "k";
	}
}

// The size of a Key, to take the memory of a freed one
class Filler {
	public var a = 0;
	public var b = 0;
	public var c = false;

	public function new() {}

	public function toString() {
		Main.fillerReached++;
		return "f";
	}
}

class Val {
	public function new() {}

	// Lets go of every key and collects, partway through the map's toString
	public function toString() {
		if (++Main.visits == Main.collectAt) {
			for (i in 0...Main.held.length)
				Main.held[i] = null;
			Gc.run(true);
			for (i in 0...20000)
				Main.fillers.push(new Filler());
		}
		return "v";
	}
}

// A collection during a weak map's toString, which allocates as it goes,
// unlinked the entries whose keys had died, and the walk went on from its
// entry's next, which still led to them: it called toString on keys the
// collector had finalized and freed, and on whatever took their memory. A
// weak map now holds its keys and values while a walk is on them.
class Main {
	public static var deadReached = 0;
	public static var fillerReached = 0;
	public static var visits = 0;
	public static var collectAt = 0;
	public static var held:Array<Key> = [];
	public static var fillers:Array<Filler> = [];
	static var finalized = 0;

	static function onDead(k:Key) {
		k.dead = true;
		finalized++;
	}

	static function fill(map:WeakMap<Key, Val>, n:Int) {
		held = [];
		for (i in 0...n) {
			var k = new Key(i);
			Gc.setFinalizer(k, cpp.Callable.fromStaticFunction(onDead));
			held.push(k);
			map.set(k, new Val());
		}
	}

	static function walk(at:Int) {
		var map = new WeakMap<Key, Val>();
		fill(map, 64);
		visits = 0;
		collectAt = at;
		map.toString();
		fillers = [];
	}

	// Once the walk is over the keys go as before
	static function keysGoAfterWalk():Bool {
		var map = new WeakMap<Key, Val>();
		fill(map, 64);
		collectAt = -1;
		map.toString();
		for (i in 0...held.length)
			held[i] = null;
		finalized = 0;
		Gc.run(true);
		Gc.run(true);
		return finalized > 0;
	}

	static function main() {
		for (t in 0...200)
			walk(1 + t % 32);
		Sys.println('walk reached $deadReached dead keys and $fillerReached other objects');
		Sys.println('keys collected after the walk: ${keysGoAfterWalk()}');
	}
}
