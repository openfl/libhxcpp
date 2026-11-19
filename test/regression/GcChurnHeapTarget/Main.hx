import cpp.vm.Gc;
import haxe.io.Bytes;

// Churn of objects bigger than a line (128 bytes) with a tiny live set must
// keep the heap at its working target (20 MB by default). A collection started
// because no free block could take an allocation counted the free rows of the
// blocks past the first one smaller requests had not taken yet as in use, and
// only the smallest requests move that point on, so the rows of the blocks the
// churn had taken and filled were counted too: the heap looked full and its
// target doubled at every other collection. Bytes of 2000 bytes reached 41 MB
// reserved, and strings after them 83 MB (openfl/libhxcpp#256).
//
// With HXCPP_GC_GENERATIONAL the heap stays near the nursery's size instead
// (see GcHeapGrows), which is under the bound as well.
function main() {
	var most = 0.0;
	var ring = [for (i in 0...64) Bytes.alloc(16)];
	for (i in 0...2000000) {
		ring[i & 63] = Bytes.alloc(2000);
		if (i & 1023 == 0)
			most = Math.max(most, Gc.memInfo64(Gc.MEM_INFO_RESERVED));
	}
	var base = StringTools.lpad("", "x", 1990);
	var strings = [for (i in 0...64) ""];
	for (i in 0...2000000) {
		strings[i & 63] = base + i;
		if (i & 1023 == 0)
			most = Math.max(most, Gc.memInfo64(Gc.MEM_INFO_RESERVED));
	}
	Sys.println(most < 32 * 1048576 ? "stayed at the target" : 'grew to ${Std.int(most / 1048576)} MB');
}
