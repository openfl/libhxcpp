package;

import AllocBenchmarks;
import AnonBenchmarks;
import ArrayBenchmarks;
import CffiBenchmarks;
import EnumBenchmarks;
import GcBenchmarks;
import GcFrameBenchmarks;
import IoBenchmarks;
import LargeBenchmarks;
import MapBenchmarks;
import SocketBenchmarks;
import SortBenchmarks;
import StringBenchmarks;
import ThreadBenchmarks;
import cpp.vm.Gc;
import haxe.Json;
import haxe.Timer;

/**
	Benchmark executable. The driver (bench/Run.hx) builds it once per
	runtime and starts one process per benchmark, so peak memory belongs
	to that benchmark alone.

	  Main list                         names of all benchmarks
	  Main env                          compiler, pointer size and API level
	  Main noop                         exits at once (startup time)
	  Main <name> [--warmup N] [--samples N]

	Results go to stdout as one line: BENCH_RESULT {json}.
**/
class Main {
	static function all():Array<Benchmark> {
		#if bench_cppia
		return CppiaBenchmarks.all();
		#else
		return [
			new AllocSmall(), new AllocStrings(), new AllocArrays(), new GcPauses(), new GcFrames(), new MapInt(), new MapString(), new MapObject(),
			new StringBuild(), new StringIndex(), new StringSubstr(), new StringSplit(), new StringCase(), new SortInt(), new SortString(),
			new SortObject(), new DequePush(), new DequeBurst(), new CffiCallVector(), new CffiStringView(), new EnumFormat(), new EnumIsOfType(), new EnumTypedCatch(), new ClassTypedCatch(),
			new MapInt64(), new MapInt64Values(), new ArrayPushDynamic(), new ArrayReserveDynamic(), new ArrayPop(),
			new SearchFrom(), new StringLongNeedle(), new ParseNumbers(), new FloatFormat(), new AnonFieldSmall(), new AnonFieldWide(), new SqliteRows(),
			new SocketRead(), new CffiBuffer(), new LargeResize(), new LargeRecycle(), new LargeCopy(), new LargeCachePurge(),
			new GcWeakMapInt(), new GcObjectIds(), new GcLargeStackRoots(), new GcRegexWait(), new GcZombieReservations(), new GcFinalizerPromotion(), new GcIdleMinor(), new GcRememberedBurst(), new GcObjectIdBurst(), new GcWeakMapChain(),
			new SocketReadLine(false), new SocketReadLine(true),
		].concat(PollIdle.all());
		#end
	}

	static function main() {
		var args = Sys.args();
		if (args.length == 0) {
			Sys.println("usage: Main list | env | noop | <benchmark> [--warmup N] [--samples N]");
			Sys.exit(2);
		}
		switch (args[0]) {
			case "noop":
				return;
			case "list":
				for (b in all())
					Sys.println(b.name);
				return;
			case "env":
				var apiLevel:Int = untyped __cpp__("HXCPP_API_LEVEL");
				Sys.println("BENCH_ENV " + Json.stringify({
					compiler: Native.compiler(),
					pointerBits: Native.pointerBits(),
					apiLevel: apiLevel,
				}));
				return;
			default:
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

		var bench:Benchmark = null;
		for (b in all())
			if (b.name == args[0])
				bench = b;
		if (bench == null)
			fail('unknown benchmark ${args[0]}');

		bench.setup();
		var checksum = 0;
		var times:Array<Float> = [];
		var latencies:Array<Float> = [];
		for (s in 0...warmup + samples) {
			// Each sample starts from the same heap state, outside the timed section
			Gc.run(true);
			if (bench.measuresLatency)
				bench.latencies = [];
			var t0 = Timer.stamp();
			var r = 0;
			for (k in 0...bench.reps)
				r = (r * 31 + bench.run()) | 0;
			var t = Timer.stamp() - t0;
			checksum = (checksum * 31 + r) | 0;
			if (s >= warmup) {
				times.push(t);
				if (bench.measuresLatency)
					for (l in bench.latencies)
						latencies.push(l);
			}
		}

		latencies.sort(Reflect.compare);
		Sys.println("BENCH_RESULT " + Json.stringify({
			name: bench.name,
			unit: bench.unit,
			ops: bench.ops,
			warmup: warmup,
			samples: samples,
			times: times,
			latency: bench.measuresLatency ? {
				count: latencies.length,
				p50: percentile(latencies, 0.5),
				p99: percentile(latencies, 0.99),
				p999: percentile(latencies, 0.999),
				max: latencies[latencies.length - 1],
			} : null,
			checksum: checksum,
			peakRss: Native.peakRss(),
			heapReserved: Gc.memInfo64(Gc.MEM_INFO_RESERVED),
			heapUsage: Gc.memInfo64(Gc.MEM_INFO_USAGE),
			#if HXCPP_GC_INCREMENTAL
			incrementalGc: hxcpp.gc.Incremental.getStats(),
			#end
		}));
	}

	/** Nearest-rank percentile of a sorted array. **/
	static function percentile(sorted:Array<Float>, p:Float):Float {
		var rank = Math.ceil(p * sorted.length) - 1;
		return sorted[rank < 0 ? 0 : rank];
	}

	static function fail(message:String):Dynamic {
		Sys.stderr().writeString(message + "\n");
		Sys.exit(2);
		return null;
	}
}
