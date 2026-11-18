#include <hxcpp.h>
#include <hx/OS.h>
#include <hx/Thread.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <string>
#include <vector>
#include <time.h>

#ifndef EPPC
#include <sys/types.h>
#include <sys/stat.h>
#ifdef __linux__
#include <fcntl.h>
#endif
#endif



#ifdef NEKO_WINDOWS
   #include <windows.h>
   #include <direct.h>
   #include <conio.h>
   #include <locale.h>
#else
   #include <errno.h>
   #ifndef EPPC
      #include <unistd.h>
      #include <dirent.h>
      #include <termios.h>
      #include <sys/time.h>
      #include <sys/times.h>
   #endif
   #include <limits.h>
   #ifndef ANDROID
      #include <locale.h>
      #if !defined(BLACKBERRY) && !defined(EPPC) && !defined(GCW0) && !defined(__GLIBC__) && !defined(__linux__) && !defined(NEKO_BSD)
         #include <xlocale.h>
      #endif
   #endif
#endif

#ifdef __EMSCRIPTEN__
   #include <sys/wait.h>
#endif

#if !defined(IPHONE) && !defined(APPLETV) && !defined(HX_APPLEWATCH)
   #ifdef NEKO_MAC
      #include <sys/syslimits.h>
      #include <limits.h>
      #include <mach-o/dyld.h>
   #endif
#endif

#if defined(HX_WINRT) && !defined(_XBOX_ONE)
   #include <string>
#endif

#if defined(HX_ANDROID) || defined(NEKO_BSD)
 #include <sys/wait.h>
#endif

#ifndef CLK_TCK
   #define CLK_TCK   100
#endif


/**
   <doc>
   <h1>System</h1>
   <p>
   Interactions with the operating system.
   </p>
   </doc>
**/

/**
   get_env : string -> string?
   <doc>Get some environment variable if exists</doc>
**/

// The environment is shared mutable state: putenv/setenv may free or move
// the storage a concurrent getenv result points into.  Serialize access and
// copy under the lock.
static HxMutex &hxEnvMutex()
{
   // Never destroyed, so a thread reading the environment at exit cannot
   //  take it after its destructor ran (#371)
   static HxMutex *sMutex = new HxMutex();
   return *sMutex;
}

String _hx_std_get_env( String v )
{
   #ifdef HX_WINRT
      return String();
   #else
      // Copy the value out under the lock, and make the GC string after
      // releasing it: an allocation can start a collection, which would
      // wait forever for a thread blocked on this lock
      bool found = false;
      #if defined(NEKO_WINDOWS) && defined(HX_SMART_STRINGS)
         hx::strbuf wbuf;
         const wchar_t *name = v.wchar_str(&wbuf);
         std::wstring value;
         {
            AutoLock lock(hxEnvMutex());
            const wchar_t *found_value = _wgetenv(name);
            if (found_value)
            {
               found = true;
               value = found_value;
            }
         }
      #else
         hx::strbuf buf;
         const char *name = v.utf8_str(&buf);
         std::string value;
         {
            AutoLock lock(hxEnvMutex());
            const char *found_value = getenv(name);
            if (found_value)
            {
               found = true;
               value = found_value;
            }
         }
      #endif
      if (!found)
         return String();
      return String::create(value.c_str(), (int)value.size());
   #endif
}

/**
   put_env : var:string -> val:string -> void
   <doc>Set some environment variable value</doc>
**/
void _hx_std_put_env( String e, String v )
{
#ifdef HX_WINRT
   // Do nothing
#elif defined(NEKO_WINDOWS)
   String set = e + HX_CSTRING("=") + (v != null()?v:HX_CSTRING(""));

   // Convert before taking the lock - conversion can allocate, and an
   // allocation must not happen under it (see _hx_std_get_env)
   // The C library reads TZ in tzset, and some of its local time calls never
   //  run it again, so a new zone took effect only sometimes (libhxcpp #392)
   bool zone = e.length==2 && toupper(e.cca(0))=='T' && toupper(e.cca(1))=='Z';
   #ifdef HX_SMART_STRINGS
   if (set.isUTF16Encoded())
   {
      const wchar_t *w = set.wchar_str();
      AutoLock lock(hxEnvMutex());
      _wputenv(w);
      if (zone)
         _tzset();
   }
   else
   #endif
   {
      const char *u = set.utf8_str();
      AutoLock lock(hxEnvMutex());
      putenv(u);
      if (zone)
         _tzset();
   }
#else
   const char *name = e.utf8_str();
   const char *value = v == null() ? 0 : v.utf8_str();
   AutoLock lock(hxEnvMutex());
   if (!value)
      unsetenv(name);
   else
      setenv(name,value,1);
   // The C library reads TZ in tzset, and localtime_r need not run it again,
   //  so a new zone took effect only sometimes (libhxcpp #392)
   if (!strcmp(name,"TZ"))
      tzset();
#endif
}

