import cpp.vm.Gc;
import sys.thread.Lock;
import sys.thread.Thread;

// A thread that allocates inside a GC-free zone while another thread collects
// stops in PauseForCollect to wait for the collection. The collector neither
// waits for a thread in a zone nor releases it, so in a release build that
// thread waited for ever, silently; only debug builds said why. It is
// reported as a critical error now, which ends the program, so the misuse
// runs in a child process. A third thread that reaches no safe point for half
// a second keeps the collection pending while the zone thread allocates.
@:cppFileCode('
#include <atomic>
#include <chrono>
#ifdef HX_WINDOWS
#include <windows.h>
#endif
static std::atomic<bool> gInZone(false);
static bool collectionPending() { return ::hx::gPauseForCollect.load() != 0; }
static void spinFor(int inMilliseconds)
{
	auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(inMilliseconds);
	while (std::chrono::steady_clock::now() < end) {}
}
// The report ends the program with a fault, which must not open a dialog
static void quietFaults()
{
	#ifdef HX_WINDOWS
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	#endif
}
')
class Main {
	static function child() {
		untyped __cpp__("quietFaults()");
		final busyStarted = new Lock();
		Thread.create(() -> {
			busyStarted.release();
			untyped __cpp__("spinFor(500)");
		});
		busyStarted.wait();

		final allocated = new Lock();
		Thread.create(() -> {
			Gc.enterGCFreeZone();
			untyped __cpp__("gInZone = true");
			while (!untyped __cpp__("collectionPending()")) {}
			// The misuse: an allocation in the zone, with a collection pending
			final strings = [for (i in 0...10) Std.string(i)];
			Gc.exitGCFreeZone();
			allocated.release();
		});
		while (!untyped __cpp__("gInZone"))
			Sys.sleep(0.001);
		Gc.run(true);
		Sys.println(allocated.wait(5.0) ? "the allocation in the zone returned" : "the allocation in the zone never returned");
	}

	static function main() {
		if (Sys.args().length > 0) {
			child();
			return;
		}
		final process = new sys.io.Process(Sys.programPath(), ["child"]);
		final out = process.stdout.readAll().toString();
		final code = process.exitCode();
		process.close();
		Sys.stderr().writeString('child: exit code $code\n$out');
		// A debug build reports any allocation in a zone, with its own message
		Sys.println("allocating in a GC-free zone while another thread collects is reported: "
			+ (out.indexOf("Critical Error: Allocating or collecting from a GC-free thread while another thread collects") >= 0
				|| out.indexOf("Critical Error: Allocating from a GC-free thread") >= 0));
		Sys.println("and ends the program: " + (code != 0));
	}
}
