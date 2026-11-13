// hx::thread::ConditionVariable had no timedWait, which Haxe 5's
// sys.thread.Condition calls, so every program using Condition failed to
// compile with the Haxe nightly. The checks call it from native code, which
// builds with every Haxe.
@:cppFileCode('
#include <hx/thread/ConditionVariable.hpp>
#include <cmath>
static Dynamic makeCondition() { return hx::thread::ConditionVariable(new hx::thread::ConditionVariable_obj()); }
static hx::thread::ConditionVariable_obj *cv(Dynamic c) { return (hx::thread::ConditionVariable_obj *)c.mPtr; }
static void acquireCondition(Dynamic c) { cv(c)->acquire(); }
static void releaseCondition(Dynamic c) { cv(c)->release(); }
static void signalCondition(Dynamic c) { cv(c)->signal(); }
static bool timedWaitCondition(Dynamic c, double s) { return cv(c)->timedWait(s); }
')
class Main {
	static var condition:Dynamic = untyped __cpp__("makeCondition()");

	// Waits with the lock held, as Condition.timedWait is called
	static function wait(seconds:Float):{woken:Bool, took:Float} {
		var start = haxe.Timer.stamp();
		var woken:Bool = untyped __cpp__("timedWaitCondition({0}, {1})", condition, seconds);
		return {woken: woken, took: haxe.Timer.stamp() - start};
	}

	// A thread that signals once it gets the lock, which the wait gives up
	static function waitForSignal(seconds:Float):{woken:Bool, took:Float} {
		untyped __cpp__("acquireCondition({0})", condition);
		sys.thread.Thread.create(() -> {
			untyped __cpp__("acquireCondition({0})", condition);
			untyped __cpp__("signalCondition({0})", condition);
			untyped __cpp__("releaseCondition({0})", condition);
		});
		var result = wait(seconds);
		untyped __cpp__("releaseCondition({0})", condition);
		return result;
	}

	static function main() {
		untyped __cpp__("acquireCondition({0})", condition);
		var timeout = wait(0.05);
		var none = [wait(0), wait(-1), wait(Math.NaN)];
		untyped __cpp__("releaseCondition({0})", condition);
		Sys.println("a wait no one signals runs out: " + (!timeout.woken && timeout.took >= 0.04));
		Sys.println("zero, negative and NaN waits return at once: " + Lambda.foreach(none, r -> !r.woken && r.took < 1));

		var signalled = waitForSignal(30);
		Sys.println("a signal ends the wait: " + (signalled.woken && signalled.took < 10));
		// Past the cap, the deadline would overflow the clock and the wait end at once
		var long = waitForSignal(1e300);
		Sys.println("a wait past the cap still waits for the signal: " + long.woken);
	}
}
