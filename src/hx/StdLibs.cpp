#include <hxcpp.h>
#include <hxMath.h>
#include <hx/Memory.h>
#include <hx/Thread.h>
#include "FastMutex.h"

#ifdef HX_WINDOWS
#include <windows.h>
#include <io.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif
#ifndef __EMSCRIPTEN__
typedef int64_t __int64;
#endif
#endif

#ifdef ANDROID
#include <android/log.h>
#endif
#ifdef WEBOS
#include <syslog.h>
#endif
#ifdef TIZEN
#include <dlog.h>
#endif
#if defined(BLACKBERRY) || defined(GCW0)
#include <unistd.h>
#endif
#include <string>
#include <map>
#include <stdio.h>
#include <time.h>
#include <clocale>
#include <mutex>
#include <chrono>

#ifdef HX_WINRT
#define PRINTF WINRT_PRINTF
#elif defined(TIZEN)
#define PRINTF(fmt, ...) dlog_dprint(DLOG_INFO, "trace", fmt, __VA_ARGS__);
#elif defined(HX_ANDROID) && !defined(HXCPP_EXE_LINK)
#define PRINTF(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "trace", fmt, __VA_ARGS__);
#elif defined(WEBOS)
#define PRINTF(fmt, ...) syslog(LOG_INFO, "trace", fmt, __VA_ARGS__);
#else
#define PRINTF printf
#endif

void __hx_stack_set_last_exception();
void __hx_stack_push_last_exception();

int _hxcpp_argc = 0;
char **_hxcpp_argv = 0;

namespace hx
{

Dynamic Throw(Dynamic inDynamic)
{
   #ifdef HXCPP_STACK_TRACE
   __hx_stack_set_last_exception();
   #endif
   throw inDynamic;
}


Dynamic Rethrow(Dynamic inDynamic)
{
   #ifdef HXCPP_STACK_TRACE
   __hx_stack_push_last_exception();
   #endif
   throw inDynamic;
}


null NullArithmetic(const char *inErr)
{
	Throw( String::create(inErr) );
	return null();
}

}

// -------- Resources ---------------------------------------

namespace hx
{

//typedef std::map<std::wstring,Resource> ResourceSet;
//static ResourceSet sgResources;

// The host's resources are registered first and stay. Each cppia module
// registers its own while it is loaded: there used to be room for one, so a
// second module hid the first's. Modules load and release on any thread, so
// the list is locked, and nothing is allocated from the collector while it is
// held: a thread waiting for it is not at a safe point.
Resource *sgResources = 0;
static std::vector<Resource *> &sgScriptResources = *new std::vector<Resource *>();
static hx::FastMutex &sgScriptResourceMutex = *new hx::FastMutex();

void RegisterResources(Resource *inResources)
{
   if (sgResources)
   {
      std::lock_guard<hx::FastMutex> lock(sgScriptResourceMutex);
      sgScriptResources.push_back(inResources);
   }
   else
      sgResources = inResources;
}

void UnregisterResources(Resource *inResources)
{
   std::lock_guard<hx::FastMutex> lock(sgScriptResourceMutex);
   for(size_t i=0;i<sgScriptResources.size();i++)
      if (sgScriptResources[i]==inResources)
      {
         sgScriptResources.erase(sgScriptResources.begin()+i);
         break;
      }
}

// A copy of the data of the script resource named inName, from the module
// loaded last that has one: the module could be released, and its data freed,
// once the lock is let go
static bool findScriptResource(const String &inName, std::string &outData)
{
   std::lock_guard<hx::FastMutex> lock(sgScriptResourceMutex);
   for(int t=(int)sgScriptResources.size()-1;t>=0;t--)
      for(Resource *reso = sgScriptResources[t]; reso->mData; reso++)
         if (reso->mName == inName)
         {
            outData.assign((const char *)reso->mData, reso->mDataLength);
            return true;
         }
   return false;
}

}

using namespace hx;

Array<String> __hxcpp_resource_names()
{
   Array<String> result(0,0);

   if (sgResources)
      for(Resource *reso  = sgResources; reso->mData; reso++)
         result->push( reso->mName );

   // The scripts' names, in load order. The array is allocated with the lock
   //  let go, then filled with it held, again if a module came or went
   for(;;)
   {
      int count = 0;
      {
         std::lock_guard<hx::FastMutex> lock(sgScriptResourceMutex);
         for(size_t t=0;t<sgScriptResources.size();t++)
            for(Resource *reso = sgScriptResources[t]; reso->mData; reso++)
               count++;
      }
      int base = result->length;
      result->__SetSizeExact(base + count);
      std::lock_guard<hx::FastMutex> lock(sgScriptResourceMutex);
      int n = 0;
      for(size_t t=0;t<sgScriptResources.size();t++)
         for(Resource *reso = sgScriptResources[t]; reso->mData; reso++)
            if (n++<count)
               result->__unsafe_set(base + n - 1, reso->mName);
      if (n==count)
         break;
      result->__SetSizeExact(base);
   }

   return result;
}

