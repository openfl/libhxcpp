Defines
-------

There are a number of standard defines you can use to control the hxcpp build.  Some of these are used by the haxe compiler, and affect then generated code.  Others apply to the build tool and affect how the code is compiled.

Defines affecting how the code is generated.  These need to be in the command line when calling haxe.

| Define                  | Meaning            |
|-------------------------|--------------------|
| *HXCPP_DEBUGGER*        | Add extra macros required by debugger.  Usually added automatically be debugger haxelib |
| *HXCPP_GC_GENERATIONAL* | Enable experimental generational garbage collector |
| *HXCPP_GC_INCREMENTAL* | Enable experimental incremental marking on native Haxe 4 C++ builds. Use `-lib hxcpp` and regenerate all C++. The existing generator's barriers are enabled automatically; do not combine with the generational, nursery or moving collectors, cppia or Emscripten. See [Incremental GC](../IncrementalGc.md) for scheduling hooks and limits |
| *HXCPP_GC_INCREMENTAL_VERIFY* | Independently retrace roots and descendants before an incremental cycle reclaims memory. Adds substantial memory and pause overhead; use for correctness checks, not timing results |
| *HXCPP_GC_INCREMENTAL_TEST* | Enable deterministic phase and failure controls used by the incremental GC regression fixtures. Requires `HXCPP_GC_INCREMENTAL`; omit from application and performance builds |
| *annotate_source*       | Add additional annotations to source code - useful for developing hxcpp |
| *dll_export*            | Export hxcpp runtime symbols |
| *file_extension*        | Set the extension (without the dot) of generated files.  eg "-D file_extension=mm" for objc++ code  |
| *force_native_property* | Make dynamic access of fields call property getters/setters where appropriate |
| *include_prefix*        | Place all generated include files in a sub-directory, eg "-D include_prefix=hxinc".  Useful for avoiding name clashes |
| *no-compilation*        | Generate the code, but do not compile it |
| *no-debug*              | Do not generate debug macros in code |
| *nocppiaast*            | Use legacy cppia generation instead of new more recent changes |
| *objc*                  | Generate objective-c++ classes |
| *scriptable*            | Enable extra runtime information required for scripting |



Defines affecting how the code is compiled.  These can be on the command line when calling haxe, or added via the hxcpp build environment.

