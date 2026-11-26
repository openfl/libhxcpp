// A Haxe exception that no catch takes, as one thrown in a callback through a
// noexcept function or out of a native thread's function, ended the process
// through std::terminate: GCC's runtime said only "terminate called after
// throwing an instance of 'Dynamic'", LLVM's much the same, and MSVC's nothing
// at all, so the Haxe error was lost. The runtime's terminate handler now
// reports it as main's catch does. Each case runs in a child process, as each
// ends it.
@:cppFileCode('
#include <thread>

static void callThroughNoexcept(bool inNull) noexcept
{
	if (inNull)
		::Main_obj::throwNull();
	else
		::Main_obj::throwThroughNoexcept();
}

// A thread hxcpp did not create, attached as a native callback thread is
static void runNativeThread()
{
	std::thread thread([]() {
		int top = 0;
		::hx::SetTopOfStack(&top, true);
		::Main_obj::throwOnNativeThread();
		::hx::SetTopOfStack(0, true);
	});
	// The report may collect, which must not wait for this thread
	::hx::EnterGCFreeZone();
	thread.join();
}

// Attached once and then detached again, so the collector no longer knows it
//  when the exception, which a static keeps, leaves it
static void runDetachedThread(::hx::Object *inException)
{
	std::thread thread([inException]() {
		int top = 0;
		::hx::SetTopOfStack(&top, true);
		::hx::SetTopOfStack(0, true);
		// Not through ::hx::Throw, which records the stack of a thread the
		//  collector knows
		throw ::Dynamic(inException);
	});
	::hx::EnterGCFreeZone();
	thread.join();
}

static void throwInGCFreeZone(::hx::Object *inException) noexcept
{
	::hx::EnterGCFreeZone();
	::hx::Throw(inException);
}
')
class Main {
	static var pending:Boom;

	@:keep static function throwThroughNoexcept():Void {
		throw "thrown through a noexcept function";
	}

	@:keep static function throwNull():Void {
		throw null;
	}

	@:keep static function throwOnNativeThread():Void {
		throw "thrown on a native thread";
	}

	static function child(mode:String):Void {
		switch (mode) {
			case "noexcept":
				untyped __cpp__("callThroughNoexcept(false)");
			case "null":
				untyped __cpp__("callThroughNoexcept(true)");
			case "thread":
				untyped __cpp__("runNativeThread()");
			case "detached":
				pending = new Boom("thrown on a thread the collector no longer knows");
				untyped __cpp__("runDetachedThread({0}.mPtr)", pending);
			case "zone":
				pending = new Boom("thrown inside a GC-free zone");
				untyped __cpp__("throwInGCFreeZone({0}.mPtr)", pending);
		}
		Sys.println("still running");
	}

	static function main() {
		var args = Sys.args();
		if (args.length > 0) {
			child(args[0]);
			return;
		}
		var cases = [
			{mode: "noexcept", name: "through a noexcept function", error: "thrown through a noexcept function", frame: "Main::throwThroughNoexcept"},
			{mode: "null", name: "a null", error: "null", frame: "Main::throwNull"},
			{mode: "thread", name: "on a native thread", error: "thrown on a native thread", frame: "Main::throwOnNativeThread"},
			{mode: "detached", name: "on a thread the collector no longer knows", error: "Boom: thrown on a thread the collector no longer knows", frame: null},
			{mode: "zone", name: "inside a GC-free zone", error: "Boom: thrown inside a GC-free zone", frame: null},
		];
		for (c in cases) {
			var proc = new sys.io.Process(Sys.programPath(), [c.mode]);
			var out = proc.stdout.readAll().toString().split("\r\n").join("\n");
			var err = proc.stderr.readAll().toString();
			var code = proc.exitCode();
			proc.close();
			var missing = [];
			if (code == 0)
				missing.push("a failing exit code");
			if (out.indexOf('Error : ${c.error}\n') < 0)
				missing.push("the error");
			if (out.indexOf("escaped through native code") < 0)
				missing.push("where it escaped");
			// The frames it left, or never left, before the process ended
			if (c.frame != null && out.indexOf('Called from ${c.frame}') < 0)
				missing.push("the frame that threw");
			if (missing.length == 0) {
				Sys.println(c.name + ": reported");
			} else {
				Sys.println(c.name + ": missing " + missing.join(", "));
				Sys.stderr().writeString('${c.mode}: exit code $code\nstdout:\n$out\nstderr:\n$err\n');
			}
		}
	}
}

class Boom extends haxe.Exception {
	// Allocates, as most do
	override public function toString():String {
		return "Boom: " + message;
	}
}
