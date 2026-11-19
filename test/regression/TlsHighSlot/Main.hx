import sys.thread.Lock;
import sys.thread.Thread;

// libhxcpp #458. On 32-bit Windows the collector's per-thread context was read
// from the thread environment block's inline TLS slots whatever its slot, but
// only the first 64 are inline. In a process that had used 64 slots before the
// runtime started, as a host with many DLLs can, every thread looked
// unregistered and the first allocation stopped the process. Here, with MSVC, a
// CRT initializer that runs before the runtime takes TLS slots up to 70.
@:cppFileCode('
#if defined(_MSC_VER) && !defined(HX_WINRT)
#include <windows.h>
#include <stdio.h>
extern "C" void __cdecl hxUseUpInlineTlsSlots(void)
{
   for (int i = 0; i < 128; i++)
   {
      DWORD last = TlsAlloc();
      if (last == TLS_OUT_OF_INDEXES || last >= 70)
         break;
   }
}
#pragma section(".CRT$XCT", read)
extern "C" __declspec(allocate(".CRT$XCT")) void (__cdecl *hxUseUpInlineTlsSlotsInit)(void) = hxUseUpInlineTlsSlots;
#ifdef _WIN64
#pragma comment(linker, "/include:hxUseUpInlineTlsSlotsInit")
#else
#pragma comment(linker, "/include:_hxUseUpInlineTlsSlotsInit")
#endif
#endif
')
class Main {
	static function strings(n:Int)
		return [for (i in 0...n) Std.string(i)].length;

	static function main() {
		var made = strings(100000);
		var done = new Lock();
		var madeOnThread = 0;
		Thread.create(() -> {
			madeOnThread = strings(100000);
			cpp.vm.Gc.run(true);
			done.release();
		});
		done.wait();
		cpp.vm.Gc.run(true);
		Sys.println('made $made strings, and $madeOnThread on another thread, and collected');
	}
}