/**
   sys_sleep : number -> void
   <doc>Sleep a given number of seconds</doc>
**/

void _hx_std_sys_sleep( double f )
{
   // A negative duration sleeps for zero - the double->DWORD conversion on
   // Windows wraps it to ~49.7 days, and schedulers routinely compute
   // sleep(deadline - now), which dips below zero under load.  (!(f>0) also
   // catches NaN.)  Zero still sleeps: Sys.sleep(0) gives up the time slice,
   // and spin-wait loops rely on that to let other threads run.
   if (!(f > 0))
      f = 0;
   hx::EnterGCFreeZone();
#if defined(NEKO_WINDOWS)
   // Sleep takes a DWORD of milliseconds, and 0xffffffff is INFINITE. A longer
   //  sleep, infinity included, converted to a short one or none, so it goes in
   //  parts that fit
   double ms = f * 1000;
   while (ms >= 4294967294.0)
   {
      Sleep(4294967294u);
      ms -= 4294967294.0;
   }
   Sleep((DWORD)ms);
#elif defined(EPPC)
//TODO: Implement sys_sleep for EPPC
#else
   // In parts of up to 1e9 seconds, which fit an int: a longer sleep, infinity
   //  included, overflowed the conversion and returned at once
   do
   {
      double part = f < 1e9 ? f : 1e9;
      f -= part;
      struct timespec t;
      struct timespec tmp;
      t.tv_sec = (int)(part);
      t.tv_nsec = (int)((part - t.tv_sec) * 1e9);
      while( nanosleep(&t,&tmp) == -1 )
      {
         if( errno != EINTR )
         {
            hx::ExitGCFreeZone();
            return;
         }
         t = tmp;
      }
   }
   while (f > 0);
#endif
   hx::ExitGCFreeZone();
}

/**
   set_time_locale : string -> bool
   <doc>Set the locale for LC_TIME, returns true on success</doc>
**/
bool _hx_std_set_time_locale( String l )
{
#if defined(ANDROID) || defined(GCW0)
    return false;
#else

#ifdef NEKO_POSIX
   locale_t lc, old;
   lc = newlocale(LC_TIME_MASK,l.utf8_str(),NULL);
   if( !lc )
      return false;
   old = uselocale(lc);
   if( !old )
   {
      freelocale(lc);
      return false;
   }
   if( old != LC_GLOBAL_LOCALE )
      freelocale(old);
   return true;
#else
   #if defined(NEKO_WINDOWS) && defined(HX_SMART_STRINGS)
   if (l.isUTF16Encoded())
      return _wsetlocale(LC_TIME,l.wchar_str());
   #endif
   return setlocale(LC_TIME,l.utf8_str());
#endif

#endif // !Android
}

/**
   get_cwd : void -> string
   <doc>Return current working directory</doc>
**/
String _hx_std_get_cwd()
{
   #ifdef HX_WINRT
   return HX_CSTRING("ms-appdata:///local/");
   #elif defined(EPPC)
   return String();
   #else
#ifdef NEKO_WINDOWS
   std::vector<wchar_t> buf;
   DWORD l = 0;
   do
   {
      buf.resize(l + 2);
      l = GetCurrentDirectoryW((DWORD)buf.size() - 1, buf.data());
      if( !l )
         return String();
   } while( l >= buf.size() - 1 );
#else
   std::vector<char> buf(1024);
   while( getcwd(buf.data(), buf.size() - 1) == NULL )
   {
      if( errno != ERANGE )
         return String();
      buf.resize(buf.size() * 2);
   }
   size_t l = strlen(buf.data());
#endif
   if( buf[l-1] != '/' && buf[l-1] != '\\' ) {
      buf[l] = '/';
      buf[l+1] = 0;
   }
   return String::create(buf.data());
   #endif
}