String __hxcpp_resource_string(String inName)
{
   if (sgResources)
      for(Resource *reso  = sgResources; reso->mData; reso++)
      {
         if (reso->mName == inName)
         #if (HXCPP_API_LEVEL > 0)
         {
            #ifdef HX_SMART_STRINGS
            const unsigned char *p = reso->mData;
            for(int i=0;i<reso->mDataLength;i++)
               if (p[i]>127)
                  return String::create((const char *)p, reso->mDataLength);
            #endif
            return String((const char *) reso->mData, reso->mDataLength );
         }
         #else
            return String::create((const char *) reso->mData, reso->mDataLength );
         #endif
      }

   std::string data;
   if (findScriptResource(inName, data))
   {
      int len = (int)data.size();
      #ifdef HX_SMART_STRINGS
      const unsigned char *p = (const unsigned char *)data.data();
      for(int i=0;i<len;i++)
         if (p[i]>127)
            return _hx_utf8_to_utf16(p, len,false);
      #endif
      // A copy: a script's resources are freed with its module
      return String::create(data.data(), len );
   }
   return null();
}

Array<unsigned char> __hxcpp_resource_bytes(String inName)
{
   if (sgResources)
      for(Resource *reso  = sgResources; reso->mData; reso++)
      {
         if (reso->mName == inName)
         {
            int len = reso->mDataLength;
            Array<unsigned char> result( len, 0);
            memcpy( result->GetBase() , reso->mData, len );
            return result;
         }
      }
   std::string data;
   if (findScriptResource(inName, data))
   {
      int len = (int)data.size();
      Array<unsigned char> result( len, 0);
      memcpy( result->GetBase() , data.data(), len );
      return result;
   }

   return null();
}

// -- hx::Native -------

extern "C" void __hxcpp_lib_main();
namespace hx
{
   // Never destroyed (#371)
   static std::string &initReturnBuffer = *new std::string();
   const char *Init(bool stayAttached)
   {
      try
      {
         __hxcpp_lib_main();
         if (!stayAttached)
            SetTopOfStack(0,true);
         return 0;
      }
      catch(Dynamic e)
      {
         HX_TOP_OF_STACK
         if (!stayAttached)
         {
            initReturnBuffer = e->toString().utf8_str();
            SetTopOfStack(0,true);
            return initReturnBuffer.c_str();
         }
         return e->toString().utf8_str();
      }
   }
}

// --- System ---------------------------------------------------------------------

// --- Maths ---------------------------------------------------------
// Thread-local xoshiro256++ generator.  The old C rand() backing took a
// global libc lock per call on glibc (three calls per Math.random()),
// produced the same default-seeded sequence on every Windows worker
// thread (MSVC rand state is per-thread but only the booting thread was
// seeded), and had only 32 bits of state.
namespace
{
   inline unsigned long long hxRotl64(unsigned long long x, int k)
   {
      return (x << k) | (x >> (64 - k));
   }

   struct HxRandState
   {
      unsigned long long s[4];
      bool seeded;

      void seed()
      {
         // The TLS address is distinct per live thread, the counters are
         // distinct per run - splitmix64 whitens them into a full state
         unsigned long long mix = (unsigned long long)(size_t)this;
         mix ^= ((unsigned long long)time(0)) << 24;
         mix ^= (unsigned long long)clock();
         // Fine-grained on every platform, so two runs started in the same
         // second (with no address randomisation) still differ
         mix ^= (unsigned long long)std::chrono::high_resolution_clock::now().time_since_epoch().count();
         #ifdef HX_WINDOWS
         LARGE_INTEGER now;
         QueryPerformanceCounter(&now);
         mix ^= (unsigned long long)now.QuadPart;
         #endif
         for(int i=0;i<4;i++)
         {
            mix += 0x9e3779b97f4a7c15ULL;
            unsigned long long z = mix;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            s[i] = z ^ (z >> 31);
         }
         seeded = true;
      }

      inline unsigned long long next()
      {
         if (!seeded)
            seed();
         unsigned long long result = hxRotl64(s[0] + s[3], 23) + s[0];
         unsigned long long t = s[1] << 17;
         s[2] ^= s[0];
         s[3] ^= s[1];
         s[1] ^= s[2];
         s[0] ^= s[3];
         s[2] ^= t;
         s[3] = hxRotl64(s[3], 45);
         return result;
      }
   };

   thread_local HxRandState sHxRand;
}

double __hxcpp_drand()
{
   // 53 random mantissa bits in [0,1)
   return (sHxRand.next() >> 11) * (1.0/9007199254740992.0);
}

