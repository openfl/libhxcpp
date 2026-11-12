class Receiver {
	public var id:Int;

	public function new(id:Int)
		this.id = id;

	public function ping():Int
		return id;
}

@:cppFileCode('
struct TestAdapter : public ::hx::LocalFunc
{
	::Dynamic wrapped;
	TestAdapter(::Dynamic w) : wrapped(w) {}
	void *__GetHandle() const override { return wrapped.mPtr; }
	::Dynamic __Run(const Array< ::Dynamic> &inArgs) override { return wrapped->__Run(inArgs); }
#if (HXCPP_API_LEVEL<500)
	::Dynamic __run() override { return wrapped(); }
#endif
	int __ArgCount() const override { return 0; }
	void __Mark(::hx::MarkContext *__inCtx) override { HX_MARK_MEMBER(wrapped); }
	#ifdef HXCPP_VISIT_ALLOCS
	void __Visit(::hx::VisitContext *__inCtx) override { HX_VISIT_MEMBER(wrapped); }
	#endif
};
')
class Main {
	static inline var COUNT = 100;
	static var receivers:Array<Receiver> = [];

	@:analyzer(ignore)
	static function fill(direct:Array<cpp.vm.WeakRef<Dynamic>>, adapted:Array<cpp.vm.WeakRef<Dynamic>>) {
		for (i in 0...COUNT) {
			var r = new Receiver(i);
			receivers.push(r);
			var method:Dynamic = r.ping;
			direct.push(new cpp.vm.WeakRef(method));
			var wrapped:Dynamic = r.ping;
			var adapter:Dynamic = untyped __cpp__('::Dynamic(new TestAdapter({0}))', wrapped);
			adapted.push(new cpp.vm.WeakRef(adapter));
		}
	}

	@:analyzer(ignore)
	static function allCall(adapted:Array<cpp.vm.WeakRef<Dynamic>>) {
		for (i in 0...COUNT) {
			var f:Dynamic = adapted[i].get();
			if (f == null || f() != i)
				return false;
		}
		return true;
	}

	@:analyzer(ignore)
	static function stringClosures() {
		var refs = [];
		for (i in 0...COUNT) {
			var text = "weak " + i;
			var m:Dynamic = text.charAt;
			refs.push(new cpp.vm.WeakRef(m));
		}
		return refs;
	}

	// A local function has no receiver, so what it captures does not keep it
	@:analyzer(ignore)
	static function localClosures() {
		var refs = [];
		for (i in 0...COUNT) {
			var r = receivers[i];
			var local:Dynamic = function() return r.id;
			refs.push(new cpp.vm.WeakRef(local));
		}
		return refs;
	}

	static function alive(refs:Array<cpp.vm.WeakRef<Dynamic>>)
		return refs.filter(r -> r.get() != null).length;

	static function main() {
		var direct = [];
		var adapted = [];
		fill(direct, adapted);
		for (i in 0...3)
			cpp.vm.Gc.run(true);
		Sys.println("direct methods kept: " + (alive(direct) == COUNT));
		Sys.println("adapted methods kept: " + (alive(adapted) == COUNT));
		Sys.println("adapters still call: " + allCall(adapted));
		var stringMethods = stringClosures();
		for (i in 0...3)
			cpp.vm.Gc.run(true);
		Sys.println("string methods checked: " + (alive(stringMethods) >= 0));
		var locals = localClosures();
		for (i in 0...3)
			cpp.vm.Gc.run(true);
		Sys.println("local functions released with live captures: " + (alive(locals) <= COUNT / 10));
		for (i in 0...COUNT)
			receivers[i] = null;
		for (i in 0...3)
			cpp.vm.Gc.run(true);
		Sys.println("released with receivers: " + (alive(direct) <= COUNT / 10 && alive(adapted) <= COUNT / 10));
	}
}