/**
   set_cwd : string -> void
   <doc>Set current working directory</doc>
**/
bool _hx_std_set_cwd( String d )
{
   #if !defined(HX_WINRT) && !defined(EPPC)
#ifdef NEKO_WINDOWS
   // _wchdir also updates the drive's =X: variable, as the CRT and the other targets do
   bool ok = _wchdir(d.wchar_str()) == 0;
#else
   bool ok = chdir(d.utf8_str()) == 0;
#endif
   if (!ok)
      hx::Throw(HX_CSTRING("Could not set the current directory to ") + d);
   return true;
   #else
   return false;
   #endif
}


/**
   sys_string : void -> string
   <doc>
   Return the local system string. The current value are possible :
   <ul>
   <li>[Windows]</li>
   <li>[Linux]</li>
   <li>[BSD]</li>
   <li>[Mac]</li>
   </ul>
   </doc>
**/
String _hx_std_sys_string()
{
#if defined(HX_WINRT)
   return HX_CSTRING("WinRT");
#elif defined(NEKO_WINDOWS)
   return HX_CSTRING("Windows");
#elif defined(NEKO_GNUKBSD)
   return HX_CSTRING("GNU/kFreeBSD");
#elif defined(NEKO_LINUX)
   return HX_CSTRING("Linux");
#elif defined(NEKO_BSD)
   return HX_CSTRING("BSD");
#elif defined(NEKO_MAC)
   return HX_CSTRING("Mac");
#elif defined(ANDROID)
   return HX_CSTRING("Android");
#elif defined(BLACKBERRY)
   return HX_CSTRING("BlackBerry");
#elif defined(__EMSCRIPTEN__)
   return HX_CSTRING("Emscripten");
#elif defined(EPPC)
   return HX_CSTRING("EPPC");
#else
#error Unknow system string
#endif
}

/**
   sys_is64 : void -> bool
   <doc>
   Returns true if we are on a 64-bit system
   </doc>
**/
bool _hx_std_sys_is64()
{
#ifdef NEKO_64BITS
   return true;
#else
   return false;
#endif
}

/**
   sys_command : string -> int
   <doc>Run the shell command and return exit code</doc>
**/
int _hx_std_sys_command( String cmd )
{
   #if defined(HX_WINRT) || defined(__EMSCRIPTEN__) || defined(EPPC) || defined(IPHONE) || defined(APPLETV) || defined(HX_APPLEWATCH)
   return -1;
   #else
   if( !cmd.raw_ptr() || !cmd.length )
      return -1;

#ifdef NEKO_WINDOWS
   int result = 0;
   hx::EnterGCFreeZone();

   #ifdef HX_SMART_STRINGS
   if (!cmd.isUTF16Encoded())
      result = system(cmd.raw_ptr());
   else
   #endif
   {
      hx::strbuf wbuf;
      result = _wsystem(cmd.wchar_str(&wbuf));
   }
#else
   hx::strbuf buf;
   hx::EnterGCFreeZone();
   int result = system(cmd.utf8_str(&buf));
#endif
   hx::ExitGCFreeZone();

   #if !defined(NEKO_WINDOWS)
   result = WEXITSTATUS(result) | (WTERMSIG(result) << 8);
   #endif

   return result;
   #endif
}


/**
   sys_exit : int -> void
   <doc>Exit with the given errorcode. Never returns.</doc>
**/
void _hx_std_sys_exit( int code )
{
   exit(code);
}

