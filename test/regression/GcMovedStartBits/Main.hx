import cpp.vm.Gc;

class Item {
	public var x:Int;
	public var y:Int;
	public var z:Float;
	public var next:Item;

	public function new(i:Int) {
		x = i;
	}
}

class Filler {
	public var a:Int = 0;
	public var b:Int = 0;
	public var c:Int = 0;
	public var d:Int = 0;
	public var e:Int = 0;
	public var f:Int = 0;

	public function new() {}
}

// libhxcpp #450. A moving collection that runs out of blocks to move into
// keeps the block it was emptying, in part. That block kept the start bits of
// the objects it had moved out, whose headers say they moved. A stale word on
// the stack still holding one of those addresses then had the next collection
// take the moved header for an object of 254 rows, and mark rows past the
// block's row table, over the objects in its first rows.
//
// Old addresses are kept encoded, so the collector does not see them, and put
// on the stack only for the collections below, as stale slots of a deeper call
// would be on a real stack.
@:cppFileCode('
#include <vector>
#include <map>
#include <stdint.h>
static std::vector<uintptr_t> gOld;
static std::vector<uintptr_t> gStale;
static std::vector<uintptr_t> gNow;
static int gPartial = 0;
static void recordOld(hx::Object *o) { gOld.push_back(~(uintptr_t)o); }
static void recordNow(hx::Object *o) { gNow.push_back(~(uintptr_t)o); }
// Reads headers in blocks the collector may be zeroing on its own thread, on
//  purpose, which ThreadSanitizer would report
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline, no_sanitize("thread")))
#endif
static unsigned int headerAt(uintptr_t p) { return ((unsigned int *)p)[-1]; }
// The moved-out addresses in blocks that kept some of their objects
static int pickStale()
{
   enum { BLOCK = 1<<15 };
   std::map<uintptr_t,int> flags;
   int n = (int)gOld.size();
   for(int i=0;i<n;i++)
   {
      uintptr_t old = ~gOld[i];
      uintptr_t base = old & ~(uintptr_t)(BLOCK-1);
      if (headerAt(old)==0xfe)
         flags[base] |= 1;
      else if (~gNow[i] == old)
         flags[base] |= 2;
   }
   gPartial = 0;
   for(std::map<uintptr_t,int>::iterator f=flags.begin(); f!=flags.end(); ++f)
      if (f->second==3)
         gPartial++;
   gStale.clear();
   for(int i=0;i<n && gStale.size()<4096;i++)
   {
      uintptr_t old = ~gOld[i];
      uintptr_t base = old & ~(uintptr_t)(BLOCK-1);
      if (flags[base]==3 && headerAt(old)==0xfe)
         gStale.push_back(~old);
   }
   return (int)gStale.size();
}
// Moved-out locations that a collection marked as if they held an object
static int remarked()
{
   int n = 0;
   for(size_t i=0;i<gStale.size();i++)
   {
      unsigned int h = headerAt(~gStale[i]);
      if ((h & 0xff)==0xfe && (h>>24)!=0)
         n++;
   }
   return n;
}
static void collectWithStale()
{
   volatile uintptr_t slots[4096];
   size_t n = gStale.size();
   for(size_t i=0;i<n;i++) slots[i] = ~gStale[i];
   __hxcpp_collect(false);
   for(size_t i=0;i<n;i++) slots[i] = 0;
}
')
class Main {
	static function markNibble():Int
		return untyped __cpp__("(::hx::gByteMarkID & 0xf)");

	static function main() {
		// Keep the heap as it is, so the moving collection runs short of blocks
		Gc.setMinimumWorkingMemory(256 << 20);

		var keep:Array<Item> = [];
		var junk:Array<Item> = [];
		for (i in 0...300000) {
			var it = new Item(i);
			if (i % 3 == 2)
				junk.push(it);
			else
				keep.push(it);
		}
		junk = null;

		// The collection after the moving one takes a mark byte of 0 for the
		// previous mark when its mark id ends in 1
		while (markNibble() != 15)
			Gc.run(false);

		for (it in keep)
			untyped __cpp__("recordOld({0}.mPtr)", it);
		Gc.run(true);
		for (it in keep)
			untyped __cpp__("recordNow({0}.mPtr)", it);
		var stale:Int = untyped __cpp__("pickStale()");
		var partial:Int = untyped __cpp__("gPartial");
		Sys.stderr().writeString('partly emptied blocks: $partial, moved-out addresses: $stale\n');

		var fillers = new haxe.ds.Vector<Filler>(40000);
		untyped __cpp__("collectWithStale()");
		// Nothing else allocates before the fillers take the rows the moved objects left
		for (i in 0...fillers.length)
			fillers[i] = new Filler();
		Sys.println("moved-out locations marked as objects: " + untyped __cpp__("remarked()"));
		untyped __cpp__("collectWithStale()");
		Sys.println("moved-out locations marked as objects, again: " + untyped __cpp__("remarked()"));

		var bad = 0;
		for (f in fillers)
			if (f.a != 0 || f.b != 0 || f.c != 0 || f.d != 0 || f.e != 0 || f.f != 0)
				bad++;
		Sys.println("fillers changed: " + bad);
		var wrong = 0;
		for (i in 0...keep.length)
			if (keep[i].x != ((i >> 1) * 3) + (i & 1))
				wrong++;
		Sys.println("kept items with a wrong value: " + wrong);
	}
}