int __hxcpp_irand(int inMax)
{
   if (inMax<=0)
      return 0;
   // Multiply-shift bounding - cheaper than modulo and avoids its bias
   return (int)(( (sHxRand.next()>>32) * (unsigned long long)(unsigned int)inMax ) >> 32);
}

#ifdef HX_WINDOWS
LARGE_INTEGER qpcFrequency;
#endif

void __hxcpp_stdlibs_boot()
{
#ifdef HX_WINDOWS
    // MSDN states that QueryPerformanceFrequency will always succeed on XP and above, so I'm ignoring the result.
    QueryPerformanceFrequency(&qpcFrequency);
#endif

   #if defined(HX_WINDOWS) && !defined(HX_WINRT)
   if (!AttachConsole(ATTACH_PARENT_PROCESS)) {

   } else if (GetConsoleWindow()) {
      if (_fileno(stdout) < 0 || _get_osfhandle(fileno(stdout)) < 0)
         freopen("CONOUT$", "w", stdout);
      if (_fileno(stderr) < 0 || _get_osfhandle(fileno(stderr)) < 0)
         freopen("CONOUT$", "w", stderr);
      if (_fileno(stdin) < 0 || _get_osfhandle(fileno(stdin)) < 0)
         freopen("CONIN$", "r", stdin);
   }
   //_setmode(_fileno(stdout), 0x00040000); // _O_U8TEXT
   //_setmode(_fileno(stderr), 0x00040000); // _O_U8TEXT
   //_setmode(_fileno(stdin), 0x00040000); // _O_U8TEXT
   #endif

   setlocale(LC_NUMERIC, "C");

   // I think this does more harm than good.
   //  It does not cause fread to return immediately - as perhaps desired.
   //  But it does cause some new-line characters to be lost.
   //setbuf(stdin, 0);
   setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
   setbuf(stderr, 0);
}

#ifdef HX_WINDOWS
void WriteConsoleAllW(HANDLE h, const wchar_t *str, DWORD length) {
   DWORD total_written = 0;
   DWORD remaining = length;
   while (total_written < length) {
      DWORD written;
      if (!WriteConsoleW(h, str + total_written, remaining, &written, NULL))
      {
         return;
      }
      if (written == remaining) {
         return;
      }
      total_written += written;
      remaining -= written;
   }
}

void WriteConsoleAllA(HANDLE h, const char *str, DWORD length) {
   DWORD total_written = 0;
   DWORD remaining = length;
   while (total_written < length) {
      DWORD written;
      if (!WriteConsoleA(h, str + total_written, remaining, &written, NULL))
      {
         return;
      }
      if (written == remaining) {
         return;
      }
      total_written += written;
      remaining -= written;
   }
}
#endif

namespace
{
// Output to stdout can block for a long time: a pipe nobody reads, a slow
//  reader, or a Windows console with text selected. Outside a GC-free zone, the
//  next collection on any thread waits for it, and every other thread with it.
//  The text is made before, since nothing may allocate in a zone, and a thread
//  the collector does not know needs no zone.
struct OutputZone
{
   bool entered;
   OutputZone() : entered(HX_CTX_GET && hx::TryGCFreeZone()) { }
   ~OutputZone() { if (entered) hx::ExitGCFreeZone(); }
   OutputZone(const OutputZone &) = delete;
   OutputZone &operator=(const OutputZone &) = delete;
};
}

void __trace(Dynamic inObj, Dynamic info)
{
   String text;
   if (inObj != null())
      text = inObj->toString();

#ifdef HX_WINDOWS
   HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
   DWORD mode;
   if (GetConsoleMode(handle, &mode))
   {
      String s;
      if (info == null()) {
         s = HX_CSTRING("?? ") + text + HX_CSTRING("\n");
      } else {
         String filename = Dynamic((info)->__Field(HX_CSTRING("fileName"), HX_PROP_DYNAMIC))->toString();
         int line = Dynamic((info)->__Field(HX_CSTRING("lineNumber"), HX_PROP_DYNAMIC))->__ToInt();
         s = filename + HX_CSTRING(":") + line + HX_CSTRING(": ") + text + HX_CSTRING("\n");
      }
      if (s.isUTF16Encoded())
      {
         const wchar_t *wide = s.__WCStr();
         OutputZone zone;
         fflush(stdout);
         WriteConsoleAllW(handle, wide, s.length);
      } else {
         // ascii
         const char *narrow = s.__CStr();
         OutputZone zone;
         fflush(stdout);
         WriteConsoleAllA(handle, narrow, s.length);
      }
      return;
   }
#endif

   hx::strbuf convertBuf;
   const char *textOut = text.raw_ptr() ? text.out_str(&convertBuf) : "null";
   if (info==null())
   {
      OutputZone zone;
      PRINTF("?? %s\n", textOut);
      fflush(stdout);
   }
   else
   {
      const char *filename = Dynamic((info)->__Field(HX_CSTRING("fileName"), HX_PROP_DYNAMIC))->toString().utf8_str(0,false);
      int line = Dynamic((info)->__Field( HX_CSTRING("lineNumber") , HX_PROP_DYNAMIC))->__ToInt();

      OutputZone zone;
      PRINTF("%s:%d: %s\n", filename, line, textOut);
      fflush(stdout);
   }
}