/**
   sys_exists : string -> bool
   <doc>Returns true if the file or directory exists.</doc>
**/
bool _hx_std_sys_exists( String path )
{
   #ifdef EPPC
   return true;
   #else

#ifdef NEKO_WINDOWS
   const wchar_t * wpath = path.wchar_str();
   hx::EnterGCFreeZone();
   bool result = GetFileAttributesW(wpath) != INVALID_FILE_ATTRIBUTES;
#else
   struct stat st;
   hx::EnterGCFreeZone();
   hx::strbuf buf;
   bool result = stat(path.utf8_str(&buf),&st) == 0;
#endif
   hx::ExitGCFreeZone();

   return result;
   #endif
}

/**
   file_delete : string -> void
   <doc>Delete the file. Exception on error.</doc>
**/
void _hx_std_file_delete( String path )
{
   #ifndef EPPC
   hx::EnterGCFreeZone();

   bool err = false;
   #if defined(NEKO_WINDOWS) && defined(HX_SMART_STRINGS)
   if (path.isUTF16Encoded())
      err = _wunlink(path.wchar_str());
   else
   #endif
   {
      hx::strbuf buf;
      err = unlink(path.utf8_str(&buf));
   }

   hx::ExitGCFreeZone();

   if (err)
      hx::Throw( HX_CSTRING("Could not delete ") + path );
   #endif
}

/**
   sys_rename : from:string -> to:string -> void
   <doc>Rename the file or directory. Exception on error.</doc>
**/
void  _hx_std_sys_rename( String path, String newname )
{
   hx::EnterGCFreeZone();

   hx::strbuf buf0;
   hx::strbuf buf1;
   #ifdef NEKO_WINDOWS
   bool err = _wrename(path.wchar_str(&buf0),newname.wchar_str(&buf1));
   #else
   bool err = rename(path.utf8_str(&buf0),newname.utf8_str(&buf1));
   #endif

   hx::ExitGCFreeZone();

   if (err)
      hx::Throw(HX_CSTRING("Could not rename"));
}

#define STATF(f) o->Add(HX_CSTRING(#f),(int)(s.st_##f))
// Times as Float seconds: an Int ends in 2038. FileSystem.stat multiplies
//  them by 1000.0 for Date.fromTime, which takes either
#define STATT(f) o->Add(HX_CSTRING(#f),(double)(s.st_##f))

// statx reports when a file was created, where <sys/stat.h> declares it
#if defined(__linux__) && defined(STATX_BTIME) && (!defined(__ANDROID__) || __ANDROID_API__ >= 30)
   #define HX_STATX
#endif

/**
   sys_stat : string -> {
      gid => int,
      uid => int,
      atime => 'int,
      mtime => 'int,
      ctime => 'int,
      dev => int,
      ino => int,
      nlink => int,
      rdev => int,
      mode => int,
      size => int
   }
   <doc>Run the [stat] command on the given file or directory.</doc>
**/
Dynamic _hx_std_sys_stat( String path )
{
   #ifdef EPPC
   return alloc_null();
   #else
   hx::EnterGCFreeZone();
   bool err = false;
   // ctime is the creation time, as on Windows, where the file system records one
   double ctime = 0;
   #if defined(NEKO_WINDOWS)
      struct _stat s;
      #if defined(HX_SMART_STRINGS)
      if (path.isUTF16Encoded())
      {
         hx::strbuf buf;
         err = _wstat(path.wchar_str(&buf),&s);
      }
      else
      #endif
      {
         hx::strbuf buf;
         err = _stat(path.utf8_str(&buf),&s);
      }
      ctime = (double)s.st_ctime;
   #else
      struct stat s;
      hx::strbuf buf;
      const char *utf8 = path.utf8_str(&buf);
      err = stat(utf8,&s);
      ctime = (double)s.st_ctime;
      #if defined(HX_STATX)
      struct statx sx;
      if (!err && statx(AT_FDCWD,utf8,0,STATX_BTIME,&sx)==0 && (sx.stx_mask & STATX_BTIME))
         ctime = (double)sx.stx_btime.tv_sec;
      #elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
      if (s.st_birthtime>0)
         ctime = (double)s.st_birthtime;
      #endif
   #endif

   hx::ExitGCFreeZone();
   if (err)
      hx::Throw(HX_CSTRING("Could not stat ") + path);
   hx::Anon o = hx::Anon_obj::Create();

   STATF(gid);
   STATF(uid);
   STATT(atime);
   STATT(mtime);
   o->Add(HX_CSTRING("ctime"),ctime);
   STATF(dev);
   STATF(ino);
   STATF(mode);
   STATF(nlink);
   STATF(rdev);
   STATF(size);
   STATF(mode);

   return o;
   #endif
}

