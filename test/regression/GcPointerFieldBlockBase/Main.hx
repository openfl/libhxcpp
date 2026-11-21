// A cpp.Pointer field is marked as a pointer into an allocation, searching back
// from where it points for the allocation's start (libhxcpp #451). A pointer
// holding exactly a block's base address, such as one past the end of an array
// whose buffer ends where its block does, made that search's offset wrap, and
// the collection walked for hours. It holds no object and keeps nothing.
@:cppFileCode('
static ::cpp::Pointer<int> blockBaseOf(::Array<int> inArray)
{
	#ifdef HXCPP_GC_BIG_BLOCKS
	const size_t blockSize = (size_t)1 << 16;
	#else
	const size_t blockSize = (size_t)1 << 15;
	#endif
	size_t at = (size_t)inArray->GetBase();
	return ::cpp::Pointer<int>((int *)(at & ~(blockSize - 1)));
}
')
class Holder {
	public var base:cpp.Pointer<Int>;

	public function new() {}
}

class Main {
	static var holders:Array<Holder>;
	static var arrays:Array<Array<Int>>;

	static function main() {
		holders = [];
		arrays = [];
		for (i in 0...100) {
			var a = [for (j in 0...50) j];
			arrays.push(a);
			var h = new Holder();
			h.base = untyped __cpp__("blockBaseOf({0})", a);
			holders.push(h);
		}
		var start = haxe.Timer.stamp();
		for (i in 0...5)
			cpp.vm.Gc.run(true);
		var sum = 0;
		for (a in arrays)
			sum += a[49];
		Sys.println('collections finished: ${haxe.Timer.stamp() - start < 30}');
		Sys.println('arrays kept: ${sum == 49 * 100}');
	}
}
