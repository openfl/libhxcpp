import cpp.vm.Gc;
import haxe.io.Bytes;

@:cppFileCode('
extern "C" void *hx_cffi(const char *inName);
typedef hx::Object *(*CreateAbstract)(void *, int, void *);

// A CFFI abstract with memory of its own, which the collector marks
//  conservatively, as it does a stack
static Dynamic makeAbstract(int inWords)
{
   static CreateAbstract create = (CreateAbstract)hx_cffi("create_abstract");
   return create((void *)(intptr_t)0x1234, inWords*(int)sizeof(void *), 0);
}

static void setWord(Dynamic inAbstract, int inIndex, void *inValue)
{
   ((void **)inAbstract->__GetHandle())[inIndex] = inValue;
}
')
// The collector finds a large allocation from a word that may point at one
// through sorted indexes of the large allocations, which it builds on the
// first lookups of a collection. The marking threads look up too, for an
// abstract's memory, and two of them building one index at once left
// buffers that the abstracts' memory pointed to unmarked: they were freed
// while held, and taken again by the next allocations.
class Main {
	static inline var BUFFERS = 20000;
	static inline var BUFFER_SIZE = 4000;
	static inline var ABSTRACTS = 8000;
	static inline var WORDS = 16;

	static var keep:Array<Dynamic>;
	static var bases:Array<cpp.Pointer<cpp.UInt8>>;

	// The buffers are held only through the abstracts' memory once this returns
	static function round(r:Int) {
		bases = [];
		var hold = [];
		for (j in 0...BUFFERS) {
			var b = Bytes.alloc(BUFFER_SIZE);
			hold.push(b);
			b.fill(0, BUFFER_SIZE, (r * 7 + j) & 255);
			bases.push(cpp.Pointer.arrayElem(b.getData(), 0));
		}
		keep = [];
		for (a in 0...ABSTRACTS) {
			var abs:Dynamic = untyped __cpp__("makeAbstract({0})", WORDS);
			for (w in 0...WORDS) {
				var p = bases[(a * WORDS + w) % BUFFERS];
				untyped __cpp__("setWord({0}, {1}, (void *){2}.ptr)", abs, w, p);
			}
			keep.push(abs);
		}
		for (j in 0...BUFFERS)
			hold[j] = null;
	}

	static function intact(r:Int):Bool {
		for (j in 0...BUFFERS) {
			var p = bases[j];
			var want = (r * 7 + j) & 255;
			for (k in 0...BUFFER_SIZE)
				if (p[k] != want)
					return false;
		}
		return true;
	}

	static function main() {
		var lost = 0;
		for (r in 0...16) {
			round(r);
			Gc.run(true);
			// What the collection freed is taken again here
			for (j in 0...BUFFERS)
				Bytes.alloc(BUFFER_SIZE).fill(0, BUFFER_SIZE, 0xee);
			if (!intact(r))
				lost++;
		}
		Sys.println('rounds that lost a buffer an abstract held: $lost');
	}
}