/**
   sys_file_type : string -> string
   <doc>
   Return the type of the file. The current values are possible :
   <ul>
   <li>[file]</li>
   <li>[dir]</li>
   <li>[symlink]</li>
   <li>[sock]</li>
   <li>[char]</li>
   <li>[block]</li>
   <li>[fifo]</li>
   </ul>
   </doc>
**/
String _hx_std_sys_file_type( String path )
{
   if (path==null())
      return String();
   #ifdef EPPC
   return String();
   #else
   hx::EnterGCFreeZone();
   bool err = false;
   #if defined(NEKO_WINDOWS)
      struct _stat s;
      #if defined(HX_SMART_STRINGS)
      if (path.isUTF16Encoded())
      {
         hx::strbuf buf;
         err = _wstat(path.wchar_str(&buf),&s);
      }
      else
      #endif
      {
         hx::strbuf buf;
         err = _stat(path.utf8_str(&buf),&s);
      }
   #else
      struct stat s;
      hx::strbuf buf;
      err = stat(path.utf8_str(&buf),&s);
   #endif
   hx::ExitGCFreeZone();
   if (err)
      return String();

   // The S_IF* constants are values of the S_IFMT field, not independent
   // bits - testing them with & made sockets report as "file" and block
   // devices as "dir" (FileSystem.isDirectory matched block devices)
   switch( s.st_mode & S_IFMT )
   {
      case S_IFREG: return HX_CSTRING("file");
      case S_IFDIR: return HX_CSTRING("dir");
      case S_IFCHR: return HX_CSTRING("char");
#ifndef NEKO_WINDOWS
      case S_IFLNK: return HX_CSTRING("symlink");
      case S_IFBLK: return HX_CSTRING("block");
      case S_IFIFO: return HX_CSTRING("fifo");
      case S_IFSOCK: return HX_CSTRING("sock");
#endif
   }
   return String();
   #endif
}

/**
   sys_create_dir : string -> mode:int -> void
   <doc>Create a directory with the specified rights</doc>
**/
bool _hx_std_sys_create_dir( String path, int mode )
{
   #ifdef EPPC
   return true;
   #else
#ifdef NEKO_WINDOWS
   const wchar_t * wpath = path.wchar_str();
   hx::EnterGCFreeZone();
   bool err = _wmkdir(wpath);
#else
   hx::EnterGCFreeZone();
   hx::strbuf buf;
   bool err = mkdir(path.utf8_str(&buf), mode);
#endif
   hx::ExitGCFreeZone();
   return !err;
   #endif
}

/**
   sys_remove_dir : string -> void
   <doc>Remove a directory. Exception on error</doc>
**/
void _hx_std_sys_remove_dir( String path )
{
   #ifdef EPPC
   return true;
   #else
   hx::EnterGCFreeZone();

   bool ok = false;

   #if defined(NEKO_WINDOWS) && defined(HX_SMART_STRINGS)
   if (path.isUTF16Encoded())
   {
      ok = _wrmdir(path.wchar_str()) == 0;
   }
   else
   #endif
   {
      hx::strbuf buf;
      ok = rmdir(path.utf8_str(&buf)) == 0;
   }


   hx::ExitGCFreeZone();
   if (!ok)
      hx::Throw(HX_CSTRING("Could not remove directory"));
   #endif
}

