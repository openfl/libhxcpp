#include <hxcpp.h>
#include <hxMath.h>

#ifdef HX_WINRT
#include<Roapi.h>
#elif defined(HX_WINDOWS)
#include <windows.h>
#endif
namespace hx
{

void ParkThreadsForExit();

void Boot()
{
   //__hxcpp_enable(false);
   #ifdef HX_WINRT
   HRESULT hr = ::RoInitialize(  RO_INIT_MULTITHREADED );
   #endif

	#ifdef GPH
	 setvbuf( stdout , 0 , _IONBF , 0 );
	 setvbuf( stderr , 0 , _IONBF , 0 );
	#endif

   __hxcpp_stdlibs_boot();
   Object::__boot();
	Dynamic::__boot();
	hx::Class_obj::__boot();
	String::__boot();
	Anon_obj::__boot();
	ArrayBase::__boot();
	EnumBase_obj::__boot();
   Math_obj::__boot();

   #if !defined(HXCPP_SINGLE_THREADED_APP) && !defined(HX_WINRT)
   // Other threads stop running Haxe code once the process starts to exit.
   //  A DLL's atexit handlers run as it is detached, when the process has
   //  already ended the other threads, perhaps one holding the lock this takes
   static bool parkAtExit = false;
   if (!parkAtExit)
   {
      parkAtExit = true;
      #ifdef HX_WINDOWS
      HMODULE module = 0;
      GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
         (LPCSTR)&Boot, &module);
      if (module == GetModuleHandleA(0))
      #endif
         atexit(ParkThreadsForExit);
   }
   #endif
}

}


