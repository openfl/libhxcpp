// A class's __register copies its vtable from an instance it makes on the stack,
// and that instance's constructor gives each dynamic function a default closure
// holding it. A stale word on a stack can reach the closure, and marking it went
// on into the stack as if it were an object: it wrote a mark into the bytes
// before it, and faulted where the line marks it looked for were not mapped. The
// telemetry test crashed in a mark thread this way on Windows x64.
//
// Here the instance is made in zeroed memory the collector does not manage, as
// __register makes it on the stack, and its default closure is kept reachable.
// Collecting must leave that memory as it was.
class Owner {
	public function new() {}

	public dynamic function run() {}
}

@:headerCode('#include <Owner.h>')
@:cppFileCode('
#include <new>
#include <string.h>
// Room for the instance and the bytes around it, which the collector reads as
//  its header and the line marks of its block
alignas(64) static unsigned char sForeign[4096];
static const int sAt = 2048;

static ::Dynamic makeForeignOwner()
{
	memset(sForeign, 0, sizeof(sForeign));
	::Owner_obj *owner = ::new (sForeign + sAt) ::Owner_obj();
	return owner->run;
}

static int foreignBytesTouched()
{
	unsigned char copy[sizeof(::Owner_obj)];
	memcpy(copy, sForeign + sAt, sizeof(copy));
	int touched = 0;
	for (int i = 0; i < sAt; i++)
		if (sForeign[i])
			touched++;
	for (int i = sAt + (int)sizeof(::Owner_obj); i < (int)sizeof(sForeign); i++)
		if (sForeign[i])
			touched++;
	return touched;
}
')
class Main {
	static var kept:Dynamic;

	static function main() {
		kept = untyped __cpp__("makeForeignOwner()");
		for (i in 0...3) {
			var junk = [for (j in 0...10000) [j]];
			cpp.vm.Gc.run(true);
		}
		var touched:Int = untyped __cpp__("foreignBytesTouched()");
		Sys.println('bytes written around the instance: $touched');
	}
}