/**
   sys_time : void -> float
   <doc>Return an accurate local time stamp in seconds since Jan 1 1970</doc>
**/
double _hx_std_sys_time()
{
#ifdef NEKO_WINDOWS
#define EPOCH_DIFF   (134774*24*60*60.0)
   // GetSystemTime only updates at the ~15.6ms timer tick; the precise
   // variant (Win8+) has sub-microsecond resolution at the same epoch
   typedef VOID (WINAPI *GetTimeFunc)(LPFILETIME);
   static GetTimeFunc sGetTime = (GetTimeFunc)GetProcAddress(
         GetModuleHandleA("kernel32"), "GetSystemTimePreciseAsFileTime");
   FILETIME ft;
   ULARGE_INTEGER ui;
   if( sGetTime )
      sGetTime(&ft);
   else
      GetSystemTimeAsFileTime(&ft);
   ui.LowPart = ft.dwLowDateTime;
   ui.HighPart = ft.dwHighDateTime;
   return ( ((double)ui.QuadPart) / 10000000.0 - EPOCH_DIFF );
#elif defined(EPPC)
   time_t tod;
   time(&tod);
   return ((double)tod);
#else
   struct timeval tv;
   if( gettimeofday(&tv,NULL) != 0 )
      return 0;
   return ( tv.tv_sec + ((double)tv.tv_usec) / 1000000.0 );
#endif
}

/**
   sys_cpu_time : void -> float
   <doc>Return the most accurate CPU time spent since the process started (in seconds)</doc>
**/
double _hx_std_sys_cpu_time()
{
#if defined(HX_WINRT) && !defined(_XBOX_ONE)
    return ((double)GetTickCount64()/1000.0);
#elif defined(NEKO_WINDOWS)
   FILETIME unused;
   FILETIME stime;
   FILETIME utime;
   if( !GetProcessTimes(GetCurrentProcess(),&unused,&unused,&stime,&utime) )
      return 0;
   return ( ((double)(utime.dwHighDateTime+stime.dwHighDateTime)) * 65.536 * 6.5536 + (((double)utime.dwLowDateTime + (double)stime.dwLowDateTime) / 10000000) );
#elif defined(EPPC)
    return ((double)clock()/(double)CLOCKS_PER_SEC);
#else
   struct tms t;
   times(&t);
   return ( ((double)(t.tms_utime + t.tms_stime)) / CLK_TCK );
#endif
}

/**
   sys_read_dir : string -> string list
   <doc>Return the content of a directory</doc>
**/
Array<String> _hx_std_sys_read_dir( String p )
{
   Array<String> result = Array_obj<String>::__new();
   // An enumeration that fails part way throws, where it gave the entries
   // read so far as if they were all of them
   bool failed = false;

#if defined(NEKO_WINDOWS)
   // Sized to the path: a fixed MAX_PATH buffer threw for a longer one,
   // such as a \\?\ path, before Windows saw it. The pattern goes after a
   // backslash, as \\?\ paths take no forward slashes
   std::wstring searchPath(p.wchar_str());
   size_t len = searchPath.size();
   if( len && searchPath[len-1] != '/' && searchPath[len-1] != '\\' )
      searchPath += L'\\';
   searchPath += L"*.*";

   WIN32_FIND_DATAW d;
   HANDLE handle;
   hx::EnterGCFreeZone();
  #if defined(HX_WINRT) && !defined(_XBOX_ONE)
   handle = FindFirstFileExW(searchPath.c_str(), FindExInfoStandard, &d, FindExSearchNameMatch, NULL, 0);
  #else
   handle = FindFirstFileW(searchPath.c_str(),&d);
  #endif
   if( handle == INVALID_HANDLE_VALUE )
   {
      hx::ExitGCFreeZone();
      hx::Throw(HX_CSTRING("Invalid directory"));
   }
   while( true )
   {
      // skip magic dirs
      if( d.cFileName[0] != '.' || (d.cFileName[1] != 0 && (d.cFileName[1] != '.' || d.cFileName[2] != 0)) )
      {
         hx::ExitGCFreeZone();
         result->push(String::create(d.cFileName));
         hx::EnterGCFreeZone();
      }
      if( !FindNextFileW(handle,&d) )
      {
         failed = GetLastError() != ERROR_NO_MORE_FILES;
         break;
      }
   }
   FindClose(handle);
#elif !defined(EPPC)
   const char *name = p.utf8_str();
   hx::EnterGCFreeZone();
   DIR *d = opendir(name);
   if( d == NULL )
   {
      hx::ExitGCFreeZone();
      hx::Throw(HX_CSTRING("Invalid directory"));
   }
   while( true )
   {
      // Only errno tells the end from a failure, and the push below can
      // change it, so it is cleared before each read
      errno = 0;
      struct dirent *e = readdir(d);
      if( e == NULL )
      {
         failed = errno != 0;
         break;
      }
      // skip magic dirs
      if( e->d_name[0] == '.' && (e->d_name[1] == 0 || (e->d_name[1] == '.' && e->d_name[2] == 0)) )
         continue;
      hx::ExitGCFreeZone();
      result->push( String::create(e->d_name) );
      hx::EnterGCFreeZone();
   }
   closedir(d);
#endif
   hx::ExitGCFreeZone();
   if( failed )
      hx::Throw(HX_CSTRING("Error reading directory"));

   return result;
}