void __hxcpp_exit(int inExitCode)
{
   exit(inExitCode);
}

double  __time_stamp()
{
// The starting point is set by the first call, from whichever thread makes it.
// Static locals are initialised exactly once even when threads race to it,
// where a check for zero let two threads both set it, and on Apple let one
// read the timebase while another filled it in.
#ifdef HX_WINDOWS
   static const __int64 t0 = []() { __int64 v = 0; QueryPerformanceCounter((LARGE_INTEGER*)&v); return v; }();
   static const double period = qpcFrequency.QuadPart ? 1.0/qpcFrequency.QuadPart : 0;
   __int64 now;

   if (period!=0 && QueryPerformanceCounter((LARGE_INTEGER*)&now))
      return (now-t0)*period;
   return (double)clock() / ( (double)CLOCKS_PER_SEC);
#elif defined(__APPLE__)
   static const mach_timebase_info_data_t timebase = []() { mach_timebase_info_data_t tb; mach_timebase_info(&tb); return tb; }();
   static const unsigned long long t0 = mach_absolute_time();
   unsigned long long now = mach_absolute_time();
   return (double)(now-t0) * timebase.numer / timebase.denom * 1e-9;
#elif defined(__unix__)
   struct timespec ts;
   if( clock_gettime(CLOCK_MONOTONIC,&ts) )
      throw Dynamic("Could not get time");
   double t =  ( ts.tv_sec + ((double)ts.tv_nsec) / 1000000000.0 );
   static const double t0 = t;
   return t-t0;
#else
   return (double)clock() / ( (double)CLOCKS_PER_SEC);
#endif
}

::cpp::Int64 __time_stamp_ms()
{
#ifdef HX_WINDOWS
    // MSDN states that QueryPerformanceCounter will always succeed on XP and above, so I'm ignoring the result.
    auto now = LARGE_INTEGER{ 0 };
    QueryPerformanceCounter(&now);

    return now.QuadPart * LONGLONG{ 1000 } / qpcFrequency.QuadPart;
#else
    auto time = timespec();

    if (clock_gettime(CLOCK_MONOTONIC, &time))
    {
        throw ::Dynamic(HX_CSTRING("Failed to get the monotonic clock time"));
    }

    return time.tv_sec * 1000 + (time.tv_nsec / 1000000);
#endif
}

#if defined(HX_WINDOWS) && !defined(HX_WINRT)

/*
ISWHITE and ParseCommandLine are based on the implementation of the
.NET Core runtime, CoreCLR, which is licensed under the MIT license:
Copyright (c) Microsoft. All rights reserved.
See LICENSE file in the CoreCLR project root for full license information.

The original source code of ParseCommandLine can be found in
https://github.com/dotnet/coreclr/blob/master/src/vm/util.cpp
*/

#define ISWHITE(x) ((x)==(' ') || (x)==('\t') || (x)==('\n') || (x)==('\r') )