| Define                  | Meaning            |
|-------------------------|--------------------|
| *HXCPP_GC_MOVING*       | Allow garbage collector to move memory to reduce fragmentation |
| *HXCPP_GC_SUMMARY*      | Print small profiling summary at end of program |
| *HXCPP_GC_DYNAMIC_SIZE* | Monitor GC times and expand memory working space if required |
| *HXCPP_GC_BIG_BLOCKS*   | Allow working memory greater than 1 Gig |
| *HXCPP_GC_DEBUG_LEVEL*  | Number 1-4 indicating additional debugging in GC. Any level marks on one thread and recursively, which changes the timing and can hide a race; to watch a collection without that, use the defines below |
| *HXCPP_GC_SHOW_MEM_EVENTS* | Log the heap's size, use and decisions at each collection |
| *HXCPP_GC_SHOW_FRAGMENTATION* | Log fragmentation and the blocks a moving collection frees |
| *HXCPP_GC_PROFILE_COLLECT* | Log mark and sweep times and object counts for each collection |
| *HXCPP_GC_FIXED_BLOCKS* | Reserve the heap at a fixed address on 64-bit Windows, so objects land at the same addresses from run to run |
| *HXCPP_GC_WATCH*        | An address, such as 0x100000200, whose header and mark the collector logs at each collection, and when the conservative scan finds it or a move takes it; usually with HXCPP_GC_FIXED_BLOCKS |
| *HXCPP_GC_VERIFY*       | Check the heap's consistency around every collection and stop on the first error. Slow; for debugging the GC |
| *HXCPP_GC_DEBUG_ALWAYS_MOVE* | Implies HXCPP_GC_MOVING and moves every movable object at each collection, to flush out native code that keeps raw pointers. Slow; for debugging |
| *HXCPP_PTHREAD_FAST_TLS* | On Linux, read the collector's thread context with pthread_getspecific, as before 6.0, instead of from a __thread pointer |
| *HXCPP_CATCH_SEGV*      | Turn a segmentation fault into a Haxe exception (a signal handler, or an SEH translator with MSVC), set up on each thread |
| *HXCPP_SIGNAL_THROW*    | Convert memory errors to Haxe exceptions without extra code |
| *HXCPP_STRICT_CASTS*    | Throw on a failed cast instead of producing null |
| *HXCPP_PCRE_JIT*        | Use PCRE2's JIT for EReg, 3-5x faster. A match that runs out of JIT stack is retried on the interpreter. Ignored on iOS, tvOS, watchOS and Emscripten |
| *HXCPP_SSL_NO_TICKETS*  | Build TLS servers without RFC 5077 session tickets |
| *hxcpp_mbedtls_provider* | Set by a library that bundles mbedTLS, to its provider file: static builds then build sys.ssl on that copy instead of libhxcpp's own (docs/MbedTLSProvider.md) |
| *HXCPP_TRACY*           | Build with the Tracy profiler. Add HXCPP_TRACY_MEMORY to record allocations, HXCPP_TRACY_INCLUDE_CALLSTACKS for call stacks, HXCPP_TRACY_ON_DEMAND to profile only while a viewer is connected, HXCPP_TRACY_NO_EXIT to wait for the viewer before exiting, and HXCPP_TRACY_TIMER_FALLBACK where the CPU has no invariant TSC (some virtual machines) |
| *HXCPP_DEBUG_LINK*      | Add symbols to final binary, even in release mode. |
| *HXCPP_DEBUG_LINK_AND_STRIP* | Build with symbols as HXCPP_DEBUG_LINK does, then strip the final binary (for Android release builds) |
| *HXCPP_OPTIMIZE_FOR_SIZE* | Optimise for code size rather than speed |
| *HXCPP_OPTIMIZE_LINK*   | Optimise across files when linking (link-time code generation with MSVC, -flto with GCC and Clang). Slower to build |
| *HXCPP_PGO*             | "generate" builds a program that counts what it runs, "use" builds it again from those counts. See [ProfileGuidedOptimisation.md](../ProfileGuidedOptimisation.md) |
| *HXCPP_PGO_DIR*         | Where HXCPP_PGO keeps the counts, default "pgo" in the build folder |
| *HXCPP_STACK_TRACE*     | Have valid function-level stack traces, even in release mode. |
| *HXCPP_STACK_LINE*      | Include line information in stack traces, even in release mode. |
| *HXCPP_CPPIA_STACK_SIZE* | The size in bytes of the stack each thread keeps cppia script frames on, default 131072 (128 KB). A script that recurses past it gets a "Stack Overflow" exception (docs/ThreadsAndStacks.md) |
| *HXCPP_CPPIA_EAGER_JIT* | Have the cppia JIT compile all of a module's functions when the module loads, rather than each function when it is first called |
| *HXCPP_CHECK_POINTER*   | Add null-pointer checks,even in release mode. |
| *HXCPP_PROFILER*        | Add profiler support |
| *HXCPP_TELEMETRY*       | Add telemetry support |
| *HXCPP_SANITIZE*        | Build with the given gcc/clang sanitizers on Linux and macOS, eg "-D HXCPP_SANITIZE=address,undefined". With MSVC, address and fuzzer (fuzzer-no-link works the same there). With address, the runtime turns off ASan's use-after-return check by default, since the collector cannot see locals it moves off the stack; ASAN_OPTIONS can still turn it on |
| *HXCPP_SQLITE_FTS5*     | Build SQLite with the FTS5 full-text search extension. The JSON functions are always built in |
| *HXCPP_C_STANDARD*      | Set default C standard |
| *HXCPP_CXX_STANDARD*    | Set default C++ standard |
| *HXCPP_CPP17*           | Use C++17, as HXCPP_CXX_STANDARD=17 does |
| *HXCPP_OBJC_STANDARD*   | Set default Objective-C standard |
| *HXCPP_OBJCXX_STANDARD* | Set default Objective-C++ standard |
| *exe_link*              | Generate executable file (rather than dynamic library on android) |
| *static_link*           | Generate static library |
| *dll_link*              | Generate dynamic library |

Other defines:

