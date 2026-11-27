import Store.Tools;

class Script {
	static function main() {
		var store = new Store();
		Sys.println(store.isEmpty());
		Sys.println(store.push(1.5));
		Sys.println(store.push(2.25));
		store[1] = 4.5;
		Sys.println(store[1]);
		Sys.println(store[0] + store[1]);
		Sys.println(store.count());
		Sys.println(store.isEmpty());
		Sys.println(store.describe("values ", true));
		Sys.println(store.describe("", false, 2));
		Sys.println(store.sumWith([10, 20]));
		Sys.println(store.copyTo([0.5]));
		Sys.println(store.inherited());
		Sys.println(store.overridden());
		Sys.println(store.total());
		Sys.println(store.narrow(0.1) == 0.1);
		Sys.println(store.anything("text"));
		Sys.println(store.anything(7));
		Sys.println(store.anything(null));
		try {
			store.fail("thrown in the host");
			Sys.println("not thrown");
		} catch (e:String) {
			Sys.println("caught: " + e);
		}
		Sys.println(store.length);
		Sys.println(store.countAsInterface());
		Sys.println(store.delete(0));
		Sys.println(store.delete(5));
		Sys.println(store.describe("after delete ", true));
		store.clear();
		Sys.println(store.count());

		var filled = Store.filled(3, 0.5);
		Sys.println(filled.describe("filled ", true));

		Sys.println(Tools.twice(21));
		Sys.println(Tools.half(5));
		Sys.println(Tools.shout("quiet"));
		Sys.println(Tools.not(false));
		Sys.println(Tools.calls() % 4);

		// Math is a class of the runtime itself, and has no script functions either.
		// The value is not a constant, or the compiler would work some of these out.
		var value = Std.parseFloat("2.75");
		Sys.println(Math.floor(value));
		Sys.println(Math.round(-value));
		Sys.println(Math.sqrt(value - 0.5));
		Sys.println(Math.max(value, 3));
		Sys.println(Math.isNaN(Math.sqrt(-value)));
	}
}