static void ParseCommandLine(LPWSTR psrc, Array<String> &out)
{
    unsigned int argcount = 1;       // discovery of arg0 is unconditional, below

    bool    fInQuotes;
    int     iSlash;

    /* A quoted program name is handled here. The handling is much
       simpler than for other arguments. Basically, whatever lies
       between the leading double-quote and next one, or a terminal null
       character is simply accepted. Fancier handling is not required
       because the program name must be a legal NTFS/HPFS file name.
       Note that the double-quote characters are not copied, nor do they
       contribute to numchars.

       This "simplification" is necessary for compatibility reasons even
       though it leads to mishandling of certain cases.  For example,
       "c:\tests\"test.exe will result in an arg0 of c:\tests\ and an
       arg1 of test.exe.  In any rational world this is incorrect, but
       we need to preserve compatibility.
    */

    LPWSTR pStart = psrc;
    bool skipQuote = false;

    // Pairs of double-quotes vanish...
    while(psrc[0]=='\"' && psrc[1]=='\"')
       psrc += 2;

    if (*psrc == '\"')
    {
        // scan from just past the first double-quote through the next
        // double-quote, or up to a null, whichever comes first
        psrc++;
        while ((*psrc!= '\"') && (*psrc != '\0'))
        {
           psrc++;
           // Pairs of double-quotes vanish...
           while(psrc[0]=='\"' && psrc[1]=='\"')
              psrc += 2;
        }

        skipQuote = true;
    }
    else
    {
        /* Not a quoted program name */

        while (!ISWHITE(*psrc) && *psrc != '\0')
            psrc++;
    }

    // We have now identified arg0 as pStart (or pStart+1 if we have a leading
    // quote) through psrc-1 inclusive
    if (skipQuote)
        pStart++;
    String arg0("");
    while (pStart < psrc)
    {
        arg0 += String::fromCharCode(*pStart);
        pStart++;
    }
    // out.Add(arg0); // the command isn't part of Sys.args()

    // if we stopped on a double-quote when arg0 is quoted, skip over it
    if (skipQuote && *psrc == '\"')
        psrc++;

    while ( *psrc != '\0')
    {
LEADINGWHITE:

        // The outofarg state.
        while (ISWHITE(*psrc))
            psrc++;

        if (*psrc == '\0')
            break;
        else
        if (*psrc == '#')
        {
            while (*psrc != '\0' && *psrc != '\n')
                psrc++;     // skip to end of line

            goto LEADINGWHITE;
        }

        argcount++;
        fInQuotes = FALSE;

        String arg("");

        while ((!ISWHITE(*psrc) || fInQuotes) && *psrc != '\0')
        {
            switch (*psrc)
            {
            case '\\':
                iSlash = 0;
                while (*psrc == '\\')
                {
                    iSlash++;
                    psrc++;
                }

                if (*psrc == '\"')
                {
                    for ( ; iSlash >= 2; iSlash -= 2)
                    {
                        arg += String("\\");
                    }

                    if (iSlash & 1)
                    {
                        arg += String::fromCharCode(*psrc);
                        psrc++;
                    }
                    else
                    {
                        fInQuotes = !fInQuotes;
                        psrc++;
                    }
                }
                else
                    for ( ; iSlash > 0; iSlash--)
                    {
                        arg += String("\\");
                    }

                break;

            case '\"':
                fInQuotes = !fInQuotes;
                psrc++;
                break;

            default:
                arg += String::fromCharCode(*psrc);
                psrc++;
            }
        }

        out.Add(arg);
        arg = String("");
    }
}
#endif


#ifdef __APPLE__
 #if !defined(IPHONE) && !defined(APPLETV) && !defined(HX_APPLEWATCH)
   extern "C" {
   extern int *_NSGetArgc(void);
   extern char ***_NSGetArgv(void);
   }
 #endif
#endif
Array<String> __get_args()
{
   Array<String> result(0,0);
   if (_hxcpp_argc)
   {
      for(int i=1;i<_hxcpp_argc;i++)
         result->push( String::create(_hxcpp_argv[i],strlen(_hxcpp_argv[i])) );
      return result;
   }

   #ifdef HX_WINRT
   // Do nothing
   #elif defined(HX_WINDOWS)
   LPWSTR str =  GetCommandLineW();
   ParseCommandLine(str, result);
   #else
   #ifdef __APPLE__

   #if !defined(IPHONE) && !defined(APPLETV) && !defined(HX_APPLEWATCH)
   int argc = *_NSGetArgc();
   char **argv = *_NSGetArgv();
   for(int i=1;i<argc;i++)
      result->push( String::create(argv[i],strlen(argv[i])) );
   #endif

   #else
   #ifdef ANDROID
   // TODO: Get from java
   #elif defined(__linux__)
   char buf[80];
   sprintf(buf, "/proc/%d/cmdline", getpid());
   FILE *cmd = fopen(buf,"rb");
   bool real_arg = 0;
   if (cmd)
   {
      hx::QuickVec<char> arg;

      buf[0] = '\0';
      while (fread(buf, 1, 1, cmd))
      {
         if ((unsigned char)buf[0] == 0) // line terminator
         {
            if (real_arg)
               result->push( String::create(arg.mPtr, arg.mSize) );
            real_arg = true;
            arg.clear();
         }
         else
            arg.push(buf[0]);
      }
      fclose(cmd);
   }
   #endif

   #endif
   #endif
   return result;
}


#ifdef HX_WINDOWS
// stdout console state - GetConsoleMode is a kernel round-trip, so probing
// it per print made every println pay a syscall even when redirected.
// Cached once: a process's stdout handle does not change under us in any
// supported configuration.
static bool hxStdoutIsConsole(HANDLE &outHandle)
{
   static HANDLE sHandle = GetStdHandle(STD_OUTPUT_HANDLE);
   static DWORD sMode = 0;
   static bool sIsConsole = GetConsoleMode(sHandle, &sMode) != 0;
   outHandle = sHandle;
   return sIsConsole;
}
#endif

void __hxcpp_print_string(const String &inString)
{
   const String &inV = inString.raw_ptr() ? inString : HX_CSTRING("null");
#ifdef HX_WINDOWS
   HANDLE handle;
   if (hxStdoutIsConsole(handle) && inV.isUTF16Encoded())
   {
      const wchar_t *wide = inV.__WCStr();
      OutputZone zone;
      fflush(stdout);
      WriteConsoleAllW(handle, wide, inV.length);
      return;
   }
#endif
   hx::strbuf convertBuf;
   const char *out = inV.out_str(&convertBuf);
   OutputZone zone;
   PRINTF("%s", out );
}

