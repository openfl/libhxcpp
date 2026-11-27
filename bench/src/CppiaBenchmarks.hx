package;

import cpp.cppia.Host;
import cpp.cppia.Module;
import haxe.io.Path;
import sys.io.File;

/** The harness stays native. Only the selected workload lives in the script. **/
@:headerCode('#include <hx/Scriptable.h>')
class CppiaBenchmarks extends Benchmark {
	final workloadName:String;
	final jit:Bool;
	var module:Module;
	var workload:CppiaWorkload;

	public static function all():Array<Benchmark> {
		var result:Array<Benchmark> = [];
		for (jit in [false, true]) {
			result.push(new CppiaBenchmarks("int_loop", "iterations", 500000, 20, jit));
			result.push(new CppiaBenchmarks("float_array", "elements", 200000, 10, jit));
			result.push(new CppiaBenchmarks("alloc_objects", "objects", 125000, 4, jit));
			result.push(new CppiaBenchmarks("virtual_call", "calls", 200000, 10, jit));
			result.push(new CppiaBenchmarks("closure_call", "calls", 200000, 10, jit));
			result.push(new CppiaBenchmarks("native_call", "calls", 200000, 50, jit));
			result.push(new CppiaBenchmarks("native_callback", "callbacks", 200000, 10, jit));
			result.push(new CppiaBenchmarks("host_statics", "iterations", 200000, 10, jit));
		}
		return result;
	}

	function new(workloadName:String, unit:String, ops:Int, reps:Int, jit:Bool) {
		super("cppia_" + (jit ? "jit_" : "interp_") + workloadName, unit, ops, false, reps);
		this.workloadName = workloadName;
		this.jit = jit;
	}

	override public function setup() {
		#if CPPIA_NO_JIT
		if (jit)
			throw "CPPIA JIT benchmarks need a host built without CPPIA_NO_JIT";
		#end
		Host.enableJit(jit);
		CppiaBridge.name = workloadName;
		CppiaBridge.workload = null;
		var path = Path.join([Path.directory(Sys.programPath()), "workloads.cppia"]);
		module = Module.fromData(File.getBytes(path).getData());
		module.boot();
		module.run();
		workload = CppiaBridge.workload;
		if (workload == null)
			throw 'CPPIA workload $workloadName was not registered';
	}

	override public function run():Int {
		return workload.run();
	}
}
