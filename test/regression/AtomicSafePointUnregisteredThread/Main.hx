import haxe.atomic.AtomicInt;

// Haxe 4's AtomicInt operations stop at a pending collection (libhxcpp #455),
// through __hxcpp_gc_safe_point. A native thread the collector never
// registered, such as an audio callback thread reading a flag, has no
// allocator to stop: it ended the process with "Bad local allocator" while
// another thread collected. It is not a thread the collector waits for, so the
// safe point now does nothing there.
@:cppFileCode('
#include <thread>
#include <atomic>
static std::atomic<bool> sStop(false);
static std::atomic<int> sReads(0);
static std::thread *sNative = 0;

static void startNative(::cpp::Pointer<int> inFlag)
{
	sNative = new std::thread([inFlag]() {
		while (!sStop.load())
		{
			_hx_atomic_load(inFlag);
			sReads.fetch_add(1);
		}
	});
}

static int stopNative()
{
	sStop.store(true);
	sNative->join();
	return sReads.load() > 0 ? 1 : 0;
}
')
class Main {
	static var flag:AtomicInt;

	static function main() {
		flag = new AtomicInt(1);
		var pointer:cpp.Pointer<Int> = cast flag;
		untyped __cpp__("startNative({0})", pointer);
		for (i in 0...50) {
			var junk = [for (j in 0...2000) [j]];
			cpp.vm.Gc.run(i % 5 == 0);
		}
		var read:Int = untyped __cpp__("stopNative()");
		Sys.println('the native thread read the atomic through 50 collections: ${read == 1}');
	}
}