void __hxcpp_println_string(const String &inString)
{
   const String &inV = inString.raw_ptr() ? inString : HX_CSTRING("null");
#ifdef HX_WINDOWS
   HANDLE handle;
   bool isConsole = hxStdoutIsConsole(handle);
   if (isConsole && inV.isUTF16Encoded())
   {
      const wchar_t *wide = inV.__WCStr();
      OutputZone zone;
      fflush(stdout);
      WriteConsoleAllW(handle, wide, inV.length);
      fwrite("\n", 1, 1, stdout);
      fflush(stdout);
      return;
   }
#endif
   hx::strbuf convertBuf;
   const char *out = inV.out_str(&convertBuf);
   OutputZone zone;
   PRINTF("%s\n", out);
   // Flush every line, redirected or not.  Boot asks for line buffering,
   // but the Windows CRT treats _IOLBF as full buffering, so a redirected
   // stdout there held a server's log lines back from whatever reads them,
   // and lost them if the process died.  Elsewhere the buffer is already
   // empty after the newline, so this costs nothing.
   fflush(stdout);
}


// --- Casting/Converting ---------------------------------------------------------


bool __instanceof(const Dynamic &inValue, const Dynamic &inType)
{
   if (inValue==null())
      return false;
   if (inType==hx::Object::__SGetClass())
      return true;
   hx::Class c = inType;
   if (c==null())
      return false;
   // Classes and enums are both Class_obj, so the Class and Enum types are told
   // apart by whether the value is an enum, as on the other targets
   if (c.mPtr==hx::Class_obj::__SGetClass().mPtr || c.mPtr==hx::GetEnumClass().mPtr)
   {
      hx::Class_obj *value = dynamic_cast<hx::Class_obj *>(inValue.GetPtr());
      return value && value->__IsEnum()==(c.mPtr==hx::GetEnumClass().mPtr);
   }
   return c->CanCast(inValue.GetPtr());
}


int __int__(double x)
{
   // NaN slips past both range comparisons into an undefined (int) cast -
   // MSVC x64 yields INT_MIN, ARM yields 0, so Math.floor(NaN) differed
   // across platforms of the same target
   if (x != x)
      return 0;
   #ifndef __EMSCRIPTEN__
   if (x < -0x7fffffff || x>0x7fffffff )
   {
      // Past 64 bits, infinities included, the cast is undefined. x86 gives
      // the 64-bit minimum there, whose low 32 bits are 0
      if (x < -9223372036854775808.0 || x >= 9223372036854775808.0)
         return 0;
      __int64 big_int = (__int64)(x);
      return big_int & 0xffffffff;
   }
   else
   #endif
      return (int)x;
}


// A hex prefix with a character after it. Each character is read only once
// the one before it matched, so the string is never measured or overrun
static inline bool is_hex_string(const char *c)
{
   return (c[0] == '0' && (c[1] == 'x' || c[1] == 'X') && c[2])
      || ((c[0] == '-' || c[0] == '+') && c[1] == '0' && (c[2] == 'x' || c[2] == 'X') && c[3]);
}

// Locale-independent strtod - the process locale is global mutable state
// that host frameworks change after boot, which flips the decimal separator
// and silently breaks parseFloat (see the matching helper in String.cpp)
#if defined(HX_WINDOWS) && !defined(HX_WINRT)
static double hxStrtodC(const char *inStr, char **outEnd)
{
   static _locale_t cLoc = _create_locale(LC_NUMERIC, "C");
   return _strtod_l(inStr, outEnd, cLoc);
}
#elif defined(__APPLE__) || defined(__GLIBC__) || (defined(__ANDROID_API__) && __ANDROID_API__>=21)
#ifdef __APPLE__
#include <xlocale.h>
#endif
static double hxStrtodC(const char *inStr, char **outEnd)
{
   static locale_t cLoc = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
   locale_t old = uselocale(cLoc);
   double result = strtod(inStr, outEnd);
   uselocale(old);
   return result;
}
#else
static double hxStrtodC(const char *inStr, char **outEnd)
{
   return strtod(inStr, outEnd);
}
#endif

static bool hxParseIntChars(const char *str, int &outResult)
{
   // On the first non space char check to see if we've got a hex string.
   // isspace requires an unsigned char value - a negative char (any
   // non-ASCII utf8 byte) is undefined behaviour
   while (isspace((unsigned char)*str)) ++str;
   bool isHex = is_hex_string(str);
   char *end = 0;
   // Parse at a fixed 64-bit width so the overflow behaviour does not
   // depend on the platform's long size (32-bit on Windows, 64-bit on
   // most others)
   long long result;
   if (isHex)
   {
      bool neg = str[0] == '-';
      if (neg) str++;
      result = (long long)strtoull(str,&end,16);
      if (neg) result = -result;
   }
   else
   {
      result = strtoll(str,&end,10);
      // Saturate decimal values to the Int range on every platform
      if (result > 0x7fffffffLL)
         result = 0x7fffffffLL;
      else if (result < -0x80000000LL)
         result = -0x80000000LL;
   }
   #ifdef HX_WINDOWS
   if (str==end && !isHex)
   #else
   if (str==end)
   #endif
      return false;
   outResult = (int)result;
   return true;
}