/**
   file_full_path : string -> string
   <doc>Return an absolute path from a relative one. The file or directory must exists</doc>
**/
String _hx_std_file_full_path( String path )
{
#if defined(HX_WINRT)
   return path;
#elif defined(NEKO_WINDOWS)
   wchar_t buf[MAX_PATH+1];
   hx::strbuf wbuf;
   // A return larger than the buffer is the REQUIRED size - the buffer was
   // not written, so treating it as success returned uninitialized stack
   DWORD res = GetFullPathNameW(path.wchar_str(&wbuf),MAX_PATH+1,buf,NULL);
   if( res == 0 || res > MAX_PATH+1 )
      return null();
   return String::create(buf);
#elif defined(EPPC)
   return path;
#else
   char buf[PATH_MAX];
   hx::strbuf ubuf;
   if( realpath(path.utf8_str(&ubuf),buf) == NULL )
      return null();
   return String::create(buf);
#endif
}

#ifdef __EMSCRIPTEN__
// Under node the program is the .js file node runs, so this gives its absolute
// path, where /proc/self/exe names node itself on Linux. A browser has no file
// to name, so there it gives null, as when the path cannot be found elsewhere
EM_JS_DEPS(hx_sys_exe_path, "$stringToNewUTF8");
EM_JS(char *, hx_emscripten_exe_path, (), {
   if (typeof process != "object" || typeof process.versions != "object" || typeof process.versions.node != "string" || typeof require != "function")
      return 0;
   var file = typeof __filename == "string" ? __filename : process.argv[1];
   if (!file)
      return 0;
   return stringToNewUTF8(require("path").resolve(file));
});
#endif

/**
   sys_exe_path : void -> string
   <doc>Return the path of the executable</doc>
**/
String _hx_std_sys_exe_path()
{
#if defined(HX_WINRT) && defined(__cplusplus_winrt)
   Windows::ApplicationModel::Package^ package = Windows::ApplicationModel::Package::Current;
   Windows::Storage::StorageFolder^ installedLocation = package->InstalledLocation;
   return(String::create(installedLocation->Path->Data()));
#elif defined(NEKO_WINDOWS)
   // A path that fills the buffer was cut short, so grow it and ask again:
   // a MAX_PATH buffer gave the first 259 characters of a longer path
   std::vector<wchar_t> path(MAX_PATH);
   while( true )
   {
      DWORD len = GetModuleFileNameW(NULL,&path[0],(DWORD)path.size());
      if( len == 0 )
         return null();
      if( len < path.size() )
         return String::create(&path[0],len);
      if( path.size() >= 32768 )
         return null();
      path.resize(path.size()*2);
   }
#elif defined(NEKO_MAC) && !defined(IPHONE) && !defined(APPLETV) && !defined(HX_APPLEWATCH)
   char path[PATH_MAX+1];
   uint32_t path_len = PATH_MAX;
   if( _NSGetExecutablePath(path, &path_len) )
      return null();
   return String::create(path);
#elif defined(EPPC)
   return HX_CSTRING("");
#elif defined(__EMSCRIPTEN__)
   char *path = hx_emscripten_exe_path();
   if (!path)
      return null();
   String result = String::create(path);
   free(path);
   return result;
#else
   {
      char path[PATH_MAX];
      int length = readlink("/proc/self/exe", path, sizeof(path) - 1);
      if( length < 0 )
      {
         const char *p = getenv("_");
         if (p)
            return String::create(p);
         return null();
      }
       path[length] = '\0';
      return String::create(path);
   }
#endif
}