| Define                  | Meaning            |
|-------------------------|--------------------|
| *HXCPP_VERBOSE*         | Print extra output from build tool. |
| *HXCPP_QUIET*           | Print less output from the build tool |
| *HXCPP_SILENT*          | Print only errors from the build tool |
| *HXCPP_COMPILE_THREADS* | Number of files to compile at once (also read from the environment) |
| *HXCPP_PROGRESS_PERCENT* | Show a group's progress as a percentage of its files, instead of a count |
| *HXCPP_COMPILE_CACHE*   | Directory for the compile cache, usually set in the environment. See [CompileCache.md](../CompileCache.md) |
| *HXCPP_TIMES*           | Show some basic profiling information |
| *HXCPP_NEKO_BUILDTOOL*  | Force use of hxcpp.n, rather than compiled BuildTool.exe
| *HXCPP_NO_COLOUR*       | Same as HXCPP_NO_COLOR |
| *HXCPP_NO_COLOR*        | Do not add colour-codes to tool output |
| *HXCPP_COLOR*           | Add colour-codes to tool output, even where the terminal is not recognised or NO_COLOR is set |
| *HXCPP_KEEP_TEMP*       | Does not delete the files created for file 'embedName' option |


Defines affecting target architecture.

| Define                  | Meaning            |
|-------------------------|--------------------|
| *HXCPP_M32*             | Force 32-bit compile for current desktop |
| *HXCPP_M64*             | Force 64-bit compile for current desktop |
| *HXCPP_NO_M32*          | Do not pass -m32 on Linux, for compilers that are 32-bit already |
| *HXCPP_ARMV6*           | Compile arm-based devices for armv6 |
| *HXCPP_ARM64*           | Compile arm-based devices for 64 bits |
| *HXCPP_ARMV7*           | Compile arm-based devices for armv7 |
| *HXCPP_ARMV7S*          | Compile arm-based devices for armv7s |
| *HXCPP_LINUX_ARMV7*     | Run on a linux ARMv7 device |
| *HXCPP_LINUX_ARM64*     | Run on a linux ARM64 device |
| *winrt*                 | Compile for windowsRt/windows UWP |
| *android*               | Compile for android |
| *HXCPP_ANDROID_PLATFORM* | Specify the android platform for NDK compilation |
| *ANDROID_NDK_ROOT*      | Specify the location of the android NDK toolchain |
| *ANDROID_NDK_DIR*       | Specify the search location for finding the android NDK toolchain |
| *HXCPP_X86*             | Compile android for x86 architecture (32-bit) |
| *HXCPP_X86_64*          | Compile android for x86 architecture (64-bit) |
| *iphoneos*              | Compile for iphone iOS |
| *iphonesim*             | Compile for iphone simulator |
| *appletvos*             | Compile for apple tvOS |
| *appletvsim*            | Compile for apple tvOS simulator |
| *watchos*               | Compile for apple watchOS |
| *watchsimulator*        | Compile for apple watchOS simulator |
| *webos*                 | Compile for webOS |
| *tizen*                 | Compile for Tizen |
| *blackberry*            | Compile for Blackberry |
| *emscripten*            | Compile for Emscripten |
| *HXCPP_LINK_NO_MEMORY_GROWTH* | Emscripten: link with fixed memory (HXCPP_LINK_TOTAL_MEMORY, else Emscripten's 16 MB) instead of letting it grow |
| *HXCPP_LINK_STACK_SIZE* | Emscripten: the stack size, in bytes or with a suffix such as 1MB. Default 5MB |
| *HXCPP_LINK_NO_STACK_SIZE* | Emscripten: leave the stack size to Emscripten, whose default is 64 KB |
| *HXCPP_LINK_NO_EMBIND*  | Emscripten: link an executable without Embind (--bind), for one without JavaScript bindings. HXCPP_JS_PRIME keeps it |
| *HXCPP_LINK_NODERAWFS*  | Emscripten: under node, use the host's files directly instead of an empty in-memory file system. The program then runs only under node |
| *cygwin*                | Compile for windows using cygwin |
| *linux*                 | (Cross) Compile for linux |
| *rpi*                   | (Cross) Compile for raspberry pi |
| *mingw*                 | Compile for windows using mingw |
| *HXCPP_MINGW*           | Compile for windows using mingw |
| *NO_AUTO_MSVC*          | Do not detect msvc location, use the one already in the executable path |
| *HXCPP_WINXP_COMPAT*    | Remain compatible with Windows XP. Disables condition variables. No effect on ARM. SQLite and TLS need Windows Vista or later whatever this is set to |
| *HXCPP_MSVC_LLVM*       | Compile for windows using the LLVM-based MSVC toolchain |
| *HXCPP_MSVC_ROOT*       | Set the root for MSVC libraries and headers |