// The text parseInt and parseFloat read. Both read only ASCII, and the first
// other unit of a UTF-16 string ends a number as the end of the string does,
// so only the ASCII before it is converted, not the whole string.
static const char *hxParseText(const String &inString, hx::strbuf *outBuf)
{
   #ifdef HX_SMART_STRINGS
   if (inString.isUTF16Encoded())
      return inString.ascii_substr(outBuf,0,inString.length);
   #endif
   return inString.utf8_str(outBuf);
}

Dynamic __hxcpp_parse_int(const String &inString)
{
   if (!inString.raw_ptr())
      return null();
   hx::strbuf buf;
   int result;
   if (!hxParseIntChars(hxParseText(inString, &buf), result))
      return null();
   return result;
}

int __hxcpp_parse_substr_int(const String &inString,int start, int length)
{
   if (length<0)
      length = inString.length - start;
   if (start<0 || start>=inString.length || length<1 || length>inString.length-start)
      return 0;

   hx::strbuf buf;
   const char *str = inString.ascii_substr(&buf,start,length);
   int result = 0;
   if (!str || !hxParseIntChars(str, result))
      return 0;
   return result;
}


// strtod also reads inf, nan and hex, which parseFloat does not. "Infinity"
// is read, as JavaScript does, so Std.string of an infinity reads back.
static double hxParseFloatChars(const char *str)
{
   while (isspace((unsigned char)*str)) ++str;
   const char *digits = str;
   bool neg = *digits=='-';
   if (*digits=='+' || *digits=='-') ++digits;
   if (!strncmp(digits,"Infinity",8))
      return neg ? -Math_obj::POSITIVE_INFINITY : Math_obj::POSITIVE_INFINITY;
   if (digits[0]=='0' && (digits[1]=='x' || digits[1]=='X'))
      return neg ? -0.0 : 0.0;
   if (!isdigit((unsigned char)*digits) && !(*digits=='.' && isdigit((unsigned char)digits[1])))
      return Math_obj::NaN;
   return hxStrtodC(str,0);
}

double __hxcpp_parse_substr_float(const String &inString,int start, int length)
{
   if (start<0 || start>=inString.length || length<1 || length>inString.length-start)
      return Math_obj::NaN;

   hx::strbuf buf;
   const char *str = inString.ascii_substr(&buf,start,length);
   return str ? hxParseFloatChars(str) : Math_obj::NaN;
}


double __hxcpp_parse_float(const String &inString)
{
   if (!inString.raw_ptr())
      return Math_obj::NaN;

   hx::strbuf buf;
   const char *str = hxParseText(inString, &buf);
   return str ? hxParseFloatChars(str) : Math_obj::NaN;
}


bool __hxcpp_same_closure(Dynamic &inF1,Dynamic &inF2)
{
   hx::Object *p1 = inF1.GetPtr();
   hx::Object *p2 = inF2.GetPtr();
   if (p1==0 || p2==0)
      return false;
   if ( (p1->__GetHandle() != p2->__GetHandle()))
      return false;
   return p1->__Compare(p2)==0;
}

namespace hx
{

struct VarArgFunc : public hx::Object
{
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdClosure };

   VarArgFunc(Dynamic &inFunc) : mRealFunc(inFunc) {
     HX_OBJ_WB_NEW_MARKED_OBJECT(this)
   }

#if (HXCPP_API_LEVEL>=500)
   VarArgFunc(::hx::Callable<::Dynamic(::cpp::VirtualArray)>& inFunc) : mRealFunc(inFunc) {
       HX_OBJ_WB_NEW_MARKED_OBJECT(this)
   }
#endif

   int __GetType() const HXCPP_OVERRIDE { return vtFunction; }
   ::String __ToString() const HXCPP_OVERRIDE { return mRealFunc->__ToString(); }

   void __Mark(hx::MarkContext *__inCtx) HXCPP_OVERRIDE { HX_MARK_MEMBER(mRealFunc); }

   #ifdef HXCPP_VISIT_ALLOCS
   void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE { HX_VISIT_MEMBER(mRealFunc); }
   #endif

   void *__GetHandle() const HXCPP_OVERRIDE { return mRealFunc.GetPtr(); }
   Dynamic __Run(const Array<Dynamic> &inArgs) HXCPP_OVERRIDE
   {
#if (HXCPP_API_LEVEL>=500)
       return hx::invoker::invoke(mRealFunc.mPtr, inArgs);
#else
       return mRealFunc->__run(inArgs);
#endif
   }

