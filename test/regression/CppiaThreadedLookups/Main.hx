import haxe.io.Path;
import sys.thread.Lock;
import sys.thread.Mutex;
import sys.thread.Thread;

interface I0 {}
interface I1 {}
interface I2 {}
interface I3 {}
interface I4 {}
interface I5 {}
interface I6 {}
interface I7 {}

// libhxcpp #466. Two lookups any thread can make inserted into maps that other
// threads were searching at the same time:
// - Std.isOfType(scriptObject, HostInterface) inserted an entry into the
//   script class's interface table for each interface it did not implement,
//   which hung or crashed threads asking about the same classes.
// - Loading a module looked up host classes with operator[], inserting what it
//   missed, while other loads walked the same map, which failed loads with "No
//   base defined for non-interface" or hung.
// A native watchdog, which the collector does not know, ends a hang.
@:cppFileCode('
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstdlib>
static std::atomic<int> gStage(0);
static void startWatchdog()
{
   std::thread([]{
      for(int i=0; i<120 && gStage<2; i++)
         std::this_thread::sleep_for(std::chrono::seconds(1));
      if (gStage<2)
      {
         printf("hung at stage %d\\n", (int)gStage);
         fflush(stdout);
         std::_Exit(1);
      }
   }).detach();
}
')
class Main {
	static function main() {
		untyped __cpp__("startWatchdog()");
		var threads = 8;
		var script = sys.io.File.getBytes(Path.join([Path.directory(Sys.programPath()), "script.cppia"]));
		var module = cpp.cppia.Module.fromData(script.getData());
		module.boot();
		module.run();
		var objects:Array<Dynamic> = Reflect.field(module.resolveClass("Script"), "objects");
		var ifaces:Array<Dynamic> = [I0, I1, I2, I3, I4, I5, I6, I7];

		var mutex = new Mutex();
		var done = new Lock();
		var wrong = 0;
		for (t in 0...threads)
			Thread.create(() -> {
				var mine = 0;
				for (round in 0...4)
					for (o in objects)
						for (k in 0...ifaces.length)
							if (Std.isOfType(o, ifaces[(k + t) % ifaces.length]))
								mine++;
				mutex.acquire();
				wrong += mine;
				mutex.release();
				done.release();
			});
		for (_ in 0...threads)
			done.wait();
		untyped __cpp__("gStage = 1");
		Sys.println('${objects.length} script objects asked about ${ifaces.length} host interfaces on $threads threads: $wrong wrong');

		var failed = 0;
		for (t in 0...threads)
			Thread.create(() -> {
				try {
					cpp.cppia.Module.fromData(script.getData());
				} catch (e:Dynamic) {
					mutex.acquire();
					failed++;
					mutex.release();
				}
				done.release();
			});
		for (_ in 0...threads)
			done.wait();
		untyped __cpp__("gStage = 2");
		Sys.println('the module loaded on $threads threads at once: $failed failed');
	}
}
