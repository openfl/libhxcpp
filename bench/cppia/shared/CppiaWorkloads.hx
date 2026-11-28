package;

/**
	The CPPIA workloads, each with what one run() counts as an operation, how
	many it does and how many runs make a timed sample. The cppia harness
	(bench/src/CppiaBenchmarks.hx) and the HashLink one (bench/hl/HlMain.hx)
	both read this, so they time the same work.
**/
class CppiaWorkloads {
	public static final all:Array<{name:String, unit:String, ops:Int, reps:Int}> = [
		{name: "int_loop", unit: "iterations", ops: 500000, reps: 20},
		{name: "float_array", unit: "elements", ops: 200000, reps: 10},
		{name: "alloc_objects", unit: "objects", ops: 125000, reps: 4},
		{name: "virtual_call", unit: "calls", ops: 200000, reps: 10},
		{name: "closure_call", unit: "calls", ops: 200000, reps: 10},
		{name: "native_call", unit: "calls", ops: 200000, reps: 50},
		{name: "native_callback", unit: "callbacks", ops: 200000, reps: 10},
		{name: "bunny_update", unit: "bunny updates", ops: 20000, reps: 10},
		{name: "openfl_frame", unit: "object frames", ops: 10000, reps: 5},
	];
}
