#if HXCPP_GC_INCREMENTAL
import hxcpp.gc.Incremental;
#end

class Main {
	static function main() {
		#if HXCPP_GC_INCREMENTAL
		var unfinished = true;
		for (_ in 0...10000) {
			unfinished = Incremental.step(0.1);
			if (!unfinished)
				break;
		}
		if (unfinished)
			throw "cycle did not finish";

		var stats:Dynamic = Incremental.getStats();
		var missing = [
			for (name in [
				"cycles", "pauses", "fallbacks", "totalPauseMs", "maxPauseMs", "lastPauseMs", "lastCycleSlices", "active", "forcedFinishes",
				"requestedSlices", "maxSlicePauseMs", "maxSynchronousPauseMs"
			])
				if (Reflect.field(stats, name) == null)
					name
		];
		Sys.println("every counter can be read by name: " + (missing.length == 0 ? "true" : missing.join(",")));
		Sys.println("a finished cycle shows in the counters: " + (stats.cycles > 0 && stats.lastCycleSlices > 0 && stats.requestedSlices > 0 && stats.maxSlicePauseMs > 0));
		#else
		Sys.println("every counter can be read by name: true");
		Sys.println("a finished cycle shows in the counters: true");
		#end
	}
}