#if !defined(IPHONE) && !defined(APPLETV) && !defined(HX_APPLEWATCH)
#ifdef NEKO_MAC
#include <crt_externs.h>
#   define environ (*_NSGetEnviron())
#endif
#endif

#ifndef NEKO_WINDOWS
extern char **environ;
#endif

/**
   sys_env : void -> #list
   <doc>Return all the (key,value) pairs in the environment as a chained list</doc>
**/
Array<String> _hx_std_sys_env()
{
   Array<String> result = Array_obj<String>::__new();
   #if defined(NEKO_WINDOWS) && defined(HX_SMART_STRINGS) && !defined(HX_WINRT)
   // The narrow environ is in the ANSI code page, so read the wide block
   std::vector<std::wstring> entries;
   {
      AutoLock lock(hxEnvMutex());
      wchar_t *block = GetEnvironmentStringsW();
      if (block)
      {
         for(const wchar_t *e = block; *e; e += wcslen(e)+1)
            entries.push_back(e);
         FreeEnvironmentStringsW(block);
      }
   }
   for(size_t i=0;i<entries.size();i++)
   {
      const std::wstring &entry = entries[i];
      // Entries like "=C:=C:\dir" are the per-drive directories, not variables
      if (entry.empty() || entry[0]==L'=')
         continue;
      size_t x = entry.find(L'=');
      if (x == std::wstring::npos)
         continue;
      result->push(String::create(entry.c_str(),(int)x));
      result->push(String::create(entry.c_str()+x+1,(int)(entry.size()-x-1)));
   }
   #elif !defined(HX_WINRT)
   // Copy the entries under the lock, build the GC strings after releasing
   // it (see _hx_std_get_env)
   std::vector<std::string> entries;
   {
      AutoLock lock(hxEnvMutex());
      for(char **e = environ; *e; e++)
         entries.push_back(*e);
   }
   for(size_t i=0;i<entries.size();i++)
   {
      const char *entry = entries[i].c_str();
      const char *x = strchr(entry,'=');
      if( x == NULL )
         continue;
      result->push(String::create(entry,(int)(x-entry)));
      result->push(String::create(x+1));
   }
   #endif
   return result;
}

#ifdef HX_ANDROID
   #define tcsetattr(fd,opt,s) ioctl(fd,opt,s)
   #define tcgetattr(fd,s) ioctl(fd,TCGETS,s)

   static __inline__ void inline_cfmakeraw(struct termios *s)
   {
       s->c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
       s->c_oflag &= ~OPOST;
       s->c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
       s->c_cflag &= ~(CSIZE|PARENB);
       s->c_cflag |= CS8;
   }

   #define cfmakeraw inline_cfmakeraw

#endif

/**
   sys_getch : bool -> int
   <doc>Read a character from stdin with or without echo</doc>
**/
int _hx_std_sys_getch( bool b )
{
#if defined(HX_WINRT) || defined(__EMSCRIPTEN__) || defined(EPPC)
   return 0;
#elif defined(NEKO_WINDOWS)
   hx::EnterGCFreeZone();
   int result = b?getche():getch();
   hx::ExitGCFreeZone();

   return result;
#else
   // took some time to figure out how to do that
   // without relying on ncurses, which clear the
   // terminal on initscr()
   int c;
   struct termios term, old;
   hx::EnterGCFreeZone();
   tcgetattr(fileno(stdin), &old);
   term = old;
   cfmakeraw(&term);
   tcsetattr(fileno(stdin), 0, &term);
   c = getchar();
   tcsetattr(fileno(stdin), 0, &old);
   if( b ) fputc(c,stdout);
   hx::ExitGCFreeZone();
   return c;
#   endif
}

/**
   sys_get_pid : void -> int
   <doc>Returns the current process identifier</doc>
**/
int _hx_std_sys_get_pid()
{
#   ifdef NEKO_WINDOWS
   return (int)(GetCurrentProcessId());
#elif defined(EPPC)
   return (1);
#   else
   return (getpid());
#   endif
}