   Dynamic mRealFunc;
};

}
#if (HXCPP_API_LEVEL>=500)
Dynamic __hxcpp_create_var_args(::hx::Callable<::Dynamic(::cpp::VirtualArray)>& inArrayFunc)
#else
Dynamic __hxcpp_create_var_args(Dynamic &inArrayFunc)
#endif
{
   return Dynamic(new hx::VarArgFunc(inArrayFunc));
}

// --- CFFI helpers ------------------------------------------------------------------


// Field name management



static hx::FastMutex &sgFieldMapMutex = *new hx::FastMutex();

// Transparent comparator: lets find() take the raw char* without
// constructing a std::string (a heap allocation) per lookup - val_id is
// called from native extensions inside per-frame callbacks
struct FieldNameLess
{
   typedef void is_transparent;
   bool operator()(const std::string &a, const std::string &b) const { return a < b; }
   bool operator()(const std::string &a, const char *b) const { return a.compare(b) < 0; }
   bool operator()(const char *a, const std::string &b) const { return b.compare(a) > 0; }
};
typedef std::map<std::string,int,FieldNameLess> StringToField;

// These need to be pointers because of the unknown order of static object construction.
String *sgFieldToString=0;
int    sgFieldToStringSize=0;
int    sgFieldToStringAlloc=0;
StringToField *sgStringToField=0;

static String sgNullString;


const String &__hxcpp_field_from_id( int f )
{
   if (!sgFieldToString)
      return sgNullString;

   return sgFieldToString[f];
}


int  __hxcpp_field_to_id( const char *inFieldName )
{
   // A name that is not ASCII is UTF-8, and its String is decoded from it, as
   //  Haxe's own is: wrapping the bytes made a different String, so the field
   //  was not found. Decoding allocates, which must not happen with the lock
   //  held, so the String is made first, and only for a name not seen yet.
   String decoded;
   bool ascii = true;
   for(const unsigned char *c = (const unsigned char *)inFieldName; *c; c++)
      if (*c>127)
      {
         ascii = false;
         break;
      }
   if (!ascii)
   {
      {
         std::lock_guard<hx::FastMutex> lock(sgFieldMapMutex);
         if (sgStringToField)
         {
            StringToField::iterator i = sgStringToField->find(inFieldName);
            if (i!=sgStringToField->end())
               return i->second;
         }
      }
      decoded = String::create(inFieldName,(int)strlen(inFieldName));
      decoded.makePermanent();
   }

   std::lock_guard<hx::FastMutex> lock(sgFieldMapMutex);

   if (!sgFieldToStringAlloc)
   {
      sgFieldToStringAlloc = 100;
      sgFieldToString = (String *)HxAlloc(sgFieldToStringAlloc * sizeof(String));

      sgStringToField = new StringToField;
   }

   StringToField::iterator i = sgStringToField->find(inFieldName);
   if (i!=sgStringToField->end())
      return i->second;

   int result = sgFieldToStringSize;
   (*sgStringToField)[std::string(inFieldName)] = result;
   String str = decoded;
   if (ascii)
   {
      str = String(inFieldName,strlen(inFieldName));

      // Make into "const" string that will not get collected...
      str = String((char *)hx::InternalCreateConstBuffer(str.raw_ptr(),(str.length+1) * sizeof(char),true), str.length );
   }

   if (sgFieldToStringAlloc<=sgFieldToStringSize+1)
   {
      int oldAlloc = sgFieldToStringAlloc;
      String *oldData = sgFieldToString;
      sgFieldToStringAlloc *= 2;
      String *newData = (String *)malloc(sgFieldToStringAlloc*sizeof(String));
      if (oldAlloc)
         memcpy((void *)newData, oldData, oldAlloc*sizeof(String));
      // Let oldData dangle to keep it thread safe, rather than require mutex on id read.
      sgFieldToString = newData;
   }
   sgFieldToString[sgFieldToStringSize++] = str;
   return result;
}

// --- haxe.Int32 ---------------------------------------------------------------------
void __hxcpp_check_overflow(int x)
{
   if( (((x) >> 30) & 1) != ((unsigned int)(x) >> 31) )
      throw Dynamic(HX_CSTRING("Overflow ")+x);
}

// --- Memory ---------------------------------------------------------------------

unsigned char *__hxcpp_memory = 0;

void  __hxcpp_memory_memset(Array<unsigned char> &inBuffer ,int pos, int len, int value)
{
   // Clamp the start - a negative pos would write before the buffer
   if (pos<0)
   {
      len += pos;
      pos = 0;
   }
   if (pos<inBuffer->length)
   {
      // Overflow-safe upper clamp
      if (len > inBuffer->length - pos)
         len = inBuffer->length - pos;
      if (len>0)
         memset( inBuffer->Pointer() + pos, value, len);
   }
}

