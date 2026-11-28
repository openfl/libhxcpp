package;

import haxe.Json;
import haxe.Timer;

/**
	The CPPIA workloads built for HashLink, as a reference for the cppia JIT.
	Script.hx and the bridge in bench/cppia/shared are compiled straight to
	HashLink bytecode with this harness, so on HashLink the host's side of
	each workload is JIT code too, where under cppia it is native C++.

	  hl bench.hl list
	  hl bench.hl <workload> [--warmup N] [--samples N]

	A workload may be named as its cppia benchmark is, cppia_jit_<workload>.
	It times them as bench/src/Main.hx does, and prints the same BENCH_RESULT
	line, so Run.hx (--hl) can run it beside the cppia hosts and require the
	same checksum.
**/
class HlMain {
	static function main() {
		var args = Sys.args();
		if (args.length == 0) {
			Sys.println("usage: hl bench.hl list | <workload> [--warmup N] [--samples N]");
			Sys.exit(2);
		}
		if (args[0] == "list") {
			for (w in CppiaWorkloads.all)
				Sys.println(w.name);
			return;
		}

		var warmup = 2;
		var samples = 10;
		var i = 1;
		while (i < args.length) {
			switch (args[i]) {
				case "--warmup":
					warmup = Std.parseInt(args[++i]);
				case "--samples":
					samples = Std.parseInt(args[++i]);
				default:
					fail('unknown option ${args[i]}');
			}
			i++;
		}

		var name = StringTools.startsWith(args[0], "cppia_jit_") ? args[0].substr("cppia_jit_".length) : args[0];
		var entry = null;
		for (w in CppiaWorkloads.all)
			if (w.name == name)
				entry = w;
		if (entry == null)
			fail('unknown workload $name');

		CppiaBridge.name = name;
		CppiaBridge.workload = null;
		@:privateAccess Script.main();
		var workload = CppiaBridge.workload;

		var checksum = 0;
		var times:Array<Float> = [];
		for (s in 0...warmup + samples) {
			// Each sample starts from the same heap state, outside the timed section
			hl.Gc.major();
			var t0 = Timer.stamp();
			var r = 0;
			for (k in 0...entry.reps)
				r = (r * 31 + workload.run()) | 0;
			var t = Timer.stamp() - t0;
			checksum = (checksum * 31 + r) | 0;
			if (s >= warmup)
				times.push(t);
		}

		Sys.println("BENCH_RESULT " + Json.stringify({
			name: args[0],
			unit: entry.unit,
			ops: entry.ops * entry.reps,
			warmup: warmup,
			samples: samples,
			times: times,
			latency: null,
			checksum: checksum,
			peakRss: 0,
		}));
	}

	static function fail(message:String):Dynamic {
		Sys.stderr().writeString(message + "\n");
		Sys.exit(2);
		return null;
	}
}
