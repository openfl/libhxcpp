#if !emscripten
@:cppFileCode('
#include <thread>
#include <hx/Native.h>
static void runJoined(Dynamic callback) {
	std::thread worker([callback]() mutable { hx::NativeAttach attach; callback(); });
	hx::AutoGCFreeZone freeZone;
	worker.join();
}
')
#end
class Main {
	static var arrays:Array<Array<Int>>;
	static function allocate():Void {
		arrays = [];
		for (i in 0...2048) {
			var values:Array<Int> = [];
			values.resize(256);
			values[0] = i;
			values[255] = -i;
			arrays.push(values);
		}
	}
	static function main() {
		var current = true;
		var collected = true;
		var intact = true;
		#if !emscripten
		for (_ in 0...3) cpp.vm.Gc.run(true);
		var before = cpp.vm.Gc.memInfo64(cpp.vm.Gc.MEM_INFO_CURRENT);
		var liveBefore = cpp.vm.Gc.memInfo64(4);
		cpp.vm.Gc.enable(false);
		untyped __cpp__("runJoined({0})", allocate);
		var after = cpp.vm.Gc.memInfo64(cpp.vm.Gc.MEM_INFO_CURRENT);
		cpp.vm.Gc.enable(true);
		var payload = 2048.0 * 256 * 4;
		current = after - before >= payload;
		cpp.vm.Gc.run(true);
		var liveAfter = cpp.vm.Gc.memInfo64(4);
		var refreshed = cpp.vm.Gc.memInfo64(cpp.vm.Gc.MEM_INFO_CURRENT);
		collected = liveAfter - liveBefore >= payload && refreshed - before >= payload;
		for (i in 0...arrays.length)
			if (arrays[i][0] != i || arrays[i][255] != -i) intact = false;
		if (Sys.args().length > 0)
			Sys.println(haxe.Json.stringify({payload: payload, before: before, after: after, afterCollection: refreshed, liveGrowth: liveAfter - liveBefore}));
		#end
		Sys.println("current heap estimate includes the newly allocated payload: " + current);
		Sys.println("full collection accounts for the retained payload: " + collected);
		Sys.println("all retained arrays remain intact: " + intact);
	}
}
