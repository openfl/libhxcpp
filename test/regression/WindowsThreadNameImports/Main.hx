// libhxcpp #456. Every Windows program imported GetThreadDescription and
// SetThreadDescription from KERNEL32.dll at load time, which only Windows 10
// 1607 and later have, so on Windows 8.1 and older it did not start at all.
// They are looked up when used now. This reads the program's own import table.
@:cppFileCode('
#ifdef HX_WINDOWS
#include <windows.h>
#include <string.h>
static bool importsFromKernel32(const char *inName)
{
   unsigned char *base = (unsigned char *)GetModuleHandleW(nullptr);
   IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
   IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
   IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
   if (!dir.VirtualAddress)
      return false;
   for(IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++)
   {
      if (_stricmp((const char *)(base + d->Name), "KERNEL32.dll"))
         continue;
      IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
      for(; thunk->u1.AddressOfData; thunk++)
      {
         if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal))
            continue;
         IMAGE_IMPORT_BY_NAME *byName = (IMAGE_IMPORT_BY_NAME *)(base + thunk->u1.AddressOfData);
         if (!strcmp((const char *)byName->Name, inName))
            return true;
      }
   }
   return false;
}
#else
static bool importsFromKernel32(const char *) { return false; }
#endif
')
class Main {
	static function main() {
		// Makes sure the thread code is linked and used
		var name = sys.thread.Thread.current();
		for (f in ["GetThreadDescription", "SetThreadDescription"]) {
			var imported:Bool = untyped __cpp__("importsFromKernel32({0}.utf8_str())", f);
			Sys.println('imports $f: $imported');
		}
	}
}
