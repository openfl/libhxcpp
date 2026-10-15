Defines
-------

There are a number of standard defines you can use to control the hxcpp build.  Some of these are used by the haxe compiler, and affect then generated code.  Others apply to the build tool and affect how the code is compiled.

Defines affecting how the code is generated.  These need to be in the command line when calling haxe.

| Define                  | Meaning            |
|-------------------------|--------------------|
| *HXCPP_DEBUGGER*        | Add extra macros required by debugger.  Usually added automatically be debugger haxelib |
| *HXCPP_GC_GENERATIONAL* | Enable experimental generational garbage collector |
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
| *HXCPP_GC_DEBUG_LEVEL*  | Number 1-4 indicating additional debugging in GC |
| *HXCPP_GC_VERIFY*       | Check the heap's consistency around every collection and stop on the first error. Slow; for debugging the GC |
| *HXCPP_GC_DEBUG_ALWAYS_MOVE* | Implies HXCPP_GC_MOVING and moves every movable object at each collection, to flush out native code that keeps raw pointers. Slow; for debugging |
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
| *HXCPP_STACK_TRACE*     | Have valid function-level stack traces, even in release mode. |
| *HXCPP_STACK_LINE*      | Include line information in stack traces, even in release mode. |
| *HXCPP_CHECK_POINTER*   | Add null-pointer checks,even in release mode. |
| *HXCPP_PROFILER*        | Add profiler support |
| *HXCPP_TELEMETRY*       | Add telemetry support |
| *HXCPP_SANITIZE*        | Build with the given gcc/clang sanitizers on Linux and macOS, eg "-D HXCPP_SANITIZE=address,undefined" |
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
| *cygwin*                | Compile for windows using cygwin |
| *linux*                 | (Cross) Compile for linux |
| *rpi*                   | (Cross) Compile for raspberry pi |
| *mingw*                 | Compile for windows using mingw |
| *HXCPP_MINGW*           | Compile for windows using mingw |
| *NO_AUTO_MSVC*          | Do not detect msvc location, use the one already in the executable path |
| *HXCPP_WINXP_COMPAT*    | Remain compatible with Windows XP. Disables condition variables. No effect on ARM. |
| *HXCPP_MSVC_LLVM*       | Compile for windows using the LLVM-based MSVC toolchain |
| *HXCPP_MSVC_ROOT*       | Set the root for MSVC libraries and headers |
