
libhxcpp 6.0.0 (unreleased)
------------------------------------------------------------

* Emscripten programs let memory grow and get a 5 MB stack by default. They linked with Emscripten's fixed 16 MB of memory, which aborts with "Aborted(OOM)" as soon as a program holds more, and since Emscripten 3.1.27 a 64 KB stack, which Haxe recursion and the pointers that spill-pointers keeps on the stack soon overflow. HXCPP_LINK_NO_MEMORY_GROWTH and HXCPP_LINK_NO_STACK_SIZE restore Emscripten's settings, and HXCPP_LINK_STACK_SIZE sets another size
* Fixed the Emscripten build failing to compile Socket.cpp, which called monotonicSeconds where it was not defined, and, on Windows, failing at the strip step, which passed emstrip.py to python with the quotes still around its path
* Fixed a class with a cpp.Function field failing to compile with HXCPP_GC_GENERATIONAL: Haxe 4.3 write barriers the field's setter as if it held an object, and cpp::Function now has the null mPtr that barrier needs (HaxeFoundation/hxcpp#1425 by Tutez64)
* Fixed the collector scanning a thread that had just entered a GC-free zone without seeing that thread's last writes, on ARM64 and other weakly ordered CPUs: it could read stale registers and miss an object held only there
* Fixed the shared boxes for Ints and Int64s from -1 to 255 reaching another thread before they were built, on weakly ordered CPUs
* Fixed haxe.io.Bytes and Memory reading and writing 16, 32 and 64-bit values through misaligned pointers at odd offsets, which is undefined and traps on CPUs without unaligned access
* Fixed haxe.Timer.stamp when threads make the first call together: later stamps could move backwards, and on Apple platforms it could divide by a timebase not yet read
* Fixed a cppia cast that compiles to nothing reading the expression it had just freed
* __hxcpp_set_float_format accepts only a pattern with one floating-point conversion, such as %.6g, and throws otherwise: a pattern with %s or %n read or wrote memory through the double it was given
* Fixed a collection waiting for a thread that loops on an uncontended Semaphore.acquire until the loop ended, on Linux, Windows and other POSIX systems: the fast path now stops at a pending collection, as Mutex.acquire and Semaphore.tryAcquire do
* Fixed @:nativeGen interfaces on a class that implements more than one: converting an object, or a Dynamic, to the second interface gave the object's address instead of the interface's, so calls through it crashed, and the collector freed an object held only through such an interface. Classes with these interfaces also failed to compile with MSVC (HaxeFoundation/hxcpp#1380)
* Fixed Std.isOfType(v, Class) and Std.isOfType(v, Enum) both being true for every class and every enum: a class is now only a Class and an enum only an Enum, as on the other targets, and haxe.Serializer writes an enum type as an enum (HaxeFoundation/hxcpp#1391)
* Fixed cpp.marshal.View.slice(index, length) accepting a length so large that the bounds check overflowed, which gave a view larger than its buffer (API level 500)
* Fixed cppia on 32-bit ARM and Emscripten storing only the high half of a Float in a frame, so Float arguments, locals and results lost their low bits
* Fixed cppia script classes that implement a host interface with type parameters, such as a Map<String, Int>: calls through the interface passed the arguments as objects while the script function read them as typed values, so they read the wrong values and could crash. Such a call now goes through an adapter that converts the arguments and the result
* Fixed Process.close, and the collector cleaning up a Process, leaving the child a zombie on Linux, macOS and other POSIX systems when it was still running: such a child is now reaped when it exits, by a thread that waits only for those children, without blocking close or killing the child, and without a SIGCHLD handler
* The regression, std and GC stress suites run under UBSan, ASan and TSan in CI, and CodeQL analyzes the C++ sources
* Fixed HXCPP_SANITIZE=address builds freeing live objects: ASan's use-after-return check, on by default with GCC 13 on Linux, moves locals into frames off the stack, where the collector does not look for them. The runtime now turns that check off by default; ASAN_OPTIONS can still turn it on
* The build tool stops with a clear error when a file group lists files whose names differ only in case, such as the Texpr.cpp and TExpr.cpp that classes Texpr and TExpr generate: on Windows and macOS they are one file and one object file, and the build failed with unrelated errors (libhxcpp #70). Rename one of the classes
* Fixed the heap not growing to its working target without the moving collector: when the allocator filled its free list it forced a compacting collection, and that released every empty block group, so the heap stayed near 5 MB and collected about four times as often as hxcpp 4.3 (3x slower small allocations). Only Gc.compact now gives all empty groups back (libhxcpp #230)
* Fixed Gc.run(true) giving the whole heap back to the system like Gc.compact, so the next work had to commit and fault it in again; map lookups and iteration after a major collection were up to 2x slower than hxcpp 4.3. Gc.run(true) now keeps memory up to the normal working target, as in hxcpp 4.3, and only Gc.compact releases every empty group (libhxcpp #231, #233)
* Fixed maps shrinking their bucket array as soon as they fell to half full, so a map that lost and regained keys around that size shrank and grew again on every swing; removing 100,000 of 200,000 Int keys took 1.9 ms against hxcpp 4.3's 0.26 ms. A map now shrinks once it is a quarter full, and removing those keys takes 0.9 ms (libhxcpp #225)
* Fixed IntMap being up to 2.3x slower than hxcpp 4.3 on arithmetic keys (ids, offsets, i * odd constant). Every Int key went through a bit mix that guards against strided keys but scatters keys that the plain identity hash spreads with no collisions. Int maps now start on the identity hash, like hxcpp 4.3, and switch to the mix for good when an insert finds a chain of 16 or growth leaves more than 2 entries per used bucket, so strided keys stay 130x to 770x faster than in hxcpp 4.3 (libhxcpp #231)
* ObjectMap is about 10x faster on 64-bit targets and its peak memory 40% lower on the map benchmark: keys hash by address, as they do on 32-bit, instead of taking a global lock to add every key to the object id table that each collection then walks. The moving collector, where objects change address, keeps the ids (libhxcpp #77)
* Fixed ObjectMap being about 15% slower than hxcpp 4.3 on Windows, along with everything else that takes an object id: the locks around the object id table, finalizer and weak map registration, the large allocation list and CFFI field ids had become std::mutex, which with MSVC costs about 40 ns per uncontended lock and unlock where hxcpp 4.3's critical section cost about 10 ns. They now use a slim reader/writer lock on Windows (std::mutex elsewhere, and with HXCPP_WINXP_COMPAT); looking up 200,000 object ids takes 4.5 ms instead of 11.5 ms, against hxcpp 4.3's 5 ms (libhxcpp #232)

libhxcpp 6.0.0-rc.1
------------------------------------------------------------

The first libhxcpp version. It is based on hxcpp 4.3 (the changes listed under 4.3.xx below), with the following on top.

### Installing and using libhxcpp

* libhxcpp is installed as its own haxelib, `libhxcpp`. `haxelib run libhxcpp setup` points the name hxcpp at it, so the compiler, `-lib hxcpp` and libraries that depend on hxcpp all use the same runtime; `haxelib run libhxcpp off` switches back to hxcpp, which setup never touches. After `haxelib update libhxcpp`, the next build moves the redirect to the new version by itself
* A build that uses the library, as `-lib hxcpp` or `-lib libhxcpp`, gets both `-D libhxcpp` and `-D hxcpp`, set to libhxcpp's version. Compare them with version("x.y.z"); a plain string comparison compares text
* Stated Haxe 4.2 as the minimum Haxe version. Haxe 4.0 and 4.1 send thread messages through runtime calls that hxcpp 4.3 already dropped; they now fail with a message that says so
* Merged upstream HaxeFoundation/hxcpp master through 199b7564 (2026-09-22), fixing a race its move of the GC to std::atomic (#1389) brought: the mark-chunk spin locks did not reset the expected value after a failed exchange, so two marking threads could hold one at once, and live objects were freed under intermittent heap corruption

### Performance

* Added an opt-in PCRE2 JIT for EReg (-D HXCPP_PCRE_JIT): 3-5x faster matching, search and replace. A match that runs out of JIT stack is retried on the interpreter, so results never change; off on iOS, tvOS, watchOS, emscripten and WinRT
* Improved Map/StringMap/IntMap lookup performance by ~16-26% by lowering the default hash table load factor
* Applied the same hash mixing to Int64 and object map keys (fixes ~168x slower lookups for strided Int64 keys; modest gain for object keys)
* Reduced large-map build time ~24-36% by switching hash table growth from 2x to 4x once a map is large (small maps keep 2x, so their memory is unchanged)
* Improved array slice/splice by skipping the generational GC write barrier for arrays of primitive (non-pointer) types
* Increased Socket.read() chunk buffer from 256 bytes to 16KB, cutting recv() syscalls ~64x when reading a stream to EOF
* Sped up String.indexOf on byte strings using memchr: ~58x faster for sparse/absent single-char search (the common contains check) and ~2.5x faster for multi-char search
* Sped up String.toUpperCase/toLowerCase ~2.3x for ASCII strings with a branchless transform instead of per-char locale-dependent toupper/tolower
* Sped up Date field access ~3.7x by caching the last localtime/gmtime conversion per thread (getHours/getMinutes/getFullYear/... on one date no longer each call localtime)
* Sped up Int/Int64/UInt64 to String conversion ~2.5x by writing digits directly instead of snprintf (affects Std.string of integers, string interpolation, etc.)
* Replaced the C rand() backing of Math.random/Std.random with a per-thread xoshiro256++ generator: ~10x faster, no libc lock contention, 53-bit doubles, and worker threads no longer produce identical sequences on Windows (also fixes Std.random(0) crashing with division by zero)
* Sped up String.split with a memchr candidate scan instead of a libc compare per byte position (~15-20% on byte strings including the per-part allocation, much more on the raw scan), added a first-unit skip to the wide-string path, and fixed delimiters containing NUL falsely matching any NUL in the subject
* Sped up haxe.io.Bytes.ofString ~25% for non-ASCII strings by sizing the output once and encoding directly into the buffer instead of pushing per byte
* Reduced GC pause time for programs holding many large (4KB+) allocations: conservative stack marking now rejects out-of-range candidates against cached bounds instead of scanning the whole large-object list per stack word, and the last-value dedupe in the conservative marker actually works now
* Sped up indexOf/lastIndexOf on UTF-16 (non-ASCII) strings 2-3.5x with first-unit candidate skipping, and made searching a byte string for an unmatchable wide needle O(1) instead of a full scan
* Reduced marker-thread cache-line contention by skipping redundant row-mark stores during the GC mark phase
* Sped up freeing large allocations on the array/Bytes growth path ~36% in realloc-heavy benchmarks by searching the large-object list from the end, where the just-allocated buffer lives
* Reduced Dynamic function call overhead at API level 500 (Haxe 5): arguments are written into a pre-sized array instead of pushed one at a time with per-element capacity checks
* Made the GC-safe-zone handshake lock-free when no collection is pending: entering skips the kernel event signal and exiting skips the process-global mutex unless a collect is actually starting or running. Combined with uncontended fast paths in sys.thread.Mutex/ConditionVariable/Semaphore (try-lock before entering the zone), uncontended Mutex.acquire/release is ~18x faster and Semaphore ~2x, and independent threads no longer serialize on a global lock for every blocking call
* Sped up string maps with runtime-built keys 2.2-2.4x: strings of 8+ chars now memoize their hash on first use in a reserved slot after the terminator instead of re-hashing the whole string on every map operation
* Sped up Array.sort with scalar elements ~23-33% by boxing comparator arguments once per element instead of twice per comparison
* Sped up sys.ssl Socket.read ~64x fewer native calls per TLS record (256-byte drain buffer -> 16KB)
* Sped up print/println on Windows by caching the stdout console probe (was a kernel call per print)
* Sped up Std.isOfType(x, Int) by replacing RTTI dynamic_casts with cheap type probes
* Sped up File.getContent by reading directly into the string buffer (was staged through a std::vector, doubling peak memory with an extra full copy)
* Sped up jitted Int % Int ~7.6x: a real integer division (with a bailout for divisors 0 and -1) instead of two int-to-double conversions and a native fmod call per operation
* Sped up Type.resolveClass ~3.7x by replacing the class registry's ordered map (full string comparisons per tree level) with a hash map keyed on the precomputed permanent-string hashes, and fixed runtime cppia class registration racing unsynchronized against concurrent Type.resolveClass
* Sped up Type.getInstanceFields ~5.7x by caching the computed field list per class (metadata is immutable after registration; callers receive copies)
* CFFI val_id field lookups no longer allocate a std::string per call (transparent comparator)
* Sped up cppia dynamic field access ~1.7-2.3x (get/set/Reflect.field on script classes): each class now builds a hash map of its members at link time instead of scanning functions, dynamic functions and variables linearly with a string compare per entry on every access
* Sped up interpreted cppia switch statements with constant integer cases (including over int expressions the runtime types as float, like %): the body is found with one hash probe instead of re-running every case condition per execution (~20% on a 12-case switch including loop overhead; the win grows with case count)
* Sped up interpreted Int % Int: both operands now stay in integer math (with a bailout for divisors 0 and -1) instead of two double conversions and a native fmod per operation
* haxe.zip.Uncompress.run no longer reallocates and copies the whole accumulated output per 64KB chunk (quadratic - decompressing 100MB copied ~80GB), and guards against 2GB overflow instead of writing past the output
* Sped up sys.thread.Deque and the thread message queue ~25x for deep queues (200k add+drain: 1070ms -> 43ms): popping advances a head offset with amortized compaction instead of memmoving the entire remaining queue per pop while holding the lock; uncontended queue operations also skip two GC-free-zone transitions via a try-lock fast path
* Reduced stop-the-world GC work: the per-collect class-statics walk iterates a dense registration list instead of chasing the class registry's hash buckets (the list replaces entries in place when cppia reloads re-register a name, so replaced classes do not stay rooted)
* Sped up Poll on Windows with many sockets: ready sockets are found in one walk instead of a search of the set per registered socket, which grew with the square of the connection count
* Fixed catastrophic IntMap collisions for keys with low-bit structure (pointers, aligned/strided ids) by mixing integer hashes; up to ~280x faster lookups for such keys with no regression for dense keys
* Fixed IntMap keys packed as (x << 16) | y piling into a few buckets: a 512x512 grid of them built in 99 ms instead of the 7 ms dense keys take, and now in 8 ms. Dense and strided keys are unchanged
* Sped up FileInput.readByte about 6.8x and readLine through it about 4x (1 MB: 61 ms to 9 ms, and 70 ms to 17 ms, on Windows) on regular files opened for reading: bytes come from a 4 KB read-ahead instead of one GC-free zone and fread call each. Pipes, stdin and files opened for writing read as before
* Sped up successful dynamic field reads through FieldMapGet (cppia script class fields, toString lookups) about 1.7x by looking the name up once instead of twice: 2M reads 36 ms to 21 ms on Windows
* Sped up growing the heap again after it was released: a new GC block takes a freed id from a list instead of searching the block table from the start, which was quadratic in the number of freed blocks (refilling 1 GB after Gc.compact: 753 ms to 643 ms)
* Sped up releasing GC block groups: released blocks leave the address-ordered block list in one pass instead of one erase each, which was quadratic while threads are stopped (Gc.compact of 1 GB: 112 ms to 83 ms)
* Shortened the large-allocation lock: a recycled large buffer is cleared after the lock is released, as a fresh one is, instead of stalling other threads' large allocations for the whole clear (4 threads allocating 12000 zeroed 1 MB buffers: about 15% faster)
* Fixed reserving room in an empty hash table allocating 2 buckets whatever the size asked for, so converting a map's value storage rehashed it over a dozen times; converting 300k IntMap entries is about 15% faster
* Std.parseInt no longer measures the whole string to look for a hex prefix
* Sped up toUpperCase and toLowerCase: a string with nothing to change comes back as it is, and one that changes is copied once from its first changed character (10000 unchanged 4 KB conversions: about 90 ms to 16 ms); substr and substring of a whole string return it without a copy
* Bounded String.indexOf on repetitive text: when first-character candidates keep matching the start of a long needle it switches to Two-Way search, linear in the text (a 16 KB needle in 1 MB of the same character: 253 ms to under 1 ms); ordinary searches keep the memchr path
* Generational collections set the remembered set's marks straight from its chunks instead of first copying every pointer into a temporary array
* Array<String>.sort boxes each String once instead of boxing both arguments on every comparison
* Conservative stack scanning no longer scans the whole large-object list for every stack word in its address range: after several such words in a collection it searches a sorted index, and the range shrinks back when large objects are freed. With 40000 large buffers, a collection with native frames on the stack fell from 2.4-4.6 ms to 0.8 ms
* Programs no longer load user32.dll at startup on Windows, which made starting and exiting about 1.1 ms slower than stock hxcpp: the critical-error dialog now looks MessageBoxA up when it is needed
* cppia safe casts to Int, Float, Bool or String skip their type check when the value's static type already passes it: the check boxed every value and called out of the JIT, so Float array code ran at a fifth of its speed
* cppia sets object fields directly again, except host fields at API level 500, which can hold typed functions: since HaxeFoundation/hxcpp#1324 every object field store went through a named lookup, and script allocation and virtual calls ran at a third to a sixth of hxcpp 4.3.53's speed in the JIT
* cpp.encoding.Utf8.decode allocates its result once at the exact size: it counts the UTF-16 units first, eight bytes at a time, instead of decoding into a buffer twice the input's size and copying out of it. That allocates 4x less for CJK text and 3x less for Greek; throughput is 15% higher on short strings, 3% on mostly-ASCII text and 5% to 9% lower on text with no ASCII (libhxcpp #196)

### Garbage collector

* Fixed weak maps whose entries lead from one map's key to the next map's making a collection slow: what each weak map's values reach is now marked before the next map is looked at, and the scan alternates direction, so such a chain no longer takes one pass, and a restart of the marking threads, per map. 2,048 chained maps took 17.3 ms of a major collection and now take 0.18 ms, whichever order they were registered in (libhxcpp #203)
* Fixed the collector freeing an object that a function still used when the stack held only the address of one of its fields: clang keeps &array->length instead of the array across a call when only the length is read later, and the stack scan took only pointers to an object's start (or 16 bytes in, with MSVC) and only at 8-byte-aligned addresses. Stack values up to eight pointer sizes into an object (64 bytes on 64-bit), 4-byte aligned, now keep it alive on every compiler (libhxcpp #269)
* The GC returns fully empty block groups to the system without HXCPP_GC_MOVING too, after a full collection that leaves much more memory than it needs and on Gc.compact (not under Emscripten, where WebAssembly memory never shrinks and a released group would only fragment it)
* Debug builds now trap when a GC block's row counts exceed its useful lines, before the counts skew the heap stats
* Fixed a data race in the large-allocation recycle list: the lock-free probe now reads a dedicated counter instead of scanning the vector concurrently with mutation (also fixed a skipped-entry bug when the locked recheck failed)
* Fixed GC finalizer-map corruption when a finalizer unregisters itself or another finalizer: dropping an unclosed haxe.zip Compress/Uncompress (whose finalizer calls the same close() as manual cleanup) invalidated the iterator the GC was holding - undefined behavior on every such collection. Dead entries are now unregistered before their callbacks run
* Fixed the collector freeing an object that a paused thread held only in a register, on Windows x64 with MSVC: the register capture recorded its own RBX instead of the thread's and never recorded RSI, and the registers the pausing function saved on entry lay just below the stack range scanned. A worker calling cpp.vm.Gc.safePoint() in a loop while another thread forced a collection lost its working array, and so could a thread entering a GC-free zone or collecting
* Fixed a thread running through a collection after leaving a GC-free zone just as the previous one ended: the end of that collection signalled it as if it had been paused, the signal stayed set, and its next pause for a collection returned at once - leaving a stale signal that let the collection after scan its stack while it ran. Collections now release only the threads they paused
* Fixed a process hanging or crashing as it exits while its threads end: a thread removes its root as it ends (since the typed thread API, #1336), and the root set it removed it from was a static, which the main thread had already destroyed among the destructors that run after main returns. The erase faulted while holding the root lock; on Windows the next thread to end then waited on that lock for ever inside the loader lock, and the process's exit waited on the loader lock. The set now lasts as long as the process, as its lock already did
* Fixed a process built with stack traces (-debug, HXCPP_STACK_TRACE, telemetry) or the debugger crashing as it exits while its threads end: an ending thread also removes itself from the stack map and the debugger's thread lists, statics the main thread had destroyed by then. They now last as long as the process
* Fixed the collector walking the GC roots without their lock: a thread that is starting or ending, or native code in a GC-free zone or on a thread hxcpp never knew, adds or removes roots without waiting for a collection, and the walk could follow a node erased or relinked under it - an access violation in the collector, or roots passed over and their objects freed. The collector now holds the root lock for the two walks only
* Fixed heap corruption in the moving/compacting GC (HXCPP_GC_MOVING) with HXCPP_ALIGN_ALLOC: the alignment padding added to the destination position was not deducted from the remaining hole length, so the free-space count drifted and an object could be relocated past the end of a block, corrupting adjacent objects
* Fixed the moving GC not adjusting pointers held by large (non-block) objects after a compaction, leaving them dangling
* Fixed compilation with HXCPP_GC_DYNAMIC_SIZE
* Fixed GC block row counts overflowing with HXCPP_GC_BIG_BLOCKS, which made the GC underestimate the live heap and collect on almost every new block
* Fixed HXCPP_GC_VERIFY reporting an unmarked row with the generational collector: its verification mark advanced the mark id a second time each collection, so dead objects' stale entries could match the current mark before they were cleared (hxcpp #1415)
* Fixed HXCPP_GC_BIG_BLOCKS stalling or running out of memory once the heap passed 2 GiB: the large allocation target was computed in an int (hxcpp #1339)
* Fixed a weak-key map keeping alive a value that references its own key, and so the key and everything reachable from it: values are marked only while their key is (hxcpp #1357)
* Fixed the generational GC freeing a thread's slot and scratch arrays: a collection while the thread object was being constructed promoted it, and its arrays were then stored without the write barrier
* Fixed a thread's scratch offset starting uninitialised, and a new thread being able to read its id before it was set
* Fixed the critical error handler never being rooted, so a collection could free it before a critical error called it
* Fixed more generational GC write-barrier gaps: dynamic iterators, enum parameters and strings, socket select results, file errors, MySQL field names, CFFI abstract toString, bound this pointers, and cppia dynamic members, call arguments, array conversion, JIT this-field updates and JIT enum construction
* Fixed a collection starting on another thread corrupting a block's fragmented row count, which skews the heap statistics that size working memory
* Fixed the members of large container objects (4000 bytes or more, such as cppia classes with many fields) never being marked, so they were freed while still referenced; with the generational GC marking one also corrupted block metadata
* Fixed weak references and weak map keys to constant objects (small boxed Ints, Bools, one-character and permanent strings) being cleared while still held
* Fixed allocating threads zeroing free blocks they then drop while a collection is pending
* Fixed constant allocations (Class objects, boxed one-character strings, frozen objects) being only 4-byte aligned
* Fixed the moving GC ignoring the pin flag on alloc finalizers, so val_gc_ptr allocations could move under native code
* Fixed the collector on ARM64 possibly missing a thread entering or leaving a GC-free zone
* Fixed a thread attaching while the GC switched to generational mode getting no remembered set, which crashed the next generational collection
* Fixed Sys.exit destroying the GC's containers and locks while other threads could still allocate
* Fixed a rare race where a thread starting while another thread collects could be handed rows that other threads are also given, so two live objects shared memory
* Fixed the GC freeing an object that a thread paused for a collection, or in a GC-free zone, held only in a register, on 32-bit Windows: the pausing function had saved that register below the scanned stack. Every target now copies the pausing frame along with the registers, as Windows x64 already did
* Fixed a weak map emptied by the GC keeping, and scanning on every collection, the bucket array of the size it had reached (2 MB after 100k keys); the array is dropped once the last key is collected
* Fixed an object held only through a @:nativeGen interface being freed: converting an object to the interface directly went through ObjectPtr's conversion operator, which skipped registering the interface's offset with the collector
* Fixed weak references to a method wrapped in a callable adapter (Haxe 5 signature adapters) being cleared while the method's receiver was still alive; the collector now follows the adapter chain to the receiver
* Fixed _hx_gc_freeze duplicating objects referenced more than once, recursing forever on cycles, and crashing in generational builds (it misread the size of nursery allocations)
* Fixed data races in the large-allocation path: the collection trigger, the large-allocation budget and the recycle hint were read and written by several threads without synchronization, and freeing a large buffer checked the recycle list outside its lock
* The large-allocation allowance now halves at each collection, down to what the heap needs, instead of keeping its peak for the life of the process: after one 400 MB buffer, later garbage no longer piled up to 800 MB before a collection
* An empty HXCPP_MINIMUM_WORKING_MEMORY, HXCPP_MINIMUM_FREE_SPACE or HXCPP_TARGET_FREE_SPACE is treated as unset again instead of stopping the program with a critical error
* Fixed programs with many threads allocating at once stopping with "Memory exhausted" while the heap was far from full: other threads could claim every block a thread had just added before it took one (HaxeFoundation/hxcpp#1289)
* Fixed releasing more managed memory than was reported wrapping the GC's large-allocation count, after which every GCChangeManagedMemory call forced a collection; negative GC tuning values and HXCPP_MINIMUM_WORKING_MEMORY, HXCPP_MINIMUM_FREE_SPACE and HXCPP_TARGET_FREE_SPACE values no longer become huge ones that stop collections, and cpp.vm.Gc's Int memory figures saturate instead of wrapping above 2 GB
* Gc.memInfo64(4) reports live bytes: the bytes of the objects that survived the last full collection (every major collection is one), next to MEM_INFO_USAGE, which counts every 128-byte line a live object touches, so fragmentation can be measured. The value has no named constant in Haxe's cpp.vm.Gc yet (libhxcpp #83)
* __hxcpp_gc_census() counts the live objects of each class, with their bytes, in any build, so a leak can be found without rebuilding in debug for Gc.trace. Call it as untyped __global__.__hxcpp_gc_census(); it runs a major collection and returns rows of { name, count, bytes }, most bytes first. An object with no pointer fields that only a local variable holds is not counted, since on the stack it cannot be told from raw memory (libhxcpp #82)
* GC debugging aids are switched on with defines instead of by editing Immix.cpp: HXCPP_GC_SHOW_MEM_EVENTS, HXCPP_GC_SHOW_FRAGMENTATION, HXCPP_GC_PROFILE_COLLECT, HXCPP_GC_FIXED_BLOCKS and HXCPP_GC_WATCH=<address>. Fixed HX_WATCH builds crashing at their first collection, a collector report printed just before a trap being lost in the output buffer, and the memory event log printing block counts as the wrong type on 64-bit (libhxcpp #87)
* Fixed threads allocating while another thread added blocks to the heap crashing on ARM64, such as Apple Silicon Macs, with a null block in GlobalAllocator::GetNextFree: the free-block list's new slot and its larger size were written with nothing ordering them, so a thread reading the list without the lock could see the size first and read a slot not written yet (libhxcpp #254, HaxeFoundation/hxcpp#1289)

### Threads

* An uncaught exception in a sys.thread.Thread now prints the exception instead of silently terminating the whole process with no diagnostic; the thread's closure is released when it finishes instead of being pinned by the thread handle
* Fixed sys.thread.Lock timed waits breaking after ~24.8 days of process uptime on Windows (was built on the 32-bit clock(); now uses the performance counter, as haxe.Timer.stamp does - the tick count first used instead moves only every ~15.6ms, which made every timed wait come due up to a tick late)
* Fixed every Thread.create on Windows leaking a kernel thread handle for the process lifetime
* Fixed native threads that touch a Haxe thread API leaking a semaphore (a kernel event handle on Windows) per thread, and Lock.wait/timed waits overflowing for very large timeout values
* Fixed a lost wakeup in sys.thread.Deque on Windows: with multiple blocked consumers, coalesced signals could leave a consumer asleep while items sat in the queue
* Fixed sys.thread.Semaphore on Linux throwing spurious exceptions when a signal interrupts acquire/tryAcquire (EINTR is now retried)
* Fixed sys.thread.Semaphore.tryAcquire on Apple platforms truncating fractional timeouts to whole seconds (0.5 became 0), and a dispatch semaphore leak per Semaphore object
* Fixed Lock and Semaphore timeouts and haxe.Timer.stamp on Linux, Android and Apple following the wall clock: changing the system time cut a wait short or stretched it by the change. They use the monotonic clock (hxcpp #887)
* Fixed a thread polling Mutex.tryAcquire, Condition.tryAcquire, Semaphore.tryAcquire(0) or Lock.wait(0) without allocating deadlocking the process when another thread started a collection
* Fixed a failure to start a thread terminating the process; Thread.create throws "Could not create thread" again
* Fixed collecting a Semaphore with permits still taken crashing on macOS and iOS, and Semaphore(-1) crashing there on first use
* Fixed thread wait timeouts that were negative, NaN, huge or under a millisecond failing, spinning or not waiting
* Fixed Windows semaphores refusing more than 134 million permits, and failed semaphore construction leaking its native part
* Fixed natively attached threads all getting the main thread's id 0, and created threads possibly getting 0 too
* Fixed a ThreadLocal reading the value of the collected ThreadLocal whose slot it reused, or a legacy __hxcpp_tls value with the same id
* Fixed debugger, debug and telemetry builds aborting on macOS when a thread ends after main returns: the debugger's and the stack map's mutexes were destroyed with the other statics, and libc++ throws when a destroyed mutex is locked
* Fixed Semaphore.tryAcquire throwing "Failed to wait for semaphore" on Linux and Android for negative, NaN and huge timeouts, and returning early on Windows for huge ones; sub-millisecond waits on Windows now round up instead of returning at once, so Lock.wait no longer busy-polls the last millisecond of a timed wait, and Lock.wait with a huge timeout no longer spins where time_t is 32-bit
* Fixed the Windows main and natively attached thread objects holding GetCurrentThread's pseudo-handle, so setName and getName called from another thread named that thread instead; and fixed Scratch's copy assignment (Haxe 5) keeping its old view and release function and freeing the malloc path twice
* Made hx::NativeAttach and hx::AutoGCFreeZone non-copyable: a copy duplicated the guard's ownership, so one scope detached the thread or left the GC-free zone twice
* Fixed values stored in a ThreadLocal staying alive for the life of every thread that set them after the ThreadLocal itself was collected
* Fixed hx::thread::Scratch (Haxe 5) handing out a live buffer again after allocations were released out of order, and placing reference counters and data at unaligned offsets

### Strings, numbers, Dynamic and reflection

* Fixed String.substr with a large length and a positive start, such as `"abc".substr(1, 0x7fffffff)`, ending the process with Memory Exhausted: the length plus the start overflowed an Int, so the length was not clamped to the characters left (libhxcpp #279)
* Fixed an ObjectMap holding Int64 values rounding them all to the nearest Float when a Float was set: it converted its Int64 storage to Float storage, which loses every value past 2^53. It now converts to Dynamic storage, as the other maps do (libhxcpp #282)
* Fixed a field write through reflection to a boxed Int64 from -1 to 255 changing every other boxing of that value: those boxes are shared constants. Writing `hi` or `lo` on one now throws, and a box of a value outside that range can still be changed (libhxcpp #295)
* Fixed setting `hi` on a boxed Int64 through reflection giving a high word of all ones whenever the low word's top bit was set: the low word was sign-extended before being combined (libhxcpp #296)
* Fixed dividing the minimum Int64 by -1, or taking its remainder, ending the process with an integer overflow trap on x86. The quotient now wraps to the minimum and the remainder is 0, as on the other Haxe targets (libhxcpp #326)
* Made Std.parseFloat and Float-to-String formatting independent of the process locale: host frameworks (GUI toolkits, audio libraries, plugin containers) that call setlocale after startup no longer flip the decimal separator and break float parsing, printing, and serialization (uses explicit "C" numeric locale on Windows, macOS, glibc and Android 21+)
* Calling a Dynamic value that is not a function now throws "Cannot call ..." at API level 500 (Haxe 5) instead of silently returning null, matching the other targets; API level 430 and below keep the old behavior
* Math.round/fround no longer double-round (the largest double below 0.5 rounded to 1 where every other target gives 0), Math.floor/ceil/round of NaN is a defined 0 on every platform (was undefined per-platform behavior), and Type.getClassFields(Math) reports the full field list
* cpp.encoding hardening (new marshal API surface): Ascii.decode no longer truncates at the first NUL byte; malformed UTF-8 (bad continuation bytes, overlong forms, codepoints beyond U+10FFFF) throws instead of producing mojibake, desynchronized decodes, or leaking uninitialized memory into the decoded String; decoded ASCII/char strings are properly NUL-terminated; a lone trailing surrogate reports "Invalid UTF16" instead of an internal view error; the all-ASCII detection is a byte scan instead of a triple decode
* Type.resolveClass no longer crashes when called by a native host before any class has booted
* Normalized float exponent formatting to minimal digits ("1e-7" instead of "1e-07"), matching other targets
* UTF-8 decoding replaces malformed input with U+FFFD instead of decoding it to unrelated characters: stray and missing continuation bytes, overlong forms (such as C0 AF for /), encoded surrogates and values above U+10FFFF
* Haxe code is compiled with -fwrapv on gcc and clang, so Int overflow wraps as it does on the other targets instead of being undefined behaviour the optimiser may exploit
* Fixed Dynamic + with boxed Int64 operands producing a string concatenation of the two numbers; Int64 addition now stays in 64-bit math (no precision loss via double), and Int + Int keeps an Int-typed result consistent with - and * (also avoids boxing a double per addition)
* Fixed equal strings hashing differently and missing each other in string maps in two cases: strings created from bytes (haxe.io.Bytes.toString, file/socket reads of non-ASCII text) stored their pre-computed hash at an address the hash reader did not use for UTF-16 strings, and strings containing astral-plane characters (emoji) hashed surrogate halves individually while compile-time literals hashed real UTF-8 - so a literal key and an equal runtime-built key could not find each other
* Fixed comparing a statically-typed number with a Dynamic skipping the runtime type check: 5 == ("5":Dynamic) was true (the string was parsed as a number) and 0 compared equal to non-numeric objects; mixed string/number comparisons also answered != incorrectly
* Fixed ++/-- on Dynamic values and untyped fields rewriting Int values as Float (disabling downstream Int fast paths) - Int values now stay Int
* Fixed converting a null function value across compatible Callable signatures producing a non-null callable (the universal `if (callback != null) callback()` idiom then threw instead of skipping)
* Fixed out-of-bounds read looking up a missing field on an anonymous object with exactly 5 fields, and missed lookups when field-name hashes collide
* Fixed String.fromCharCode with negative codes corrupting global memory (now throws), and made its lazy lookup-table init thread-safe
* Fixed charAt/substr on non-ASCII byte strings in legacy (non-smart-strings) builds passing negative char codes
* Fixed UTF-16 surrogate-pair validation: a lone high surrogate no longer swallows the following character, produces corrupt code points, or scans past the end of the buffer when converting to UTF-8 (out-of-bounds read)
* Fixed comparison of boxed Int64 values above 2^53 (was routed through double, making distinct values compare equal)
* Fixed null Dynamic operands crashing in -, *, ++ and -- (now treated as 0, consistent with +, / and %)
* Fixed Std.parseInt undefined behaviour passing non-ASCII chars to isspace, and made overflow behaviour platform-independent (decimal values saturate to the Int range instead of depending on the platform's long size)
* Fixed String(unsigned int) appending a literal "d" (from a "%ud" format) for raw unsigned ints formatted via the runtime
* Fixed Std.string of NaN/Infinity to match other targets ("NaN", "Infinity", "-Infinity") instead of platform printf output ("-nan(ind)", "inf")
* Fixed Std.string(Float) losing precision: it now prints the fewest digits that read back as the same Float (0.1+0.2 prints "0.30000000000000004", the smallest denormal "5e-324"), so String<->parseFloat is lossless and matches other targets
* Fixed out-of-bounds read when deleting a fixed field of an anonymous object (Reflect.deleteField)
* Fixed String.indexOf with a negative start index reading out of bounds and returning a bogus negative result (now clamps to 0, matching other targets)
* Fixed String.lastIndexOf with a negative start index returning -1 instead of matching at index 0 (now clamps to 0, matching other targets)
* Fixed Dynamic modulo (%) dereferencing a null operand without a guard (now coerces null to 0 like the other dynamic operators)
* Fixed a Dynamic or Any Bool comparing equal to the number 1 (or 0): a Bool known only at run time now equals only another Bool, as on the other targets (hxcpp #1009)
* Fixed small Int64 values (-1 to 255) boxing as Int: they came from the boxed Int cache, so Type.typeof and the Int64 type checks saw an Int. They have a cache of their own (hxcpp #1278)
* Fixed long and unsigned long, and so size_t, being ambiguous when converted to Dynamic on macOS, where int64_t is long long: an Int default of -2147483648 failed to compile there (hxcpp #621, #1214)
* Fixed cpp.encoding.Utf16 without smart strings (-D disable_unicode_strings): decode works instead of throwing, and encode and the byte and char counts read the string as UTF-8 instead of one character per byte
* Fixed cpp.encoding.Ascii without smart strings taking any string as ASCII: isEncoded checks the bytes and encode rejects non-ASCII text
* Fixed Int -2147483648 % -1 crashing the process; it gives 0, as on the other targets
* Fixed Type.createInstance and Type.createEmptyInstance crashing on classes without a constructor (interfaces, Int, Math...); they throw
* Fixed wchar_t strings with values above U+10FFFF overrunning their buffer, and wchar_str() and ConvertToWChar mangling characters outside the BMP where wchar_t is 32 bits
* Fixed haxe.Utf8.charCodeAt with a negative index and NativeString.parseSubstrFloat with a negative start or huge length reading outside the string
* Fixed string headers and memoized hashes being read and written through misaligned pointers
* Fixed cpp::Variant's <= with a String acting as <, so equal strings compared false (native code and externs; generated code was not affected)
* Fixed cpp.NativeString.parseInt and parseSubstrInt failing to link, as the runtime never defined __hxcpp_parse_substr_int. They give 0 for text that is not a number
* Fixed StringTools.urlDecode replacing every character above 127 in a non-ASCII string with ?
* Fixed anonymous objects not finding fields with non-ASCII names: Reflect.field read null, hasField was false, and setField added a second field of the same name
* Fixed Bytes.ofString, StringTools.urlEncode and utf8_str() throwing Invalid UTF16 on a string holding a lone surrogate; it is encoded as U+FFFD, as trace and Sys.print already did
* Fixed comparing a String with a Dynamic depending on the static types: ("5":String) == (5:Dynamic) and ("true":String) == (true:Dynamic) were true; a Dynamic operand now goes through the runtime type check
* Fixed -0.0 losing its sign when boxed: it came from the small Int cache, so 1/(d:Float) was Infinity and Type.typeof(d) was TInt
* Fixed Std.parseFloat reading words and hex as numbers through strtod: "info" and "inferior" gave Infinity and "0x10" gave 16. They give NaN, NaN and 0, as the Haxe spec says; "Infinity" is still read, as JavaScript reads it
* Fixed haxe.Utf8 keeping only the low 16 bits of characters above U+FFFF, and writing invalid code points as they were; they become U+FFFD. Without smart strings it also read past the requested range
* Fixed toUpperCase and toLowerCase on a non-ASCII string returning a string that did not equal the same ASCII text: "\u0131".toUpperCase() == "I" was false, so map lookups and switches on it missed
* Fixed substr and substring replacing half of a split surrogate pair with U+FFFD, so s.substr(0, 2) + s.substr(2) did not give back s; the halves are kept, as JavaScript keeps them, and still encode to UTF-8 as U+FFFD
* Fixed Reflect.hasField returning false for every field of a fixed anonymous object (@:fixed, up to 5 fields) that Reflect.field and Reflect.fields found
* Fixed formatting an anonymous object on one thread making another thread's anonymous objects print as "...": the nesting limit was one counter for the whole process; it is per thread

### Arrays and Bytes

* Fixed a search of an Array<Dynamic> for a value of another type than its elements, such as `indexOf("missing")` on one holding Ints, converting the whole array as if the value were being added: contains, indexOf, lastIndexOf and remove boxed every element, or rewrote Int elements as Float for a Float, even on a miss. A search now compares the elements where they are, with the same results (libhxcpp #281)
* Fixed formatting an array in one thread printing `[...]` while another thread was formatting a nested array: the recursion depth that stops at 5 levels was shared by every thread. It is now per thread, as an anonymous object's already was (libhxcpp #277)
* Out-of-range reads on an untyped array now return null instead of a boxed 0/false from the typed backing store
* Mixing Int64 and Float values in an untyped array now promotes to object storage instead of silently rounding Int64 values beyond 2^53 through a double
* Fixed Array<Dynamic>.concat/blit with an empty untyped operand silently corrupting the receiver: it installed byte storage, so a later push(300) stored 44 and push(-1) stored 255
* Fixed Array<Dynamic>.resize on a fresh untyped array: the result reported length 0, pop/shift/copy/slice/concat ignored the elements, and the first typed push rewrote the null elements as 0/false
* Fixed untyped-array equality operators: comparing a wrapped array with null was inverted, == against a typed array permanently froze and retyped the array as a side effect of comparing, and == / != could both be true for the same operands (now identity comparison, like all other array comparisons)
* Fixed memcmp on an empty untyped array returning inverted results (reported two empty arrays as different and an empty vs non-empty as equal)
* Fixed bool-element untyped arrays mislabeling their storage after copy/slice/concat/splice, which made a later push(5) silently store true
* Fixed heap corruption sorting arrays of Bool or small integer element types (Array&lt;Bool&gt;.sort, cpp.UInt8/Int16/etc): the buffer was reinterpreted as an array of Dynamic and read/written out of bounds
* Fixed Array.resize with a negative size zeroing memory before the array buffer
* Fixed integer overflow in array growth size math: huge reserve/resize requests now throw a catchable exception instead of corrupting the heap, and pushing past ~2GB of buffer no longer hangs in an infinite loop
* Fixed integer overflow in Array.splice with huge lengths (now clamps like other targets) and rejected negative element counts in array blit
* Fixed haxe.io.Bytes.fill with a negative position writing before the buffer (GC heap corruption)
* Fixed array sizes, blit offsets, out-of-range writes and join lengths overflowing int and writing outside the buffer; they throw instead
* Fixed shrinking an array with unmanaged data to zero handing that memory to the GC
* Fixed Array.sort crashing, corrupting the heap or hanging when the comparator is inconsistent (one returning random results to shuffle, for example) or changes the array it sorts: both sorts use a stable merge sort that stays in range whatever the comparator returns
* Fixed copying an array crashing with telemetry's allocation tracking on: the tracker read the type of an object whose constructor had not run
* Fixed Array.filter called through reflection or a Dynamic keeping the value a predicate wrote into the slot instead of the value it was given, so it disagreed with a direct call
* Fixed cpp.marshal.View (Haxe 5) reinterpret and compare narrowing 64-bit lengths: reinterpreting a view of 4 GiB or more to a wider element type gave the wrong length, and compare could report views of different lengths as equal; fill uses a 64-bit index

### Sockets and TLS

* Fixed a peer certificate from sys.ssl.Socket.peerCertificate() reading freed memory once the socket closed: it pointed into the TLS session's copy, which closing frees. It is now a copy of its own. A certificate from Certificate.next also keeps the certificate whose chain it is in alive, which it did not, so collecting that one first left it pointing at freed memory (libhxcpp #291)
* Fixed cpp.net.Poll on Windows missing a failed nonblocking connect after the first 64 read sockets, or on a socket polled only for writing: Windows reports the failure only in the exception set, which held just the first 64 read sockets. It now holds every read and write socket, and a failure is reported for the list its socket is in, as poll's POLLERR is elsewhere (libhxcpp #304)
* Fixed a prepared cpp.net.Poll not keeping its sockets alive: it held only their descriptors, so once nothing else referenced a registered socket, a collection finalized it and closed the connection while the poll was still watching it. The poll now keeps the sockets of its last prepare (libhxcpp #286)
* TLS connections now require TLS 1.2 or newer - the bundled mbedtls preset still negotiated the deprecated TLS 1.0/1.1 with downgrade-capable peers
* A UDP datagram larger than the receive buffer on Windows now returns the truncated data like posix instead of throwing a spurious "EOF" and discarding it (WSAEMSGSIZE)
* Added the missing EINTR retry to socket send and accept - any signal (profilers, child-process reaping) made them throw a bogus "EOF" and the connection got closed; an interrupted blocking connect now reports "Blocking" (the connect continues asynchronously per posix) instead of "EOF"
* Socket listen and setBlocking failures now throw instead of failing silently (a failed listen left the app believing it was accepting connections); shutdown throws except for the defensive not-connected case
* Accepted sockets are now close-on-exec (posix) like freshly created ones: accepted connections leaked into child processes
* Socket close no longer retries on EINTR (posix releases the descriptor regardless, so the retry could close a descriptor just handed to another thread) and runs in the GC free zone so a lingering close cannot stall collection
* Hardened the socket/TLS buffer bounds checks against integer overflow in position+length
* Updated mbedtls to 3.6.7, the long-term-support release (from 2.28.2, whose support ended in 2024): TLS 1.3 is negotiated when the peer offers it, and MSVC builds use AES-NI. Native code that calls mbedtls directly meets the 3.x API (see mbedtls's docs/3.0-migration-guide.md)
* Poll now reports a socket that closed or failed: on Windows it also waits on the error set, and when select() fails every registered read socket is reported so the caller finds the dead one; on Linux and macOS POLLERR and POLLNVAL count as ready
* TLS servers resume sessions with RFC 5077 tickets, so a returning client skips the full handshake (30 to 50 times more returning-client connections a second in testing); -D HXCPP_SSL_NO_TICKETS builds without them
* mbedTLS's symbols are compiled hidden under every toolchain, including watchOS's and custom ones, so a library loaded later with its own mbedTLS (a Lime ndll, for example) cannot bind to hxcpp's copy
* UDP sockets no longer get SO_REUSEADDR outside Windows: on Linux it let two sockets share a port, so a bind to port 0 could hand out one already in use
* Fixed an operator-precedence bug destroying every mbedtls error code reported from SSL (eleven call sites reported "UNKNOWN ERROR CODE (0001)" instead of the real failure)
* Fixed sys.ssl writeByte silently dropping the byte on would-block/error and readByte turning a non-blocking retry into a spurious end-of-file
* Fixed Socket.select smashing the stack on Linux/macOS when a descriptor number reaches FD_SETSIZE (1024) - the guard only counted sockets, valid on Windows where fd_set is a counted array, while posix fd_set is a fixed bitmap; selecting on a closed socket (fd -1) had the same effect and both now throw a descriptive error
* Fixed sys.net/sys.ssl reads and writes that hold a raw pointer into a GC array across a blocking call: TLS reads/writes now stage through a stack buffer (mbedtls re-enters the GC-visible world mid-call) and plain socket send/recv pin the buffer in the scanned frame, closing a heap-corruption window in moving-GC builds
* Fixed Socket.connect doing a dynamic field lookup (and potentially throwing) inside the GC free zone - a caught "Invalid socket handle" left the thread permanently marked as parked, letting the collector run concurrently with live code
* Fixed Socket.setTimeout: a negative value configured a random timeout from uninitialized stack memory on posix (now throws), a sub-millisecond value on Windows rounded to 0 = block forever (now 1ms minimum), and huge values overflowed; setting the timeout now reports failure instead of silently doing nothing
* Fixed Socket.select on Windows reporting errno (always garbage) instead of WSAGetLastError, and both selects now sample the error code before leaving the GC free zone, which can clobber it
* Fixed socket poll reporting sockets from a previous poll as ready after a poll error (stale index lists are now terminated)
* Fixed sockets leaking into child processes on Windows: created sockets are no longer inheritable (WSA_FLAG_NO_HANDLE_INHERIT), accepted ones have the flag cleared, and sys.io.Process hands a child only its own three pipe ends (PROC_THREAD_ATTRIBUTE_HANDLE_LIST) - a connection the server closed stayed open until every child started while it was open had exited, and a closed listener kept taking connections. On Linux sockets are now created and accepted close-on-exec atomically (SOCK_CLOEXEC, accept4), leaving no window for a process another thread starts
* Fixed resolving "255.255.255.255" (UDP limited broadcast) throwing "Unknown host" - inet_addr's error value collides with the broadcast address
* Fixed sys.ssl Certificate loading with a null CA chain crashing (the null check tested the wrong handle), and message digests over-allocating their result 4x
* Fixed sys.ssl.Digest.sign and verify overflowing a 32-byte buffer with SHA-384 and SHA-512 digests
* Fixed mbedtls on Windows failing every lock -- "CTR_DRBG - The entropy source failed" -- for code that is not hxcpp's in a program that never used sys.ssl, such as a static Lime build's curl: its mutexes are now installed before main
* Fixed closing a listening sys.ssl.Socket freeing the TLS configuration its accepted connections run on: they read freed memory on every record afterwards, which on mbedtls 3.6 and Linux crashed in a renegotiation the garbage asked for. A configuration now lives until its handle is closed and the last connection set up on it has gone
* Fixed TLS certificate verification failing on iOS and tvOS, which have no root store to load: a chain mbedTLS cannot trust is judged by the system's trust, and a wrong hostname, an expired certificate or a bad signature is still an error (hxcpp #1331, #570)
* Fixed an SSL server crashing when an accepted socket handshakes after its listener was closed and collected, and, on Haxe 5, any SNI handshake after a collection: the SNI callback, certificate, key and CA the configuration uses were not kept alive
* Fixed sending or receiving with an empty Bytes growing its data to length 1
* Fixed SSL read and write crashing on TLS errors such as a corrupted record from the peer or writing after close: mbedTLS error codes were used as byte counts; they throw the mbedTLS error now
* Fixed the SSL SNI callback reading the server name past its length
* Fixed the SSL socket binding accepting any value as a socket handle; it throws "Invalid socket handle" like the other socket functions
* Fixed macOS leaking the whole system root certificate store each time the default certificates were loaded for a TLS context
* Fixed socket polling on 64-bit Windows: fd_set allocations and copies were sized 4 bytes short of the socket array, so select() was handed a corrupted socket, failed on every call, and Poll reported no socket ready
* Fixed a TLS write to a peer that had gone (one that left mid-handshake, or closed without close_notify) ending the process with SIGPIPE on Linux; it now fails like a plain socket write
* Fixed formatting a host address on macOS and the BSDs returning "[inet_ntoa error]" when several threads did it at once; it now uses inet_ntop into a buffer of its own
* Added mbedTLS providers: in a static build, a library that bundles mbedTLS (Lime 8.4.0 does) can set hxcpp_mbedtls_provider to a file describing its copy, and sys.ssl is built on that copy instead of libhxcpp's own, so the app links one mbedTLS. docs/MbedTLSProvider.md describes it
* Fixed a socket dropped without close keeping its descriptor open for the life of the process; a collected socket closes it. Close sockets explicitly for timely release
* Fixed a positive socket timeout under a microsecond turning into no timeout on POSIX; it rounds up to one microsecond, as Windows already rounded sub-millisecond timeouts up
* Fixed Socket.select, fast_select and Poll.events starting their whole timeout again after every signal on POSIX, so a stream of signals could keep a 50 ms wait blocked indefinitely; an interrupted wait goes on for what is left of it

### Files, processes and the system

* Fixed Process.close on Windows waiting while another thread read the child's output and a descendant of the child still held the pipe, as a runner that reads on a thread and closes at a timeout does: CloseHandle waited for the pending ReadFile until the descendant exited. Close now refuses new pipe reads and writes and cancels the ones in progress with CancelIoEx first (libhxcpp #261)
* Improved Sys.time() resolution on Windows from ~15.6ms to sub-microsecond (GetSystemTimePreciseAsFileTime with a Win7 fallback)
* Writing to a dead child process's stdin no longer kills the whole host process via SIGPIPE on Linux/macOS - it throws like other closed-stream writes (SIGPIPE is now ignored process-wide once sys.io.Process is used, and restored to its default in the children it starts)
* Process.kill/exitCode/getPid after close() now throw instead of operating on a recycled OS handle - kill() could terminate an unrelated process and exitCode() hang forever
* A child process killed by a signal (crash, kill()) no longer reports exit code 0: exitCode() returns 128+signal (shell convention); a command that cannot be executed reports 127 instead of 1
* Process pipe reads/writes now retry on EINTR instead of reporting a bogus EOF mid-stream (silent output truncation under signals), and a failed stdin write throws instead of making writeFullBytes spin forever
* Process pipes are now close-on-exec: concurrently spawned children no longer inherit each other's descriptors, which held stdout/stderr open so reads hung past child exit; closing an unconsumed process also reaps the zombie if it already exited
* The forked child no longer allocates GC memory in the exec-failure path (deadlock risk in multithreaded apps) and uses _exit instead of running the parent's atexit handlers; a quote embedded in the Windows command name is rejected instead of smuggling extra arguments
* Sys.setCwd throws when the directory cannot be set, as the other targets do, and on Windows updates the drive's current directory variable (=X:) as the CRT does
* Process start failures on Windows say why: the native error carries the system message and error code
* FileSystem.stat throws for a path that cannot be read, as HL, neko and eval do, instead of returning a FileStat of zeros
* Fixed Sys.sleep with a negative duration hanging ~49.7 days on Windows (schedulers computing sleep(deadline-now) could dip below zero)
* Fixed FileSystem.kind/isDirectory misclassifying special files (the S_IF* constants were tested as bit flags: sockets reported as "file", block devices as "dir")
* Fixed File.getContent/getBytes silently returning truncated data for huge files (32-bit length math), leaking the file handle on the length-error path, and now reporting "file too large" instead of wrapping
* Fixed FileSystem.fullPath on Windows returning uninitialized memory for paths longer than MAX_PATH, and a one-character stack overflow in readDirectory at exactly MAX_PATH
* Fixed a race between Sys.getEnv and Sys.putEnv (putenv can free the storage a concurrent getenv result points into) - environment access is now serialized
* Fixed Process.exitCode(false) on Windows conflating exit code 259 with "still running" (could poll forever) and reading an uninitialized exit code when the status query failed; on Linux a stale EINTR turned the non-blocking poll into a 100% CPU busy-wait
* Fixed sys.io.Process resource leaks: a failed CreateProcess (e.g. executable not found) leaked six pipe handles per attempt on Windows, partial pipe/fork failures leaked descriptors on posix, and an unchecked CreatePipe could close arbitrary process handles via uninitialized stack values
* Fixed crash writing to the null device on Windows
* Fixed Windows console writes larger than 4096 UTF-16 units being silently discarded while reporting success (output is now converted and written in chunks), and fixed the broken surrogate constants in the partial-write accounting
* Fixed sys.io.Process.close followed by stdin.close closing a stale (possibly recycled by the OS) handle
* Fixed Date where time_t is 32 bits (32-bit Linux and Android): new Date(...) after 2038 or before 1902 gave 1969-12-31 23:59:59, and such a time read back wrapped round. Those dates are converted by calendar arithmetic, with the zone's offset taken 28 years closer, where the calendar repeats
* Fixed Sys.print and Sys.println of a null value printing (null), or crashing where the compiler turns the printf into puts(NULL); they print null (hxcpp #964)
* Fixed FileStat.ctime on Linux, macOS, FreeBSD and NetBSD reporting when the file's status last changed: it is the creation time, as on Windows, wherever the file system records one (hxcpp #1063)
* Fixed a failed file write (disk full, a closed pipe) aborting the process with "GCFree Zone mismatch" instead of throwing
* Fixed Sys.getCwd returning null for long paths, reading an unfilled buffer for long paths on Windows, and Sys.programPath writing past its buffer on Linux
* Fixed Process.exitCode returning 0 when called again on POSIX, and kill and close acting on a pid that was already reaped
* Fixed Date.getTimezoneOffset on Windows leaving out daylight saving time (300 in New York in July, where the other targets give 240)
* Fixed Sys.environment on Windows garbling non-ASCII values, such as a user profile path, by decoding the ANSI code page as UTF-8; it reads the wide environment, as Sys.getEnv does
* Fixed a negative time with milliseconds landing on the wrong second: Date.fromTime(-500) was 1970-01-01 00:00:00 UTC instead of 1969-12-31 23:59:59
* Fixed FileOutput.close and File.saveContent reporting success when the final write fails at close (a full disk, /dev/full); close throws file_close, as the other file operations do
* Fixed files opened through sys.io.File leaking into child processes on POSIX: they are opened close-on-exec, as sockets and process pipes already are
* Fixed Process.exitCode returning 0 when the child's status cannot be read (the host reaped it, or the wait failed); it throws process_exit with the reason, on POSIX and Windows
* Fixed a POSIX process that fails to start (missing, or not executable) being created anyway and exiting 127; the constructor fails with the system's reason, as on Windows, and a program that really exits 127 is unaffected. Pipe and fork failures report their reason too
* Fixed the first POSIX process launch replacing the host's SIGPIPE handler with SIG_IGN; SIGPIPE is blocked only in the thread writing to a child's stdin, for that write, which still throws EOF when the child has gone
* Process pipe reads and writes of zero bytes return 0 at once, without touching the buffer or entering a GC-free zone
* Fixed File.getContent and getBytes returning nothing for files that report a size of 0, such as Linux /proc files, and failing on unseekable inputs; they read to the end when the size is zero or unknown, and keep what was read when a file holds less than its size said
* Fixed a POSIX child starting with its stdin, stdout or stderr closed when the parent had that descriptor closed: the pipe took the same number and kept close-on-exec through a dup2 onto itself
* _hx_std_process_communicate(process, input, timeout, maxOutput) runs a child without the pipe deadlocks of the blocking stream API: it writes stdin while draining stdout and stderr together, can keep only the first maxOutput bytes of each, kills the child at a deadline, and returns once the child exits even when a descendant keeps a pipe open. Haxe's sys.io.Process has no method for it yet; call it through untyped __global__ with the process's handle (libhxcpp #214)

### Regular expressions, zlib and databases

* Fixed a SQLite result set that outlived every other reference to its connection losing its unread rows: a collection finalized the connection, which closed the database and the live statement with it, and iterating then ended without an error. A result now keeps its connection alive (libhxcpp #290)
* Fixed every SQLite result adding its column names to the permanent string pool, so a program that ran queries with changing column names, such as generated aliases, kept every name it had ever seen. The names are ordinary strings now, collected with the result and its rows: an anonymous object marks the names of its fixed fields, which a full collection of a million of them does not measurably slow (libhxcpp #274)
* Fixed every failed sys.db.Sqlite.open leaking its native connection handle: SQLite allocates one even when the open fails, and only closing it frees it (libhxcpp #273)
* Fixed SQLite TEXT values that hold a NUL character coming back cut off at it, from a row's fields and from getResult: they were read as C strings instead of by their byte count (libhxcpp #272)
* A zlib stream requesting a preset dictionary (FDICT) now throws instead of looping forever (denial of service on hostile input); zlib failures now include the error code and zlib's message text instead of a bare "ZLib Error"
* MySQL: logs in to MySQL 8 and later servers (caching_sha2_password, sha256_password and auth switch requests, with the password RSA-encrypted under the server's key when the connection is not encrypted) and connects over TLS, with sslMode from disabled to verifying the server's identity. MySQL 8's default collation, utf8mb4_0900_ai_ci, and the other missing charsets are known
* MySQL: connectTimeout, readTimeout and writeTimeout bound every wait, and the connect timeout is one deadline for the whole open. TCP keepalive can be enabled and read back. Errors carry the server's error number and SQLSTATE and never include the statement, and a lost connection fails with CR_SERVER_LOST. New ping, errno, sqlstate and thread id accessors
* MySQL: results can be streamed a row at a time instead of read whole, and the insert id, affected rows, server version and server status are available without extra queries
* MySQL and SQLite: values are read exactly. Integers are an Int or an Int64, DECIMAL is its exact text, DATE, DATETIME and TIMESTAMP are UTC with their fractional seconds, NULL is null, and SQLite INTEGER keeps all 64 bits. Dates before 1970 no longer end the process on Windows
* MySQL: queries and escaped values are sent by their UTF-8 byte length (non-ASCII text cut the end off a statement), escaping follows the server's NO_BACKSLASH_ESCAPES status, a write's result has no rows instead of throwing, floats are parsed in the C locale, and counts or lengths no server sends are refused instead of crashing the process
* SQLite: a failed step finalizes its statement at once and reports SQLite's own message, so the next statement and close() no longer fail as well. FTS5 can be enabled with -D HXCPP_SQLITE_FTS5
* MySQL and SQLite: the functions behind the additions above (streaming, insert id, affected rows, server status, TLS and keepalive state, SQLite's autocommit state) are declared in hx/StdLibs.h, so a driver binds them with @:native without declaring them itself
* Removed the HXCPP_SQLITE_JSON1 define, which did nothing since SQLite builds the JSON functions in, and documented HXCPP_SQLITE_FTS5
* Applied PCRE2's security backports for 10.42, including a heap overflow in pcre2_match with (*LIMIT_HEAP=n)
* Updated SQLite to 3.54.0 (from 3.51.2), which fixes a bug that could corrupt a WAL database when it was reset. SQLite on Windows now needs Vista or later, so an app built with HXCPP_WINXP_COMPAT that uses sys.db.Sqlite no longer runs on XP, and WinRT builds compile SQLite for UWP
* Fixed SQLite 64-bit INTEGER columns (timestamps, large ids, SUM aggregates) being silently truncated to 32 bits - values that fit stay Int, larger ones widen to Float; last_insert_id saturates instead of wrapping
* Fixed haxe.zip streaming buffers held as raw pointers across the GC free zone while zlib runs (moving-GC corruption window), an uninitialized flush mode for unknown mode strings, Compress.run copying its whole result an extra time to shrink it (a result under half of its worst-case buffer is still copied, to an exact fit, so it does not keep that buffer alive), and using a closed Uncompress reporting "Compress closed"
* Fixed haxe.zip.Uncompress/Compress execute() returning cumulative stream totals instead of per-call counts, which broke haxe.zip.Uncompress.run for any output larger than the buffer size (64K default) and corrupted all streaming loops after the first call
* Fixed zlib state leaks: Compress.run/Uncompress.run now release zlib's internal state on all paths including errors, and closing a Compress/Uncompress no longer leaks the stream structure
* Fixed EReg.matchSub integer overflow with huge lengths, regex runtime errors (e.g. match limit) being silently treated as no-match, and undefined behaviour passing unvalidated UTF-16 subjects to PCRE2 with the no-check flag
* Fixed a long-lived EReg keeping its last UTF-16 subject (and any cleaned-up copy of it) alive after matching an ASCII subject
* haxe.zip.Compress and Uncompress streams count their native zlib state (about 256 KB per compressor) towards collection pressure, so abandoned streams are collected and their memory freed sooner; added hx::GCReleaseManagedMemory, which takes such memory back off without a safe point
* Updated zlib to 1.3.2 (inflate state zeroed on allocation, NULL input checks, macOS build fix)
* SQLite and MySQL rows are built with the fixed-field layout generated code uses for anonymous objects, laid out once per result, instead of a field hash table per row: reading 100k 8-column SQLite rows takes 21% less time and the rows hold 30% less memory. Rows stay mutable, and a MySQL name used twice still keeps its later column (libhxcpp #200)

### cppia

* Fixed the cppia jit's write barrier, under HXCPP_GC_GENERATIONAL, taking new objects for old ones on the main thread until the first collection, because the thread's mark ID was never set until then. It flagged nursery objects as remembered, which hid the nursery objects allocated after them from the stack scan, so a collection could free what only the stack held: a script method that called Gc.compact() through Reflect.callMethod lost the closure it was running in and crashed (HaxeFoundation/hxcpp#1196). Every thread's mark ID is now set when its context is created
* The cppia JIT says when it does not run, instead of quietly running scripts in the interpreter: loading prints that once, and __scriptable_cppia_jit_active() tells a host whether modules it loads now are compiled. On Apple Silicon it could never run, since its memory was mapped read-write-execute, which macOS on arm64 refuses; it now maps with MAP_JIT and makes the memory writable only while code is generated or freed (a hardened-runtime app also needs the com.apple.security.cs.allow-jit entitlement). (libhxcpp #266)
* The cppia JIT runs on arm64, which includes Apple Silicon for the first time. sljit's arm64 code generator gave a load or store the wrong base register whenever its offset was too large for the instruction and it reused the last address it had built; cppia's frames, packed at 4-byte offsets that 8-byte accesses cannot encode, took that path all the time, so jitted code crashed at once. Haxe's unit tests and the cppia suite pass under the JIT on macOS arm64 (libhxcpp #271)
* A cppia module that uses classes or members the host does not have now fails to load with one message naming all of them, with where the script first uses each when it was compiled with -debug, such as "Could not link new Foo, static function Foo.make, Player.jump (Level.hx:42)". It threw "Bad link" for the first one and printed the details on stdout. A typed call or field read on a host object that its class does not have is in the list too: it linked as a dynamic lookup that failed or gave null only when it ran. docs/CppiaLinking.md describes what is checked (libhxcpp #262)
* Added a script stack overflow guard to cppia: deep recursion now throws a catchable "Stack Overflow" instead of silently corrupting the heap past the fixed script stack (checked in the interpreter entries and in jitted function prologues)
* Fixed cppia array access through host-class getters returning garbage (the linked getter was validated but never invoked - the argument frame was read back as the result)
* Fixed cppia float constants parsing locale-dependently (comma-decimal hosts)
* Fixed truncated/corrupt .cppia files driving the loader past the end of the buffer (the byte reader now reports EOF like the other stream primitives)
* Fixed the cppia JIT writing before the array buffer for stores with a negative index (heap corruption; the write is now dropped, matching compiled code)
* Fixed jitted calls that omit two or more optional arguments corrupting the callee frame (each omitted argument advanced the frame twice)
* Fixed swapped register-class tags in jitted int/float conversions, which miscounted scratch registers
* Fixed the jitted stack-overflow guard raising a C++ exception, which cannot unwind through jitted frames (crashed instead of throwing catchably) - it now follows the runtime's stored-exception convention
* Fixed the cppia JIT compiling an abstract function's placeholder body, which Haxe writes as a bare return, as if it were a function: it read past the end of the expression, which crashed loading Haxe's own unit suite on ARM64
* Fixed the cppia JIT calling into the runtime with the stack off the 16-byte alignment the ABI requires, on 32-bit x86 outside Windows: frames were aligned on macOS only, and a call with three arguments pushed the third. Native code relying on the alignment could crash; Haxe's own unit suite did, in sys.ssl.Digest.sign
* Fixed cppia storing a Bool member field as an Int, so it read back into Dynamic as an Int (hxcpp #1367)
* Fixed cppia boxing the Bool results of member, interface, closure and property calls as Int: Std.isOfType(x, Bool) was false for them and they printed and serialized as 1
* Fixed cpp.cppia.Module and cpp.cppia.Host.enableJit failing to compile outside the cppia host class: scriptable builds include hx/Scriptable.h from hxcpp.h (hxcpp #816)
* Fixed cppia scripts using Array<cpp.Float64> failing to load with Unknown array type:double (hxcpp #633)
* Fixed a cppia script failing to load with Unknown global when it reads a static property of an extern host class: a call by C++ name to a host class static is linked through the class (hxcpp #1183)
* Fixed a cppia script that constructs a class the host does not have crashing the process: it fails to load with Could not link new (hxcpp #1410)
* Fixed cppia with HXCPP_GC_GENERATIONAL freeing young objects that an old script still used: a field set by name had no write barrier, and literals, JIT-embedded boxes and statics held by a loaded module were not traced by a generational collection (hxcpp #1408, #1406)
* Fixed the cppia JIT allocating unaligned objects in the nursery, which a conservative scan could misread (hxcpp #1411)
* Fixed the cppia JIT failing to load a module that converts an Int subtraction to a String, with Bad move target (hxcpp #1369)
* Fixed the cppia JIT storing a Bool constant in an Array<Any> as a number (hxcpp #1371)
* Fixed a scriptable host class failing to compile when a parent Haxe class has a toString with an optional argument (hxcpp #1403)
* Fixed the cppia JIT corrupting memory or crashing on Array<cpp.Float32>, and failing to load a Float32 array literal with Type mismatch
* Fixed the cppia JIT with the moving collector using stale addresses for boxed string literals, PosInfos and script enum constants, which it had written into its code
* Fixed cppia array access on a null array crashing with HXCPP_CHECK_POINTER instead of throwing Null Object Reference, interpreted and in the JIT (hxcpp #1013)
* Fixed cppia indexing of a host ArrayAccess class: compound assignment and increments failed to load, and under the JIT any index read or write crashed
* Fixed running a .cppia file on a BSD host stopping with Cppia is not supported on this host: it uses the host in bin/BSD64 or bin/BSDArm64 (hxcpp #1089)
* Fixed a jitted cppia exception thrown back to native code returning null, and every later jitted call returning null with it
* Fixed CallStack.exceptionStack() for cppia: script throws are recorded, and a catch under the JIT stops adding frames to the stack
* Fixed two cppia JIT miscompiles: field++ in a method that used this nowhere else wrote through an unloaded register, and Float /= divided by a stale value
* Fixed calling a cppia function with too many arguments killing the process; it throws a catchable error
* Fixed malformed .cppia files crashing the host; bad counts, lengths, type ids and string ids throw a load error
* Fixed cppia functions with an untyped return reading an uninitialised return type
* Fixed the cppia interpreter giving charCodeAt's character instead of its code in a string or Float context: "c" + s.charCodeAt(1) was "ce" and var f:Float = s.charCodeAt(1) was 0
* Fixed cppia giving some Bool results as Int: the interpreter's typed-array contains and remove, and the JIT's Array<Bool>.pop(), so trace([1].contains(1)) printed 1
* Fixed cppia's Std.isOfType, cast and catch ignoring interfaces a class has through another interface (interface IBar extends IFoo; class A implements IBar was not an IFoo)
* Fixed cppia's safe casts to Int, Float, Bool and String never throwing: cast("abc", Int) gave 0, cast(1.5, Int) gave 1 and cast(7, String) gave "7". They throw as compiled code does, and null gives 0, 0 and false, or null for String, as on the static targets
* Fixed the cppia JIT's String += dropping a null right-hand side, so s += null left s unchanged and a null s += "x" gave "x"; it appends "null" as compiled code and the interpreter do
* Fixed cppia Int arithmetic not wrapping when the result is used as a Float or String: the JIT's "s" + (a + 1) with a = 2147483647 gave s2147483648. Both engines now wrap as compiled code does
* cppia modules can be released (`untyped module.release()`, or `hx::CppiaLoadedModule_obj::release()` from native code): the module's classes leave the class registry, its statics are cleared so the data they held can be collected, and its resources are unregistered; objects from it keep working. See docs/CppiaModuleRelease.md. Also fixed a class replaced by a later module losing its metadata and field lists to the collector, and a later module's resources reallocating the list the previous module had registered
* A released cppia module's code is freed once nothing reaches it any more: its expressions, class tables, vtables and JIT code used to stay allocated for good, about 2 MB per reload of a 58 KB script. Its instances, closures, bound methods, enum values and Class values, and code of it still running on any thread, keep it until they are gone; class objects of script classes are now collected like other objects, and exception stacks keep copies of the positions they show. Strings from a script's resources are copies, so they outlive it. Profiler and telemetry builds keep released modules, as they remember function names by address. Also fixed module.run() leaving the script's main frame on the script stack, which grew by that frame on every call (follow-up to libhxcpp #238)
* Fixed cppia reloads that load the replacement before releasing the old module crashing in a collection or keeping old modules loaded: the class registry kept a freed module's class name as the key for that name, and a lookup that then missed the key left the released class listed for the collector. Script class names are now permanent strings, release always takes the class out of the collector's list, and the registry marks the script classes it lists (libhxcpp #243, #244)
* Fixed the debugger showing wrong values for a cppia function's this, arguments and locals: it read them from the wrong frame base, and in jitted code the frame's variable lists and unassigned locals held stale stack data, which could crash it (based on HaxeFoundation/hxcpp#1426 by @robknopf)
* Fixed script functions' stack frames and script object fields sitting at 4-byte offsets on 64-bit targets, which the debugger and the collector then read misaligned: stack frames now live in aligned native storage, and fields are aligned to their type
* Fixed cppia reading and writing Strings, objects and Floats in place in script frame slots, which sit at 4-byte offsets on 64-bit: that is undefined behaviour in C++, which UBSan reported, and can fault on a CPU that requires alignment. The frame layout stays as it is, since the code Haxe generates for scriptable host classes relies on it; every access to a slot, in the interpreter, the accessors that host code uses and the jit's native helpers, now copies the value in and out, which compiles to the same single load or store on x86 and ARM64. The sanitizer job no longer suppresses alignment reports (libhxcpp #157)
* Fixed the cppia jit boxing Array.contains and Array.remove results, and Array<Bool> elements read or written, as Int instead of Bool where they become Dynamic, as in an Array<Dynamic> literal
* Fixed the cppia jit storing a Bool set on a dynamic field, such as an optional field of an anonymous structure or a field of a Dynamic, as the Int 1 or 0, so it read back as a number and compared unequal to true (libhxcpp #259)
* The debugger's script-loaded callback, which Haxe 5's cpp.vm.Debugger sets through __hxcpp_dbg_setOnScriptLoadedFunction, is there at every hxcpp API level, not only 500
* Fixed breakpoints and stepping never stopping in jitted cppia code: the jit kept each line number for stack traces but never called the debugger's line hook, so a script could only be debugged with the jit off (HaxeFoundation/hxcpp#498, libhxcpp #267). A breakpoint on a script function by name stops now too: script functions had no first line for it to match. docs/CppiaDebugging.md describes what debugging a script needs
* A debugger that attaches before a cppia script loads, as the VS Code one does, can set breakpoints in the script from the script-loaded callback, which runs after each script loads and before it boots. HXCPP_DBG_ON_SCRIPT_LOADED tells C++ that may build against other hxcpp versions that the callback is there, and setting it again no longer adds the same root twice (libhxcpp #283)
* Fixed cpp.vm.Debugger.getFilesFullPath() not lining up with getFiles() once a cppia script was loaded: a script file with the name of a host file was left out of one list and not the other, so a debugger that pairs them by index, as the VS Code one does, gave every script file after it the wrong path. Each entry is now the full path of the same entry of getFiles()

### CFFI, ndlls, the debugger and telemetry

* The plugin loader (module registry, primitive cache, kind registry, search paths) is now thread-safe; failed lookups no longer permanently grow the registries; __hxcpp_unload_all_libraries clears the caches so later loads cannot use freed module handles or call into unmapped libraries
* "Could not load module" errors now include the OS loader detail (dlerror/Windows error code) - a missing dependent library or wrong bitness was indistinguishable from file-not-found
* CFFI fixes: buffer_to_string returned an unterminated string aliasing the live buffer (later appends mutated the result, C consumers overread); val_fun_nargs reported arbitrary objects as varargs functions instead of faNotFunction; val_to_buffer/val_array_push silently failed for dynamically typed arrays; an abstract allocated with a size but no finalizer leaked its payload on collection; loaded primitives report their real arity
* Haxelib resolution fixes: non-ASCII Windows home directories no longer break ndll lookup (wide environment reads, copied instead of aliasing CRT storage), trailing whitespace in .current/.dev files is trimmed, and pushing an empty dll path no longer inserts the filesystem root into the search list
* Debugger fixes: an inverted condition silently disabled all breakpoints after toggling execution trace (and ran the per-line handler forever with none set); thread-terminated events always reported thread -1; querying a thread that exited during a break-all crashed the debugged process (NULL map entry); reading stack variables could deadlock the whole process (GC allocation under the debugger lock); break-all no longer stalls every thread for the 2s timeout; replacing breakpoints freed the old set while unlocked readers could still dereference it; stepping state is atomic (lost step counts with multiple threads); a detach racing a stop no longer calls a null handler; execution trace logged every line twice
* Fixed a latent use-after-free in the ndll primitive cache: cache keys were made permanent only after insertion (which changes a local copy, not the stored key), so any cpp.Lib.load after a GC cycle compared against freed string data
* Fixed == on loaded ndll primitives returning true exactly when they were different functions (inverted comparison contract)
* Fixed cpp.Lib.load looking for ndlls in Windows64 on Windows ARM64 instead of WindowsArm64 (hxcpp #1400)
* Fixed HXCPP_TELEMETRY crashing when objects recorded by a Telemetry deleted on a restart or a thread exit were collected, and the HXCPP_TELEMETRY_DEBUG build failing to compile
* Fixed Tracy builds (-D HXCPP_TRACY) throwing "Not implemented" from the hxtelemetry API: starting telemetry is a no-op and dumping returns no frame
* Fixed the CFFI val_gc_ptr always throwing "Finalizer not supported here": it attaches the finalizer to the allocation
* Fixed DEFINE_PRIM_MULT exporting nothing with HXCPP_JS_PRIME (Emscripten): primitives with more than five arguments are exported and take their arguments as a JavaScript array
* Fixed the debugger's breakpoint updates having no memory barriers, and made building with MSVC older than Visual Studio 2015 stop with a clear message
* Fixed the debugger reading a stopped thread's stack while that thread was still running the stop handler: the handler is Haxe code that pushes and pops frames on that thread, so a query from the debugger thread could read its frame list while it changed, and count the handler's own frames as the top of the stack. Queries now wait until the handler returns
* Fixed the flag that tells debugged threads to check for breakpoints being read on every line without synchronisation while the debugger thread set it; it is atomic now, with loads that cost what the old ones did
* Fixed telemetry unlocking its allocation mutex before locking it when it skips an allocation made inside a CFFI call; on Windows that left the mutex held and the next thread that tracked an allocation blocked forever
* Fixed the CFFI val_ocallN taking three parameters against its four-parameter declaration, so every call passed the argument array's address as a single argument; it now passes the arguments it is given
* Fixed Marshal.readLittleEndianPointer and readBigEndianPointer (Haxe 5) failing to compile when used: they read a pointer to a pointer; they read the pointer and swap its bytes when the order differs
* Fixed a Haxe callback run by native cleanup while an exception unwinds (a C++ destructor) dropping the outer frames from CallStack.exceptionStack; the exception's stack is set aside during the callback and recording resumes when it returns
* cpp.marshal boxes of trivially destructible native values (scalars, pointers, plain structs) no longer register a GC finalizer that did no work
* hx::Throw, hx::Rethrow and the debugger's checked throw helpers are declared [[noreturn]], so native code that ends a function in a throw no longer needs a dummy return (MSVC error C4716)
* Updated the Tracy client to v0.14.1, with fixes for UTF-8 zone, plot and frame names, TRACY_ON_DEMAND, deep call stacks off Windows and Android, threads ending with undeclared small deallocations, and named frame marks (HaxeFoundation/hxcpp#1429 by @EliteMasterEric, with named frame marks by Cameron Taylor)
* Fixed 32-bit Linux builds with HXCPP_TRACY aborting at startup: Tracy 0.14 sign-extended pointers above 2 GB into its tagged message addresses, and as root it asserted on the 64-bit kernel's /proc/kcore

### Build tool and platforms

* Added FreeBSD, OpenBSD and NetBSD hosts: they build with the system clang through the Linux toolchain, into BSD, BSD64 or BSDArm64 (hxcpp #1089)
* Simplified the Apple SDK lookup in the build tool (hxcpp #1327)
* Added the arm64 macOS Cppia host to releases, in bin/MacArm64, and project/compile-cppia.hxml builds the host's own architecture unless one is given (hxcpp #1149)
* Changed the build tool to decide on colour from the terminal's environment instead of running tput, to honour NO_COLOR, and to take HXCPP_COLOR to force colour on (hxcpp #1146)
* Added compile progress to the build tool: each group says how many files it compiles, and each file how many of them have finished (hxcpp #1140, #1423)
* Added WinRT ARM64 builds: Setup's msvc-winrt-arm64-setup.bat exists, and they go to bin/WinRTArm64, where cpp.Lib.load looks. Desktop ARM64 objects move from obj/msvc*-armarm64 to obj/msvc*arm64 (hxcpp #1335)
* Added MSVC builds with clang-cl and the LLVM tools, with -D HXCPP_MSVC_LLVM, and cross-compiling them from Linux with -D HXCPP_MSVC_ROOT (hxcpp #1361)
* Added HXCPP_SANITIZE to build with gcc and clang sanitizers on Linux and macOS, with the GC's stack scanning excluded from AddressSanitizer
* Added separate Linux ARMv7 and ARM64 build architectures (hxcpp #1355)
* Fixed an arm64 macOS build that defines HXCPP_M64 but not HXCPP_ARM64, as an Xcode build can, compiling the x86-64 register capture: the capture is chosen by the compiler's target (hxcpp #1023, #1237)
* Fixed a relative path given with -options being read next to Build.xml instead of in the working directory; a missing options file now warns (hxcpp #1178)
* Fixed haxelib run hxcpp ignoring a compiled BuildTool on Apple Silicon, ARM64 Linux and Windows on ARM: it looked only in the x86-64 bin folder, so the slower neko tool ran (hxcpp #1029)
* Fixed the MSVC setup scripts failing to find Visual Studio installed under a path containing !: delayed expansion dropped the ! from the path vswhere reported (hxcpp #1034)
* Fixed a Raspberry Pi running a 64-bit system building with the 32-bit rpi hardfp profile into bin/RPi: it builds as Linux ARM64 (hxcpp #959)
* Fixed building on Linux with musl, such as Alpine: Sys.cpp included xlocale.h, which musl doesn't have (hxcpp #1165)
* Fixed MinGW GUI builds not showing traces in the console they were started from (hxcpp #1330)
* Fixed the build tool, built as C++, failing on a file group without a C or C++ standard (hxcpp #1405)
* Fixed arm64 iOS simulator builds sharing the x86_64 objdir and library suffix, and linking dylibs for i386: they use obj/iphonesim...-arm64 and the .iphonesim-arm64 suffix (hxcpp #1343)
* Fixed C++20 builds: VirtualArray's comparison with Dynamic recursed under the reversed operators, and Sys.putEnv mixed String and const char* in a conditional (hxcpp #1232)
* Fixed copyFile treating overwrite="false", or any unknown value, as always: true and false mean always and never, and an unknown value warns
* Fixed the compile cache reusing a precompiled header built for a different C++ standard
* Fixed AndroidCompat.cpp closing its extern "C" section in the wrong place (hxcpp #1404)
* Fixed Android builds failing with "no member named 'sscanf' in namespace 'std'": GcCommon.cpp now includes <cstdio>
* Fixed x86_64 Android builds producing arm64 code when they ask for HXCPP_X86_64 without HXCPP_M64, as Lime does
* Fixed toggling a define such as HXCPP_TELEMETRY, HXCPP_GC_BIG_BLOCKS or HXCPP_M64 not recompiling the runtime and hxcpp's libraries, which left objects built both ways and could fail to link
* Fixed Emscripten builds running --spill-pointers without --profiling-funcs when the detected emscripten version was below 6 or misread, which failed to find the stack pointer or lost GC roots; the two always go together
* Removed the Visual Studio 2013 and older toolchain setup, which could no longer build libhxcpp, and HXCPP_MSVC_VER values that name them now stop the build with an error (HaxeFoundation/hxcpp#1428 by @tobil4sk)
* Added a .clang-format for libhxcpp's C++ sources, used for changed lines rather than whole files; third-party code is excluded (HaxeFoundation/hxcpp#1430 by @tobil4sk)
* The build tool names the copy that runs it: -v prints its version, git commit and path, and it warns when Haxe generated the code for another hxcpp version than the one building it, as happens with several copies installed (libhxcpp #79)
* The build tool stops with a message when the code comes from a Haxe older than 4.2, which libhxcpp does not support, and warns when its hxcpp API level is newer than the tool knows
* Haxe 5.0.0-preview.1 builds and runs: it passes hxcpp API level 500 but generates code for the level 430 runtime, as hxcpp was when it was released, and the build tool now builds it against that runtime. It tells the release from the nightlies after it, which pass the same level, by the Haxe build named in the generated code. CI runs the regression suite with it


4.3.xx
------------------------------------------------------------

* Added support for killing processes on OS' other than Windows, Apple TV, and Apple Watch
* Added register capturing on Linux
* Added HXCPP_CPP17 define
* Added ASM and NASM support to build xml
* Added support for name and value entries to build xml
* Added tracy profiler
* Added support for Android NDKs higher than 21
* Added x86_64 support to older Android NDKs
* Added optional detaching of main thread

* Updated mbedtls to 2.28.2
* Updated sqlite to 3.40.1
* Updated zlib to 1.2.13

* Fixed SSL socket non blocking handshake throwing an exception on 64bit Windows
* Fixed Windows 64bit architecture detection
* Fixed critial error handler returning the wrong callstack
* Fixed ARM64 library names on Mac
* Fixed generational GC when used with HXCPP_ALIGN_ALLOC
* Fixed pthread structured being unaligned
* Fixed cppia crash on functions with empty bodies
* Fixed regression parsing integers which wrap around
* Fixed compilation with HXCPP_GC_DEBUG_LEVEL define
* Fixed compilation with MinGW
* Fixed enum parameters potentially returning null
* Fixed HXCPP_CLANG not being set when using clang
* Fixed pcre2 and mbedtls not being compiled with the c99 standard
* Fixed behaviour of indexOf and lastIndexOf on empty strings not aligning with other targets
* Fixed behaviour of directory reading function not aligning with other targets
* Fixed haxelib not being invoked with the current working directory

* Removed Haxe 3 support

4.3
------------------------------------------------------------

* Bug fixes
* Upgrade to 4.3 API
* Use PCRE v2

4.2
------------------------------------------------------------

* Update MIN_IOS_VERSION
* Bug fixes
* Upgrade to 4.2 API
* Fixed mysql_select_db failing for newer mariadb versions due to extra 0x00 byte after database
* Fixed mysql_close not sending COM_QUIT before closing socket

4.1.15
------------------------------------------------------------
* Added Arm64 suport on windows
* Fixed crash with zero-sized alloc and generational GC
* Fixed crash with generational GC when old objects come back to life
* Fixed compile error with @:fixed Anons and arrays (socket select)
* Fixed lastIndexOf
* Optimized some equality functions

4.1.1
------------------------------------------------------------
* Added functions for haxe 4.1 Api.
* Added HXCPP_DEBUG_LINK_AND_STRIP to preserve symbolic information when creating android release binaries.
* Added optional HXCPP_SIGNAL_THROW to convert memory errors to haxe exceptions without needing additional code
* Added string_hash_map_substr and __hxcpp_parse_substr_float/int to allow some substring processing without extra allocation

4.0.64
------------------------------------------------------------

* Upgrade buildserver to 4.01
* Better generational collection in high fragmentation case
* typeinfo include fix for MSVC
* Fix MySQL connections
* Fix bugs with HXCPP_GC_GENERATIONAL
* Add map.clear
* Better c++11 iOS support

4.0.19
------------------------------------------------------------

* Add Array.keyValueIterator
* General Utf16 string improvements
* Limit the amount of recursion in toString function
* Add float32 support to cppia
* Fix Gc race condition
* Throw exceptions according to the spec when casting
* Introduce hxcpp_smart_strings for unicode text

4.0.4
------------------------------------------------------------
* Compile Cppia against haxe 4.0 preview 4

4.0.2
------------------------------------------------------------
* Default Cppia to 64 bits on windows

4.0.1
------------------------------------------------------------
* More logic for determining the android NDK version
* Updated various opensource libraries (thanks robocoder)
* Updated version of zlib
* Updated version of sljit
* Updated version of pcre
* Updated version of sqlit3
* Updated version of mbedtls
* Some work on supporting utf16 strings (hx_smart_strings)
* Added process_kill
* Change root when calculating haxelib in build.xml files
* Fix cppia super calls across cpp boundary
* Add Array.resize
* Be consistent with mod in cppia
* Fix Sys.stderr
* Add 'embedName' file attribute to allow text to cpp conversion
* Updates for Msvc
* Updates for Xcode

3.4.188
------------------------------------------------------------
* Fix some threading crashes

3.4.185
------------------------------------------------------------
* Do not ship static libraries
* Use more lock-free structures in GC processing
* Added some documentation
* Added HXCPP_GC_SUMMARY option
* Added HXCPP_GC_GENERATIONAL option
* Added HXCPP_GC_DYNAMIC_SIZE option
* Some MSVC 2017 support
* Compile Cppia with JIT as an option by default

3.4.64
------------------------------------------------------------
* Fixed cppia native interface implementation
* Fixed debugger breakpoints
* More compatibility for inet_pton and inet_ntop
* Correct the order of thread housekeeping data

3.4.49
------------------------------------------------------------
* Fixed 2d-Arrays and unserialize

3.4.43
------------------------------------------------------------

* Added more options for code-size optimizations on android (thanks madrazo)
* Added version of stpcpy on android to allow building with platform > 21, and running on older devices
* Added some initial support for ipv6
* Experimental support for Cppia JIT
* Fixed issue with stale objects that use new pch files in cache
* Rethrowing exception now preserves stack correctly


3.4.2
------------------------------------------------------------

* Align float reads from memory for Arm architecture
* Removed some virtual functions not needed by newer versions of haxe
* Reworked the logic for compacting fragmented heaps with HXCPP_GC_MOVING
* Expose StackContext to allow inlining of allocation routine, and combine with Cppia context
* Fix some compare-with-dynamic issues
* Added WatchOs support
* Fixed for android NDK 13
* Fix Array closure equality
* Refactor the Cppia code
* Fix return codes for atomic decrease
* Fix some GC zone issues in the standard library
* Set minimum MacOS deployment target to 10.6
* Do not use typedefs for 'Int' and 'Bool' for newer api levels
* Added dll_link to create output dll
* Improved ObjC support
* Make Cppia order of operations of '+=' et al consistent with other targets
* Added NO_RECURSE flag to PCRE
* Fix bsd_signal undefines on android
* Add create/free abstract

3.3.49
------------------------------------------------------------
* Fix Dynamic != for haxe 3.2.1
* Fix Command line parsing on windows for triple quotes

3.3.45
------------------------------------------------------------
* Much better compile cache support
* Added tags to compiler flags to allow better targeting
* Added UCP support to regexp
* Added Array::fromData
* Added AtomicInt operations
* Added _hx_imod
* More improvements for tvos
* Fix blocking deque issue
* Improved native testing
* Added 'hxcpp run hxcpp cache ...' commands for managing cache
* Added cpp.Variant class for query of field values to avoid boxing
* Added more efficient version of finalizer
* Add non allocating version of __hxcpp_print
* More WinRT fixes
* Output 'HxcppConfig.h' with defines included for easier external integration
* Output list of output files if requested
* Add support functions for StdLib - alloc/free/sizeof
* Fix crash when marking stack names from GCRoots
* Add bitcode support for iOS
* Rename RegisterClass to avoid conflicts with windows
* Added 'VirtualArray' for arrays of unknown types
* Split Macros.tpl
* Added optional ShowParam to process_run
* Added inline functions for Int64 externs
* Add error check for allocating from a finalizer
* Fix null strings on Cffi Prime
* Use slow path if required for Win64 Tls
* Expand logic for detecting android toolchain from NDK name
* Remove the need for hxcpp binaries by compiling source directly into target
* Adjust the default verbosity level, and add HXCPP_VERBOSE/HXCPP_QUIET/HXCPP_SILENT
* Added some control options for copyFile directive
* Fix cppia decrement
* Add Array.removeRange, which does not require a return value
* Do not call setbuf(0) on stdin, since it messes with readLine
* Cppia now throws an error if loading fails
* Allocate EnumParam data inline to cut down on allocations
* Allow anonymous object data to be allocated inline to avoid allocations
* Add SSL library code
* Add NativeGen framework for interfaces
* Add macros to allow neater generated code
* Allow larger memory space with -D HXCPP_GC_BIG_BLOCKS
* Improve Array.join speed

3.2.205
------------------------------------------------------------
* Initial support for HXCPP_OPTIMIZE_FOR_SIZE
* Support HXCPP_DEBUG_LINK on more targets
* Support for cross compiling to windows from linux
* Added array removeAt
* Some telemety fixes (thanks Jeff)
* Check contents when comparing Dynamics with same pointer (Math.Nan!=Math.Nan)
* Numerous WinRT fixes (thanks madrazo)
* Fixed bug causing GC to crash marking constant strings (eg, resources)
* Updated default SDK for Tizen (thanks Joshua)
* Fixed command line args on linux (thanks Andy)

3.2.193
------------------------------------------------------------
* Some improvements for tvos
* Start on some GC defragging code
* Fix android thread access to GC structures
* Add socket socket_recv_from and socket_send_to
* Fixed memory leak in GC collection code
* Allow cross-compile to windows via MINGW
* Fix overflow error that meant GC would work with a too-small buffer in some cases

3.2.180
------------------------------------------------------------
* Initial support for tvos
* Change name of ObjectType to hxObjectType to avoid clashes with iOS
* Try to keep windows.h out of haxe-generated code
* Fix null access bug in array-of-array
* Create separate library for msvc 19

------------------------------------------------------------
* Try to get the pdb server working better for MSVS 2015
* So not export symbols on windows unless HXCPP_DLL_EXPORT is set (-D dll_export) - makes exe smaller
* Avoid dynamic-cast if possible when converting 2D arrays
* Some RPi fixes
* Some CFFI Prime fixes (thanks Joshua)
* Fix build tool for next version of neko
* Improve msvc cl.exe version checking for non-English environments
* Add more control over how much Gc memory is used
* Add faster(inline) thread local storage for Gc on windows.
* Add some Gc load balancing when marking large arrays with multiple threads
* Change the Gc memory layout to be a bit larger, but simpler.  This allows most of the allocation to be simplified and inlined.
* Explicitly scan registers for Gc references because the stack scanning was missing them sometimes
* Some additions to Undefine.h for windows
* When static linking using MSVC 2015, compile the libraries directly into the exe to avoid compatibility issues
* Move standard libraries into their own build.xml files
* Make it easier to change the generated output filename
* Allow targets from one build.xml file to be merged into another
* Some more work on HXCPP_COMPILE_CACHE
* Allow automatic grouping of obj files into librarys to avoid linking all the symbols in all obj files
* Add implicit conversion to referenced type from cpp.Reference
* Allow build.xml files to be imported relative to importing file
* Allow '-' in command-line defines
* Fix warnings from Hash class
* Fix setsockopt for Mac
* Support to MSVC2015
* Fix for Blackberry 10.3
* Fix debug break by linenumber
* Better objc integration (thanks Caue)
* Increase number of variables captured in closures to 20
* Initial support for telemetry (thanks Jeff)
* Align allocations for better emscripten support

------------------------------------------------------------
* Fix gc_lock error in remove_dir
* Some cppia bug fixes - enum and resources overrides
* More android atof fixes
* Improved haxelib seek logic

Haxe 3.2.0
------------------------------------------------------------

* Improve testing
* Allow dll_load path to be set programatically and simplified the dll search sequence.
* Improved cffi_prime, and added String class
* Fixed static linking of mysql5
* Moved static link code in general to cpp.link package, not hxcpp package
* URL decode now does not need to performe reallocs
* Ensure HXCPP_API_LEVEL is always defined
* Added __hxcpp_unload_all_libraries to cleanly unload dlls
* Added some utc date functions
* Better support for non-console apps in windows XP 64
* Increased use of HXCPP_DEBUG_LINK for gcc based targets
* Class 'hasField' is now more consistent with other functions/targets
* 'haxelib run hxcpp test.cppia' will run Cppia on the specified file
* Add fast-select option for sockets
* Allow code to run without HXCPP_VISIT_ALLOCS defined
* Fix debugger thread deadlocks
* Allow up to 27 dynamic arguments
* Fixes for Emscripten - byte align access and disable threads
* Allow emscripten to generate 'executables' (.js/.html) and add options for specifying memory
* Allow spaces in exe names again
* Make cpp::Struct compare via memcmp, and mark correctly
* Fix catch block in cppia
* Treat '-debug' as an alias for "-Ddebug"
* Expose ArrayBase for use with some generic or external code
* Clarify the role of 'buffer' in cffi

------------------------------------------------------------
* Only put a minimal run.n in source-control, and use this to boot hxcpp.n
* Added cpp.Struct and cpp.Reference classes, which are handy for extern classes
* Moved Class to hx namespace
* Simplified 'main' logic
* Allow new android compilers to work for old devices (thanks google)
* Correctly read hxcpp_api_level from Build.xml
* Verbose logging prints which file is being compiled
* Handle undefining the INT_ constants differently to allow std::string to still compile
* Remove entries form Options.txt that do not influence the cpp build
* Add optional destination= command-line option to allow copying the result to named file
* Static libraries will be prefixed with 'lib' now
* val_is_buffer always returns false on neko
* Add val_iter_field_vals, which is like val_iter_fields but consistent with neko
* Remove NekoApi binaries
* Add Cppia binaries
* Add Windows64 binaries
* Make compares between Dynamic and numeric types false, unless the Dynamic is actaully numeric

------------------------------------------------------------
* Even more optimizations for hashes
* Some more optimizations for small hashes
* Fix for google changing inlining in platform21 headers (atof, rand, srand)
* Re-tuned Hash for small objects too (improves Anon object perforamce)
* Reverted change that automatically threw 'BadCast'.  Now required HXCPP_STRICT_CASTS

------------------------------------------------------------
* Cached dynamic versions of small ints and 1-char-strings for speed
* Added support for weak hashes - needs latest haxe version
* Use internal hash structure for maps - now faster.  New version of haxe makes it faster still.
* Changed the way development versions are bootstrapped to avoid committing binaries
* Improved mingw support
* Dont append -debug to dll name
* Reorder xml includes to allow early parts to correctly influence older parts
* Fix busy wait in semaphore lock
* Fixed GC issue when constructing exrernal primitive objects
* Added armv7s and arm64 targets for ios
* Some fixes for neko cffi - wstring and warning for neko_init
* Fix file read (and copy) from thread

------------------------------------------------------------
* Compile fix for blackberry
* Pass on haxe_api_level
* Add -nocolor flag

------------------------------------------------------------
* Add support for prelinker
* Cygwin toolchain fix
* Add HXCPP_NO_COLOUR  and HXCPP_NO_M32
* Fix windows trace output
* Add initial support for GCWO compile
* Fix bug with losing GC references in Array.sort
* Fix bug with zombie marking
* Add support for optimised sort routines
* Add support for haxe.ds.Vector optimisation
* Add support for cpp.Pointer, cpp.NativeArray, cpp.NativeString

------------------------------------------------------------
* Add BlackBerry and Tizen binaries
* Fix issues when using names like ANDROID or IPHONE in an enum
* Added more info in verbose mode (setenv HXCPP_VERBOSE)
* Refactor build files to allow greater customisation
* Fix bug with 'lock' where some threads may not get released
* Add optimised arrays access
* Add optimised memory operations for arrays and haxe.io.Bytes
* Avoid blocking in gethostbyname
* Upgrade run tool output and layout
* Restore sys_time for windows

3.1.1
------------------------------------------------------------
* Fixed MSVC support for 64-bit targets (vc11, vc12)
* Initial work on cpp.Pointer (not fully functional)
* Fixed callstack when throwing from native function

3.1.0
------------------------------------------------------------

* VC 2013 support - used as default now
* Add winxp compatibility flags
* Allow cross-compiling from mac to linux
* Added NSString helper conversion
* Better auto-detection for android toolchain
* Allow foreign threads to easily attach and detach from GC system
* Weak references to closures keep object alive
* Added HXCPP_API_LEVEL define to allow for future compatibility
* Fixed clearing finalizers twice
* Int multiply and minus are performed with integers now
* Fix comparing +- infinities
* Use multiple threads in the mark phase of GC
* IOS now defaults cpp11 binary linkage
* Added HXCPP_VERBOSE environment var to enable extra output
* Fixed spin loop in pthread_cond_wait
* Added ability to link several .a files into a single .a file
* Removed dependence on STL runtime for supplied modules
* Renamed some directories to be more standard
* Moved some extra build files into obj directory
* Use sys.io.Process instead of Sys.command to avoid threading slowdown writing to console
* Add hxcpp.Builder to help with building multiple binaries
* Add android x86 support
* Drop pre-compiled support for everything excepth windows,mac,linux,ios and android
* Allow libraries and files to accumulated in the build.xml
* Supply pre-build lib files for static linking on supported platforms
* Support for static linking of all modules
* Support for hxcpp-debugger project
* Binaries have been removed from repo, and are built using a server
* Use build.n script to build all appropriate binaries
* Some initial support for mysql and sqlite databases
* Add free_abstract for safe releasing of data references
* Change process lauching to get better thread usage on mac
* Fix GC error in string resources
* Give obj files in libraries unique names

3.0.2
------------------------------------------------------------
* Fix Dynamic + Int logic
* Reverted linux compiler to older version
* Cast Array at call site if required
* Tweak Array.map return value

3.0.1
------------------------------------------------------------
* Added nekoapi for linux64
* Upgrade nekoapi to v2
* Added haxe vector support
* Added socket_set_fast_send
* Fixed android build
* Expanded native memory access methods
* Fix exception dump
* Added initial Emscriptm support
* Allow specification of ANDROID_HOST
* Inital work on auto-setup of win64
* Support call-site casting of Arrays


3.0.0
------------------------------------------------------------
* Support haxe3 syntax
* Added socket poll function
* Added some initial support for dll_import/dll_export
* Allow full path name when loading dynamic libraries
* Allow dynamic toString function
* Added initial support for Raspberry Pi
* Array sort now uses std::stable_sort
* Fixed Dynamic+null string output
* Fix splice size calculation
* Add object ids for use in maps
* Add map/filter functions to arrays
* GC will now collect more often when big arrays are used
* You can specify a number of args > 5 for cffi functions if you want
* Fix internal hash size variable
* Class static field list does not report super members now
* Fix casting of null to any object
* Do not read input twice in sys_getch
* Link in PCH generated obj data on msvs 2012
* Date is now consistent with UTC
* Hash 'remove' now returns correct value
* CPP native WeakRef now works, and has a 'set' function
* Fixed compile error when assigning to a base class
* Fixed compile error when using != and Dynamic
* Math/floor/ceil/trunc/min/max now pass unit tests
* More control over android sdk installation
* Regexp_match fix
* Fix val_callN CFFI

2.10.3
------------------------------------------------------------
* Added initial build support for WinRT
* Android toolchain improvements
* Minor compile fixes
* Other minor improvements

2.10.2
------------------------------------------------------------
* Fixes for BlackBerry 10 compatibility
* Fixes for iOS 6 compatibility
* CFFI improvements
* Minor Linux improvements
* Minor OS X improvements

2.10.1
------------------------------------------------------------
* Fix trace() output
* Clang options for OS X compiler
* Small fixes

2.10.0
------------------------------------------------------------
* GC upgrades - moving/defragging/releasing
* Built-in profiler
* Build-in debugger
* Fix mac ndll finding bug
* Add Int32 member functions
* Clang options for ios compiler
* Add a few pre-boxed constants
* Some general bug fixes

2.09.3
------------------------------------------------------------
* Fix Xml enum usage

2.09.2
------------------------------------------------------------
* Resolve library paths when launching Mac apps from Finder
* Compile fix for the BlackBerry toolchain
* Fix interface comparison
* Fix api_val_array_value for NekoApi
* Add workaround for optional Strings in interfaces 
* Tweak the timing og the GC run
* Remove setProperty conditional compiles
* String charCodeAt only returns positive values
* Fix modulo for negative numbers
* Remove extra space from array output
* Treat '.' and '_' as literals in urlEncode
* Dynamically generated, 0 param, enum instances match the static version


2.09
------------------------------------------------------------
* Improved precision in random implementations
* Added some experimental support for float32
* Added some experimental support for generic getProcAddress
* String::fromCharCode generates single-byte strings
* Fix method compares
* Plug memory leak in finalizers
* Fix debug link flags
* Separate get/SetField from get/setProperty
* Added Null<T> for optional parameters

2.08.3
------------------------------------------------------------
* Actually add blackberry toolchain

2.08.2
------------------------------------------------------------
* Add blackberry support
* Add armv7 options
* Support new xcode layout
* Fix const qualifiers on interface functions
* Fix webOS obj directory

2.08.1
------------------------------------------------------------
* Fix Math.random returning 1.0 sometimes
* Std.is( 2.0, Int ) is now true
* Make static library building more separated - refactor defines to control this 
* Do not use @files for linking on mac
* toString on Anon objects will now get called
* Fix fast memory access with --no-inline
* Android tool host now set to linux-x86
* Allow use of __compare as operator== overload
* Add toNativeInt
* Add weak references
* Implement some neko/cffi compatibility operations
* Fix mac deployment using environment variable
* Fix reentrant mutexes
* Do not explicitly specify version of g++
* Speedup some code by avoiding dynamic_cast if possible
* Some fixes to allow Android multi-threading in normal operation

2.08
------------------------------------------------------------
* Do not create a new class definition for each member function
* Allow 5 fast and up to 20 slow dynamic function arguments
* Support utf8 class
* Added "Fast Memory" API similar to flash
* Added support for webOS
* Fix uncompress buffers
* Added file to undefined pesky processor macros
* Setup default config in user area
* Auto-detect msvc and iphone version
* Force compilation for mac 10.5
* Some support for cygwin compilers
* Remove Boehm GC as an option
* Integrate properly now with Android ndk-r6
* Make Int32 pass haxe unit tests (shift/modulo)
* Fix bug in "join"
* Fix bug with marking the "this" pointer in closures
* Fix bug with returning NAN from parseFloat
* Fix linux link flags
* Fix bug where string of length 0 would be null
* Made String cca return value consistent
* Added control over @file syntax
* Removed need for nekoapi.ndll
* Allow for neko.so to end in ".0"

2.07
------------------------------------------------------------
* Added initial support for Mac64, Linux64, MinGW and GPH and refactored build tool.
* Return the count of traced objects
* Fix interface operator ==
* Initial work on msvc10 batch file
* Add bounds check on String.cca
* Build static libraries, if requrested
* Added exe stripping
* Added val_field_name, val_iter_fields
* Fixed nekoapi string length
* Fixed Sys.args

2.06.1
------------------------------------------------------------
* Close files if required in GC
* Added fix for File.write
* Fixed String UTF8 Encode
* Nekoapi is now a "ndll", not a "dso".
* Fix array compile issue on linux
* Fix stack setting on firced collect

2.06.0
------------------------------------------------------------
* Updates to match haxe 2.06 compiler features
* Numerous bug fixes
* Add additional context to GC collection process
* Swapped from wchar_t* to utf8 char*
* Added templated iterators
* Use strftime for Dates
* Fix socket select and "_s" members
* Seed Math.random
* Fixed dynamic integer compare
* Added __hxcpp_obj_id
* Added some Android support

2.05.1
------------------------------------------------------------
* Updated windows nekoapi.dll binary
* Added -m32 compile flags to force 32 bit

2.05.0
------------------------------------------------------------

* Default to IMMIX based internal garbage collection.
* Reorginised files - split big ones, and moved common ones out of "runtime".
* Put internal classes in "hx" namespace, or HX_ prefix for macros.
* Remove multiple-inheritance, and use delegation instead.
* Write "Options.txt" from compiler so dependency can be determined.
* Require -D HXCPP_MULTI_THREADED for multi-threaded classes - to avoid overhead if not required.
* Build thread code into executable for better control.
* Fix return values of parseINt/parseFloat.
* Added comprehensive list of reserved member names.
* Put if/else statements in blocks.
* Added assert, NULL, LITTLE_ENDIAN, BIG_ENDIAN as keywords.
* Added control over how fast-cffi routines are created by requiring cpp.rtti.FastIntergerLookup to be "implemented".
* Construct anonymous object fields in deterministic (as declared) order.
* Fix code generation for some complex inline cases.
* Added cpp.zip.Compress
* Change "Reflect" class to be more standard
* Use array of dynamics for StringBuf.
* Fix setting of attributes in XML nodes.

Build-tool:
* Allow multiple build threads (via setenv HXCPP_COMPILE_THREADS N) for faster building on multi-code boxes.
* Added FileGroup dependencies
* Added pre-compiled headers (windows only, at the moment since gcc seems buggy)


1.0.7
-----------------
Changelog starts.
