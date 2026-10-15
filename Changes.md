
libhxcpp 6.0.0 (unreleased)
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

### Garbage collector

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

### Strings, numbers, Dynamic and reflection

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
* Fixed Std.string(Float) losing precision: now emits the shortest representation that round-trips (e.g. 0.1+0.2 prints "0.30000000000000004"), so String<->parseFloat is lossless and matches other targets
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

### Arrays and Bytes

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

### Sockets and TLS

* TLS connections now require TLS 1.2 or newer - the bundled mbedtls preset still negotiated the deprecated TLS 1.0/1.1 with downgrade-capable peers
* A UDP datagram larger than the receive buffer on Windows now returns the truncated data like posix instead of throwing a spurious "EOF" and discarding it (WSAEMSGSIZE)
* Added the missing EINTR retry to socket send and accept - any signal (profilers, child-process reaping) made them throw a bogus "EOF" and the connection got closed; an interrupted blocking connect now reports "Blocking" (the connect continues asynchronously per posix) instead of "EOF"
* Socket listen and setBlocking failures now throw instead of failing silently (a failed listen left the app believing it was accepting connections); shutdown throws except for the defensive not-connected case
* Accepted sockets now inherit close-on-exec (posix) and SO_NOSIGPIPE (macOS) like freshly created ones - accepted connections leaked into child processes, and a client reset could SIGPIPE-kill a macOS server
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

### Files, processes and the system

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

### Regular expressions, zlib and databases

* A zlib stream requesting a preset dictionary (FDICT) now throws instead of looping forever (denial of service on hostile input); zlib failures now include the error code and zlib's message text instead of a bare "ZLib Error"
* MySQL: logs in to MySQL 8 and later servers (caching_sha2_password, sha256_password and auth switch requests, with the password RSA-encrypted under the server's key when the connection is not encrypted) and connects over TLS, with sslMode from disabled to verifying the server's identity. MySQL 8's default collation, utf8mb4_0900_ai_ci, and the other missing charsets are known
* MySQL: connectTimeout, readTimeout and writeTimeout bound every wait, and the connect timeout is one deadline for the whole open. TCP keepalive can be enabled and read back. Errors carry the server's error number and SQLSTATE and never include the statement, and a lost connection fails with CR_SERVER_LOST. New ping, errno, sqlstate and thread id accessors
* MySQL: results can be streamed a row at a time instead of read whole, and the insert id, affected rows, server version and server status are available without extra queries
* MySQL and SQLite: values are read exactly. Integers are an Int or an Int64, DECIMAL is its exact text, DATE, DATETIME and TIMESTAMP are UTC with their fractional seconds, NULL is null, and SQLite INTEGER keeps all 64 bits. Dates before 1970 no longer end the process on Windows
* MySQL: queries and escaped values are sent by their UTF-8 byte length (non-ASCII text cut the end off a statement), escaping follows the server's NO_BACKSLASH_ESCAPES status, a write's result has no rows instead of throwing, floats are parsed in the C locale, and counts or lengths no server sends are refused instead of crashing the process
* SQLite: a failed step finalizes its statement at once and reports SQLite's own message, so the next statement and close() no longer fail as well. FTS5 and JSON1 can be enabled with -D sqlite_fts5 and -D sqlite_json1
* MySQL and SQLite: the functions behind the additions above (streaming, insert id, affected rows, server status, TLS and keepalive state, SQLite's autocommit state) are declared in hx/StdLibs.h, so a driver binds them with @:native without declaring them itself
* Removed the HXCPP_SQLITE_JSON1 define, which did nothing since SQLite builds the JSON functions in, and documented HXCPP_SQLITE_FTS5
* Applied PCRE2's security backports for 10.42, including a heap overflow in pcre2_match with (*LIMIT_HEAP=n)
* Fixed SQLite 64-bit INTEGER columns (timestamps, large ids, SUM aggregates) being silently truncated to 32 bits - values that fit stay Int, larger ones widen to Float; last_insert_id saturates instead of wrapping
* Fixed haxe.zip streaming buffers held as raw pointers across the GC free zone while zlib runs (moving-GC corruption window), an uninitialized flush mode for unknown mode strings, Compress.run copying its whole result an extra time to shrink it (a result under half of its worst-case buffer is still copied, to an exact fit, so it does not keep that buffer alive), and using a closed Uncompress reporting "Compress closed"
* Fixed haxe.zip.Uncompress/Compress execute() returning cumulative stream totals instead of per-call counts, which broke haxe.zip.Uncompress.run for any output larger than the buffer size (64K default) and corrupted all streaming loops after the first call
* Fixed zlib state leaks: Compress.run/Uncompress.run now release zlib's internal state on all paths including errors, and closing a Compress/Uncompress no longer leaks the stream structure
* Fixed EReg.matchSub integer overflow with huge lengths, regex runtime errors (e.g. match limit) being silently treated as no-match, and undefined behaviour passing unvalidated UTF-16 subjects to PCRE2 with the no-check flag

### cppia

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
* Fixed telemetry unlocking its allocation mutex before locking it when it skips an allocation made inside a CFFI call; on Windows that left the mutex held and the next thread that tracked an allocation blocked forever

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
