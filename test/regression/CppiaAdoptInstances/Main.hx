import cpp.cppia.Host;
import cpp.cppia.Module;

// A module's adoptInstances(previous) moves the live instances of each of the
// previous module's classes onto the class of the same name, when the two lay
// out their fields the same way, so their calls run the new code. Instances of
// a class whose fields changed stay, and the report says why. A method bound
// before stays on the old code and keeps the old module loaded until it is
// dropped. openfl/libhxcpp#265
//
// Every call into script code runs on a joined native worker, so stale values
// it leaves on a stack do not decide what keeps a module loaded. The main
// thread only collects and prints.
@:cppFileCode('
#include <thread>
#include <hx/Native.h>
static void runJoined(Dynamic callback) {
	std::thread worker([callback]() mutable { hx::NativeAttach attach; callback(); });
	hx::AutoGCFreeZone freeZone;
	worker.join();
}
')
class Main {
	static var dir:String;
	static var previous:Module;
	static var next:Module;
	static var counter:Dynamic;
	static var doubler:Dynamic;
	static var reshaped:Dynamic;
	static var bound:Void->Int;
	static var lines:Array<String>;

	static function load(name:String):Module {
		var module = Module.fromData(sys.io.File.getBytes(haxe.io.Path.join([dir, name])).getData());
		module.boot();
		module.run();
		return module;
	}

	static function loadOld() {
		previous = load("v1.cppia");
		counter = Type.createInstance(previous.resolveClass("Counter"), [5]);
		counter.step();
		doubler = Type.createInstance(previous.resolveClass("Doubler"), [0]);
		reshaped = Type.createInstance(previous.resolveClass("Reshaped"), []);
		bound = Reflect.field(counter, "step");
	}

	static function adopt() {
		next = load("v2.cppia");
		var report:Array<Dynamic> = untyped next.adoptInstances(previous);
		untyped previous.release();
		previous = null;
		var rows = [for (r in report) '${r.name} moved ${r.moved}' + (r.reason == null ? "" : ': ${r.reason}')];
		rows.sort(Reflect.compare);
		lines.push('report: ${rows.join(", ")}');
	}

	static function useMoved() {
		// Moved, with their fields as they were
		lines.push('counter runs the new code: ${counter.step() == 16 && counter.version() == "v2 counter"}');
		lines.push('the subclass runs its new override: ${doubler.step() == 20 && doubler.version() == "v2 counter"}');
		var stepper:Stepper = counter;
		lines.push('the host interface reaches the new code: ${stepper.step() == 26}');
		var counterClass = next.resolveClass("Counter");
		lines.push('they are of the new classes: ${Type.getClass(counter) == counterClass && Std.isOfType(counter, counterClass) && Std.isOfType(doubler, counterClass) && Type.getClass(doubler) == next.resolveClass("Doubler")}');
	}

	static function useOld() {
		// Stayed
		lines.push('the reshaped instance keeps its class: ${reshaped.version() == "v1"}');
		lines.push('the bound method keeps the old code: ${bound() == 27 && counter.step() == 37}');
	}

	static function dropOld() {
		bound = null;
		reshaped = null;
	}

	static function dropAll() {
		counter = doubler = null;
		untyped next.release();
		next = null;
	}

	static function retained():String {
		var modules:Array<Dynamic> = untyped __global__.__scriptable_cppia_retained();
		return [
			for (m in modules)
				[for (i in (m.instances : Array<Dynamic>)) '${i.name} x${i.count}'].concat(['closures x${m.closures.count}']).join(", ")
		].join("; ");
	}

	static function main() {
		dir = haxe.io.Path.directory(Sys.programPath());
		for (jit in [false, true]) {
			Host.enableJit(jit);
			lines = [];
			untyped __cpp__("runJoined({0})", loadOld);
			untyped __cpp__("runJoined({0})", adopt);
			untyped __cpp__("runJoined({0})", useMoved);
			untyped __cpp__("runJoined({0})", useOld);
			lines.push('the old module keeps only what stayed: ${retained()}');
			untyped __cpp__("runJoined({0})", dropOld);
			lines.push('and is freed once they go: "${retained()}"');
			untyped __cpp__("runJoined({0})", dropAll);
			for (line in lines)
				Sys.println('jit $jit: $line');
		}
	}
}
