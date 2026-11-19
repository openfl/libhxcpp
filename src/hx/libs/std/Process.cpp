#include <hxcpp.h>
#include <hx/OS.h>

#if !defined(HX_WINRT) && !defined(EPPC)

#ifdef NEKO_WINDOWS
#   include <windows.h>
#else
#   include <sys/types.h>
#   include <unistd.h>
#   include <memory.h>
#   include <errno.h>
#   include <signal.h>
#   include <pthread.h>
#   include <fcntl.h>
#   if defined(ANDROID) || defined(BLACKBERRY) || defined(__EMSCRIPTEN__) || defined(NEKO_BSD)
#      include <sys/wait.h>
#   elif !defined(NEKO_MAC)
#      include <wait.h>
#   endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#if !defined(HX_WINRT) && !defined(EPPC)
#ifdef NEKO_WINDOWS
#include <atomic>
#else
#include <poll.h>
#endif
#endif
#if !defined(HX_WINRT) && !defined(EPPC) && !defined(NEKO_WINDOWS) && !defined(__EMSCRIPTEN__) && !defined(HXCPP_SINGLE_THREADED_APP)
#define HX_PROCESS_REAPER
#include <thread>
#include <mutex>
#include <chrono>
#endif

namespace
{

#ifndef NEKO_WINDOWS
static int do_close( int fd )
{
   POSIX_LABEL(close_again);
   if( close(fd) != 0 ) {
      HANDLE_EINTR(close_again);
      return 1;
   }
   return 0;
}
#else
// CreateProcess hands the child every inheritable handle this process holds
// unless it is given a list of the ones to pass.  The list is a Vista API,
// looked up at run time so builds whose headers predate it (MinGW, the XP
// compatible mode) still compile, and still start processes the old way
// where it is missing.
#ifndef EXTENDED_STARTUPINFO_PRESENT
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000
#endif
#ifndef PROC_THREAD_ATTRIBUTE_HANDLE_LIST
#define PROC_THREAD_ATTRIBUTE_HANDLE_LIST 0x00020002
#endif

// STARTUPINFOEXW, spelled out for the same reason
struct StartupInfoWithList
{
   STARTUPINFOW info;
   void *attributes;
};

struct HandleListApi
{
   typedef BOOL (WINAPI *InitFunc)(void *, DWORD, DWORD, SIZE_T *);
   typedef BOOL (WINAPI *UpdateFunc)(void *, DWORD, DWORD_PTR, void *, SIZE_T, void *, SIZE_T *);
   typedef void (WINAPI *DeleteFunc)(void *);

   InitFunc init;
   UpdateFunc update;
   DeleteFunc remove;

   HandleListApi() : init(0), update(0), remove(0)
   {
      HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
      if (kernel)
      {
         init = (InitFunc)GetProcAddress(kernel,"InitializeProcThreadAttributeList");
         update = (UpdateFunc)GetProcAddress(kernel,"UpdateProcThreadAttribute");
         remove = (DeleteFunc)GetProcAddress(kernel,"DeleteProcThreadAttributeList");
      }
   }

   bool available() const { return init && update && remove; }

   static const HandleListApi &get()
   {
      static HandleListApi api;
      return api;
   }
};
#endif


#ifdef HX_PROCESS_REAPER

// Children closed or collected while still running. Nothing else waits for
// them, so they would stay zombies for as long as this process lives. One
// native thread reaps them, polling only these pids: never waitpid(-1), which
// would take children other code is waiting for, and no SIGCHLD handler, so an
// embedding host keeps its own policy. It ends when the list is empty. The
// state is never destroyed, since a finalizer may still use it at exit.
namespace
{
   struct ProcessReaper
   {
      std::mutex lock;
      std::vector<pid_t> pids;
      bool running = false;
   };
   ProcessReaper &sReaper = *new ProcessReaper();

   // True once the child is gone: reaped here, or not ours to wait for (as
   // when the host ignores SIGCHLD and the system reaps it)
   bool reapIfDone(pid_t inPid)
   {
      int r;
      do
         r = waitpid(inPid, 0, WNOHANG);
      while (r<0 && errno==EINTR);
      return r==inPid || (r<0 && errno==ECHILD);
   }

   void reaperLoop()
   {
      int delayMs = 10;
      while(true)
      {
         {
            std::lock_guard<std::mutex> l(sReaper.lock);
            for(size_t i=0; i<sReaper.pids.size(); )
            {
               if (reapIfDone(sReaper.pids[i]))
               {
                  sReaper.pids[i] = sReaper.pids.back();
                  sReaper.pids.pop_back();
               }
               else
                  i++;
            }
            if (sReaper.pids.empty())
            {
               sReaper.running = false;
               return;
            }
         }
         std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
         if (delayMs<200)
            delayMs *= 2;
      }
   }

   // Takes no GC memory and never waits on the collector, so it is safe from
   // the finalizer during a collection
   void reapLater(pid_t inPid)
   {
      std::lock_guard<std::mutex> l(sReaper.lock);
      sReaper.pids.push_back(inPid);
      if (!sReaper.running)
      {
         try
         {
            std::thread(reaperLoop).detach();
            sReaper.running = true;
         }
         catch(...)
         {
            // The pid stays listed; the next close starts the thread again
         }
      }
   }
}
#endif


struct vprocess : public hx::Object
{
  HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdProcess };

   bool open;
#ifdef NEKO_WINDOWS
   HANDLE oread;
   HANDLE eread;
   HANDLE iwrite;
   PROCESS_INFORMATION pinf;
   // Threads inside ReadFile or WriteFile on the pipes, and whether close has
   // begun: see PipeCall
   std::atomic<int> pipeCalls;
   std::atomic<bool> closing;
   // The same for the writes to stdin alone, which stdin_close cancels
   std::atomic<int> stdinCalls;
   std::atomic<bool> stdinClosing;
   #define HANDLE_INIT 0
#else
   int oread;
   int eread;
   int iwrite;
   int pid;
   bool reaped;
   int exitStatus;
   #define HANDLE_INIT -1
#endif

   void create()
   {
      open = true;
      oread = HANDLE_INIT;
      eread = HANDLE_INIT;
      iwrite = HANDLE_INIT;
      #ifdef NEKO_WINDOWS
      pipeCalls = 0;
      closing = false;
      stdinCalls = 0;
      stdinClosing = false;
      #else
      reaped = false;
      exitStatus = 0;
      #endif
      _hx_set_finalizer(this, finalize);
   }

   void destroy()
   {
      if (open)
      {
         #ifdef NEKO_WINDOWS
            // CloseHandle on a pipe waits for a ReadFile or WriteFile in
            // progress on it in another thread, and a descendant of the child
            // that inherited the pipe can keep that going for as long as it
            // lives (libhxcpp #261). New calls are refused from here, and the
            // ones in progress are cancelled until none is left: one that had
            // taken its handle but not yet entered ReadFile misses the first
            // cancel, so it is retried.
            closing = true;
            for(int spins=0; pipeCalls>0; spins++)
            {
               if (oread) CancelIoEx(oread, 0);
               if (eread) CancelIoEx(eread, 0);
               if (iwrite) CancelIoEx(iwrite, 0);
               Sleep(spins<10 ? 0 : 1);
            }
            if (eread)
               CloseHandle(eread);
            if (oread)
               CloseHandle(oread);
            if (iwrite)
               CloseHandle(iwrite);
            CloseHandle(pinf.hProcess);
            CloseHandle(pinf.hThread);
            // The OS recycles handle slots - kill/exitCode on a closed
            // process must not target whatever now occupies them
            memset(&pinf,0,sizeof(pinf));
         #else
            if (eread!=-1)
               do_close(eread);
            if (oread!=-1)
               do_close(oread);
            if (iwrite!=-1)
               do_close(iwrite);
            // Reap the child if it already exited - one the user never calls
            // exitCode() on otherwise stays a zombie until the host exits. A
            // child still running is reaped later, off this thread, so close
            // does not wait for it and does not kill it.
            if (pid > 0 && !reaped)
            {
               int r;
               do
                  r = waitpid(pid, 0, WNOHANG);
               while (r<0 && errno==EINTR);
               #ifdef HX_PROCESS_REAPER
               if (r==0)
                  reapLater(pid);
               #endif
            }
            pid = -1;
         #endif
         // Reset the stale values - the OS recycles handles/fds, so a later
         // stdin/stdout close on this object must not close someone else's
         eread = HANDLE_INIT;
         oread = HANDLE_INIT;
         iwrite = HANDLE_INIT;
         open = false;
      }
   }

   static void finalize(Dynamic obj)
   {
      ((vprocess *)(obj.mPtr))->destroy();
   }

   String toString() HXCPP_OVERRIDE { return HX_CSTRING("vprocess"); }
};

#ifdef NEKO_WINDOWS
// Counts a ReadFile or WriteFile on one of the process's pipes for close, which
// cancels it before closing the handles. The count goes up before closing is
// checked, so close either sees the call and waits for it, or the call sees
// close and is refused.
struct PipeCall
{
   vprocess *process;
   HANDLE handle;
   bool stdinWrite;
   PipeCall(vprocess *inProcess, HANDLE inHandle, bool inStdinWrite=false) : process(inProcess), stdinWrite(inStdinWrite)
   {
      process->pipeCalls++;
      if (stdinWrite)
         process->stdinCalls++;
      handle = process->closing || (stdinWrite && process->stdinClosing) ? 0 : inHandle;
   }
   ~PipeCall()
   {
      if (stdinWrite)
         process->stdinCalls--;
      process->pipeCalls--;
   }
};
#endif

vprocess *getProcess(Dynamic handle)
{
   vprocess *p = dynamic_cast<vprocess *>(handle.mPtr);
   if (!p)
      hx::Throw(HX_CSTRING("Invalid process"));
   return p;
}

// For operations that target the child process itself - after close() the
// pid/handle values are stale and the OS recycles them, so kill() could
// terminate an unrelated process and exitCode() hang forever
vprocess *getOpenProcess(Dynamic handle)
{
   vprocess *p = getProcess(handle);
   if (!p->open)
      hx::Throw(HX_CSTRING("Process closed"));
   return p;
}


/**
   <doc>
   <h1>Process</h1>
   <p>
   An API for starting and communication with sub processes.
   </p>
   </doc>
**/

} // end anon namespace


#ifdef NEKO_WINDOWS

template<typename T>
static String TQuoted(const T *ptr, int len)
{
   std::vector<T> quoted;
   quoted.reserve(len*2);

   unsigned int bs_count = 0;
   for(int j=0;j<len;j++)
   {
      T c = ptr[j];
      switch( c )
      {
      case '"':
         // Double backslashes.
         for (int k=0;k<bs_count*2;k++)
            quoted.push_back('\\');
         bs_count = 0;
         quoted.push_back('\\');
         quoted.push_back('"');
         break;
      case '\\':
         // Don't know if we need to double yet.
         bs_count++;
         break;
      default:
         // Normal char
         for (int k=0;k<bs_count;k++)
            quoted.push_back('\\');
         bs_count = 0;
         quoted.push_back(c);
         break;
      }
   }
   // Add remaining backslashes, if any.
   for (int k=0;k<bs_count*2;k++)
      quoted.push_back('\\');
   int qlen = (int)quoted.size();
   quoted.push_back('\0');

   return String::create( &quoted[0], qlen );
}

static String quoteString(String v)
{
   #ifdef HX_SMART_STRINGS
   if (v.isUTF16Encoded())
      return TQuoted(v.raw_wptr(),v.length);
   #endif
   return TQuoted(v.raw_ptr(),v.length);
}

#endif


/**
   process_run : cmd:string -> args:string array -> 'process
   <doc>
   Start a process using a command.
   When args is not null, cmd and args will be auto-quoted/escaped.
   If no auto-quoting/escaping is desired, you should append necessary 
   arguments to cmd as if it is inputted to the shell directly, and pass
   null to args.

   inShowParam = only for windows, SHOW_* from "ShowWindow" function
      default = 1 = SHOW_WINDOW
   </doc>
**/
#ifndef NEKO_WINDOWS
static void startFailed(int inErr)
{
   hx::Throw(HX_CSTRING("Could not start process: ") + String(strerror(inErr)));
}

// A child forked by another thread between pipe() and FD_CLOEXEC kept the
// pipe ends across exec, so one process's stdout could stay open in an
// unrelated child and never deliver EOF.  pipe2 creates the ends with
// close-on-exec already set.  Where it is missing (macOS) or the kernel lacks
// it, the ends are created and flagged under sForkLock held shared, and this
// runtime forks under it held exclusively, so none of its own forks lands in
// between
#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#define HX_PROCESS_HAS_PIPE2
#endif
static pthread_rwlock_t sForkLock = PTHREAD_RWLOCK_INITIALIZER;

static int cloexecPipe(int outFds[2])
{
   #ifdef HX_PROCESS_HAS_PIPE2
   if (pipe2(outFds, O_CLOEXEC) == 0)
      return 0;
   if (errno != ENOSYS)
      return -1;
   #endif
   pthread_rwlock_rdlock(&sForkLock);
   int result = pipe(outFds);
   if (result == 0)
   {
      fcntl(outFds[0], F_SETFD, FD_CLOEXEC);
      fcntl(outFds[1], F_SETFD, FD_CLOEXEC);
   }
   int err = errno;
   pthread_rwlock_unlock(&sForkLock);
   errno = err;
   return result;
}
#endif

Dynamic _hx_std_process_run( String cmd, Array<String> vargs, int inShowParam )
{
   #if defined(APPLETV) || defined(HX_APPLEWATCH)
   return null();

   #else
   vprocess *p = 0;
   bool isRaw = !vargs.mPtr;

   #ifdef NEKO_WINDOWS
   {       
      SECURITY_ATTRIBUTES sattr;      
      STARTUPINFOW sinf;
      HANDLE proc = GetCurrentProcess();
      HANDLE oread,eread,iwrite;
      // creates commandline
      String b;
      if (isRaw)
      {
         b = HX_CSTRING("\"");
         const char* cmdexe = getenv("COMSPEC");
         if (!cmdexe) cmdexe = "cmd.exe";
         b += String(cmdexe) + HX_CSTRING("\" /C \"") + cmd + HX_CSTRING("\"");
      }
      else
      {
         // The arguments are escaped but cmd is concatenated raw - an
         // embedded quote would close the program token early and smuggle
         // extra arguments.  Quotes cannot appear in Win32 paths anyway
         if (cmd.indexOf(HX_CSTRING("\""), null()) >= 0)
            hx::Throw(HX_CSTRING("Invalid command name"));
         b = HX_CSTRING("\"") + cmd + HX_CSTRING("\"");

         for(int i=0;i<vargs->length;i++)
         {
            b += HX_CSTRING(" \"");
            if (vargs[i].length)
               b += quoteString(vargs[i]);
            b += HX_CSTRING("\"");
         }
      }

      const wchar_t *name = b.__WCStr();

      hx::EnterGCFreeZone();
      // startup process
      sattr.nLength = sizeof(sattr);
      sattr.bInheritHandle = TRUE;
      sattr.lpSecurityDescriptor = NULL;
      memset(&sinf,0,sizeof(sinf));
      sinf.cb = sizeof(sinf);
      sinf.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
      sinf.wShowWindow = inShowParam;

      // Checked and zero-initialized: a failed CreatePipe used to leave
      // garbage stack HANDLEs that were duplicated and closed - closing
      // an arbitrary live handle of this process
      oread = eread = iwrite = 0;
      HANDLE procOread = 0, procEread = 0, procIwrite = 0;
      bool piped = CreatePipe(&oread,&sinf.hStdOutput,&sattr,0)
                && CreatePipe(&eread,&sinf.hStdError,&sattr,0)
                && CreatePipe(&sinf.hStdInput,&iwrite,&sattr,0);

      bool dup = piped
                && DuplicateHandle(proc,oread,proc,&procOread,0,FALSE,DUPLICATE_SAME_ACCESS)
                && DuplicateHandle(proc,eread,proc,&procEread,0,FALSE,DUPLICATE_SAME_ACCESS)
                && DuplicateHandle(proc,iwrite,proc,&procIwrite,0,FALSE,DUPLICATE_SAME_ACCESS);
      if (oread) CloseHandle(oread);
      if (eread) CloseHandle(eread);
      if (iwrite) CloseHandle(iwrite);

      PROCESS_INFORMATION pinf;
      memset(&pinf,0,sizeof(pinf));

      // The child inherits its three pipe ends and nothing else.  Given
      // only bInheritHandles it took every inheritable handle this process
      // held, sockets included, and a connection the child held a copy of
      // stayed open after this process closed it, until the child exited.
      // The list must outlive CreateProcess, so it lives in this frame
      const HandleListApi &api = HandleListApi::get();
      HANDLE inherit[3] = { sinf.hStdInput, sinf.hStdOutput, sinf.hStdError };
      void *attributes = 0;
      bool initialized = false;
      bool listed = false;
      if (dup && api.available())
      {
         SIZE_T size = 0;
         api.init(0,1,0,&size);
         if (size)
            attributes = malloc(size);
         initialized = attributes && api.init(attributes,1,0,&size);
         listed = initialized &&
            api.update(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherit,sizeof(inherit),0,0);
      }
      StartupInfoWithList sinfex;
      memset(&sinfex,0,sizeof(sinfex));
      sinfex.info = sinf;
      sinfex.info.cb = sizeof(sinfex);
      sinfex.attributes = attributes;

      bool started = dup &&
         CreateProcessW(NULL,(wchar_t *)name,NULL,NULL,TRUE,
            CREATE_NO_WINDOW | (listed ? EXTENDED_STARTUPINFO_PRESENT : 0),
            NULL,NULL,listed ? &sinfex.info : &sinf,&pinf);
      DWORD startError = started ? 0 : GetLastError();

      if (initialized)
         api.remove(attributes);
      if (attributes)
         free(attributes);

      // The child ends are always done with here; the parent ends too on
      // failure.  The executable-not-found path used to leak all six
      // handles per attempt
      if (sinf.hStdOutput) CloseHandle(sinf.hStdOutput);
      if (sinf.hStdError) CloseHandle(sinf.hStdError);
      if (sinf.hStdInput) CloseHandle(sinf.hStdInput);
      if (!started)
      {
         if (procOread) CloseHandle(procOread);
         if (procEread) CloseHandle(procEread);
         if (procIwrite) CloseHandle(procIwrite);
         hx::ExitGCFreeZone();
         String reason;
         wchar_t *text = 0;
         if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                            NULL, startError, 0, (LPWSTR)&text, 0, NULL) && text)
         {
            int len = (int)wcslen(text);
            while (len > 0 && (text[len-1]==L'\r' || text[len-1]==L'\n' || text[len-1]==L' ' || text[len-1]==L'.'))
               len--;
            reason = String::create(text, len);
            LocalFree(text);
         }
         hx::Throw(HX_CSTRING("Could not start process: ") + reason + HX_CSTRING(" (") + String((int)startError) + HX_CSTRING(")"));
      }
      hx::ExitGCFreeZone();

      p = new vprocess;
      p->create();
      p->oread = procOread;
      p->eread = procEread;
      p->iwrite = procIwrite;
      p->pinf = pinf;
   }
   #else // not windows ...
   {
   // The child writes exec's errno to status; a successful exec closes it.
   // All four are close-on-exec; dup2 onto 0/1/2 in the child clears the
   // flag on the std fds
   int input[2], output[2], error[2], status[2];
   if( cloexecPipe(input) )
      startFailed(errno);
   if( cloexecPipe(output) )
   {
      int err = errno;
      do_close(input[0]); do_close(input[1]);
      startFailed(err);
   }
   if( cloexecPipe(error) )
   {
      int err = errno;
      do_close(input[0]); do_close(input[1]);
      do_close(output[0]); do_close(output[1]);
      startFailed(err);
   }
   if( cloexecPipe(status) )
   {
      int err = errno;
      do_close(input[0]); do_close(input[1]);
      do_close(output[0]); do_close(output[1]);
      do_close(error[0]); do_close(error[1]);
      startFailed(err);
   }

   hx::strbuf buf;
   std::vector< std::string > values;
   if (isRaw)
   {
      values.resize(3);
      values[0] = "/bin/sh";
      values[1] = "-c";
      values[2] = cmd.utf8_str(&buf);
   }
   else
   {
      values.resize(vargs->length+1);

      values[0] = cmd.utf8_str(&buf);
      for(int i=0;i<vargs->length;i++)
         values[i+1] = vargs[i].utf8_str(&buf);
   }

   std::vector<const char *> argv(values.size()+1);
   for(int i=0;i<values.size();i++)
      argv[i] = values[i].c_str();

   // The child execs or exits holding its copy of the lock
   pthread_rwlock_wrlock(&sForkLock);
   int pid = fork();
   if( pid != 0 )
      pthread_rwlock_unlock(&sForkLock);
   if( pid == -1 )
   {
      int err = errno;
      do_close(input[0]); do_close(input[1]);
      do_close(output[0]); do_close(output[1]);
      do_close(error[0]); do_close(error[1]);
      do_close(status[0]); do_close(status[1]);
      startFailed(err);
   }

   // child
   if( pid == 0 )
   {
      close(input[1]);
      close(output[0]);
      close(error[0]);
      close(status[0]);
      // A parent started without descriptor 0, 1 or 2 hands those numbers
      // to the pipes, and a dup2 onto itself would keep close-on-exec, or
      // one could overwrite another pipe end: move them all above 2 first
      int stdFds[3] = { input[0], output[1], error[1] };
      for(int i=0;i<3;i++)
      {
         if (stdFds[i] < 3)
         {
            #ifdef F_DUPFD_CLOEXEC
            stdFds[i] = fcntl(stdFds[i], F_DUPFD_CLOEXEC, 3);
            #else
            stdFds[i] = fcntl(stdFds[i], F_DUPFD, 3);
            if (stdFds[i] >= 0)
               fcntl(stdFds[i], F_SETFD, FD_CLOEXEC);
            #endif
         }
         if (stdFds[i] < 0)
         {
            int err = errno;
            ssize_t unused = write(status[1], &err, sizeof(err));
            (void)unused;
            _exit(127);
         }
      }
      for(int i=0;i<3;i++)
         if (dup2(stdFds[i], i) < 0)
         {
            int err = errno;
            ssize_t unused = write(status[1], &err, sizeof(err));
            (void)unused;
            _exit(127);
         }
      // An ignored signal stays ignored across exec, so a host that ignores
      // SIGPIPE would pass that on, and a pipeline such as "cat big | head -1"
      // would get write errors instead of stopping quietly. Restore the
      // default, as Python's subprocess does.
      signal(SIGPIPE, SIG_DFL);
      execvp(argv[0],(char* const*)&argv[0]);
      // Only async-signal-safe calls here, as the parent is multithreaded.
      // _exit: exit() would run the parent's atexit handlers and flush its
      // duplicated stdio buffers
      int err = errno;
      ssize_t unused = write(status[1], &err, sizeof(err));
      (void)unused;
      _exit(127);
   }

   // parent
   do_close(input[0]);
   do_close(output[1]);
   do_close(error[1]);
   do_close(status[1]);

   int execError = 0;
   hx::EnterGCFreeZone();
   ssize_t got;
   do
      got = read(status[0], &execError, sizeof(execError));
   while (got < 0 && errno == EINTR);
   do_close(status[0]);
   if (got == (ssize_t)sizeof(execError))
   {
      int r;
      do
         r = waitpid(pid, 0, 0);
      while (r < 0 && errno == EINTR);
      do_close(input[1]);
      do_close(output[0]);
      do_close(error[0]);
      hx::ExitGCFreeZone();
      startFailed(execError);
   }
   hx::ExitGCFreeZone();

   p = new vprocess;
   p->create();
   p->iwrite = input[1];
   p->oread = output[0];
   p->eread = error[0];
   p->pid = pid;
   }
   #endif

   return p;

   #endif // not APPLETV/HX_APPLEWATCH
}


/**
   process_stdout_read : 'process -> buf:string -> pos:int -> len:int -> int
   <doc>
   Read up to [len] bytes in [buf] starting at [pos] from the process stdout.
   Returns the number of bytes readed this way. Raise an exception if this
   process stdout is closed and no more data is available for reading.

        For hxcpp, the input buffer is in bytes, not characters
   </doc>
**/
int _hx_std_process_stdout_read( Dynamic handle, Array<unsigned char> buf, int pos, int len )
{
   if( pos < 0 || len <= 0 || pos > buf->length || len > buf->length - pos )
      return 0;
   vprocess *p = getProcess(handle);

   unsigned char * volatile dest = &buf[0];
   hx::EnterGCFreeZone();
   #ifdef NEKO_WINDOWS
   DWORD nbytes = 0;
   {
      PipeCall call(p, p->oread);
      if( !call.handle || !ReadFile(call.handle,dest+pos,len,&nbytes,0) )
         nbytes = 0;
   }
   #else
   POSIX_LABEL(stdout_read_again);
   int nbytes = read(p->oread,dest + pos,len);
   if( nbytes < 0 )
   {
      // A signal mid-read must retry, not report EOF and drop the rest
      // of the child's output
      HANDLE_EINTR(stdout_read_again);
      nbytes = 0;
   }
   #endif

   hx::ExitGCFreeZone();
   return nbytes;
}


/**
   process_stderr_read : 'process -> buf:string -> pos:int -> len:int -> int
   <doc>
   Read up to [len] bytes in [buf] starting at [pos] from the process stderr.
   Returns the number of bytes readed this way. Raise an exception if this
   process stderr is closed and no more data is available for reading.
   </doc>
**/
int _hx_std_process_stderr_read( Dynamic handle, Array<unsigned char> buf, int pos, int len )
{
   if( pos < 0 || len <= 0 || pos > buf->length || len > buf->length - pos )
      return 0;
   vprocess *p = getProcess(handle);

   unsigned char * volatile dest = &buf[0];
   hx::EnterGCFreeZone();
   #ifdef NEKO_WINDOWS
   DWORD nbytes = 0;
   {
      PipeCall call(p, p->eread);
      if( !call.handle || !ReadFile(call.handle,dest+pos,len,&nbytes,0) )
         nbytes = 0;
   }
   #else
   POSIX_LABEL(stderr_read_again);
   int nbytes = read(p->eread,dest + pos,len);
   if( nbytes < 0 )
   {
      HANDLE_EINTR(stderr_read_again);
      nbytes = 0;
   }
   #endif

   hx::ExitGCFreeZone();
   return nbytes;
}

/**
   process_stdin_write : 'process -> buf:string -> pos:int -> len:int -> int
   <doc>
   Write up to [len] bytes from [buf] starting at [pos] to the process stdin.
   Returns the number of bytes writen this way. Raise an exception if this
   process stdin is closed.
   </doc>
**/
#ifndef NEKO_WINDOWS
// A write to a pipe whose reader has gone raises SIGPIPE, whose default action
// ends the host. It is blocked in this thread for the write, and the one the
// write raised is taken, so the host's own handler and any SIGPIPE already
// pending are left alone.
static ssize_t writeNoSigpipe(int inFd, const void *inData, size_t inLen)
{
   sigset_t pipeSet, oldSet, pending;
   sigemptyset(&pipeSet);
   sigaddset(&pipeSet, SIGPIPE);
   pthread_sigmask(SIG_BLOCK, &pipeSet, &oldSet);
   sigpending(&pending);
   bool wasPending = sigismember(&pending, SIGPIPE);

   ssize_t result;
   do
      result = write(inFd, inData, inLen);
   while (result < 0 && errno == EINTR);
   int err = errno;

   if (result < 0 && err == EPIPE && !wasPending)
   {
      sigpending(&pending);
      if (sigismember(&pending, SIGPIPE))
      {
         int sig;
         sigwait(&pipeSet, &sig);
      }
   }
   pthread_sigmask(SIG_SETMASK, &oldSet, 0);
   errno = err;
   return result;
}
#endif

int _hx_std_process_stdin_write( Dynamic handle, Array<unsigned char> buf, int pos, int len )
{
   if( pos < 0 || len <= 0 || pos > buf->length || len > buf->length - pos )
      return 0;
   vprocess *p = getProcess(handle);

   unsigned char * volatile src = &buf[0];


   hx::EnterGCFreeZone();
   #ifdef NEKO_WINDOWS
   DWORD nbytes =0;
   bool written;
   {
      PipeCall call(p, p->iwrite, true);
      written = call.handle && WriteFile(call.handle,src+pos,len,&nbytes,0);
   }
   if( !written )
   {
      hx::ExitGCFreeZone();
      // Returning 0 forever makes haxe.io.Output.writeFullBytes spin -
      // a broken pipe (child exited) must surface as an error
      hx::Throw(HX_CSTRING("EOF"));
   }
   #else
   int nbytes = (int)writeNoSigpipe(p->iwrite,src+pos,len);
   if( nbytes < 0 )
   {
      hx::ExitGCFreeZone();
      hx::Throw(HX_CSTRING("EOF"));
   }
   #endif

   hx::ExitGCFreeZone();
   return nbytes;
}

/**
   process_stdin_close : 'process -> void
   <doc>
   Close the process standard input.
   </doc>
**/
void _hx_std_process_stdin_close( Dynamic handle )
{
   vprocess *p = getProcess(handle);

   #ifdef NEKO_WINDOWS
   if ( p->iwrite )
   {
      // CloseHandle waits for a WriteFile in progress on the pipe in another
      //  thread, for as long as the child does not read, so the writes are
      //  refused and cancelled first, as destroy does for all the pipes. It
      //  waits in a zone, so a collection meanwhile need not wait for it
      hx::AutoGCFreeZone zone;
      p->stdinClosing = true;
      for(int spins=0; p->stdinCalls>0; spins++)
      {
         CancelIoEx(p->iwrite, 0);
         Sleep(spins<10 ? 0 : 1);
      }
      CloseHandle(p->iwrite);
   }
   #else
   if( p->iwrite!=-1 )
      do_close(p->iwrite);
   #endif
   p->iwrite = HANDLE_INIT;
}

// The status is lost, as when the host reaps its children itself: that is
// not a clean exit
static void exitStatusLost(const String &inReason)
{
   hx::Throw(HX_CSTRING("process_exit: ") + inReason);
}

#ifndef NEKO_WINDOWS
// Once reaped, the pid may belong to another process, so the status is kept
static int reapStatus(vprocess *p, int inStatus)
{
   p->reaped = true;
   if( WIFEXITED(inStatus) )
      p->exitStatus = WEXITSTATUS(inStatus);
   else if( WIFSIGNALED(inStatus) )
      // Shell convention - a crashed or killed child must not read as a
      // clean exit 0
      p->exitStatus = 128 + WTERMSIG(inStatus);
   else
      p->exitStatus = 0;
   return p->exitStatus;
}
#endif

/**
   process_exit : 'process -> int
   <doc>
   Wait until the process terminate, then returns its exit code.
   </doc>
**/
#if (HXCPP_API_LEVEL > 420)
Dynamic _hx_std_process_exit( Dynamic handle, bool block )
{
   vprocess *p = getOpenProcess(handle);
   #ifndef NEKO_WINDOWS
   if (p->reaped)
      return p->exitStatus;
   #endif

   hx::EnterGCFreeZone();
   #ifdef NEKO_WINDOWS
   {
      DWORD rval = 0;
      DWORD wait = INFINITE;
      if (!block)
         wait = 0;

      // The wait result is the authoritative liveness signal - inferring
      // it from rval==STILL_ACTIVE (259) made a process that really
      // exits with code 259 read as running forever
      DWORD waitResult = WaitForSingleObject(p->pinf.hProcess,wait);
      hx::ExitGCFreeZone();

      if (waitResult == WAIT_FAILED)
         exitStatusLost(HX_CSTRING("wait failed"));
      if (!block && waitResult == WAIT_TIMEOUT)
         return null();
      if( !GetExitCodeProcess(p->pinf.hProcess,&rval) )
         exitStatusLost(HX_CSTRING("GetExitCodeProcess failed"));
      return (int)rval;
   }
   #else
   int options=0;
   if (!block)
      options = WNOHANG;

   int rval=0;
   pid_t ret=-1;
   while( (ret = waitpid(p->pid,&rval,options)) != p->pid )
   {
      // errno is only meaningful when waitpid failed - a stale EINTR
      // from an earlier syscall turned the WNOHANG poll into a busy spin
      if( ret == -1 && errno == EINTR )
         continue;

      if (!block && ret == 0)
      {
         hx::ExitGCFreeZone();
         return null();
      }

      int err = errno;
      hx::ExitGCFreeZone();
      exitStatusLost(String(strerror(err)));
   }
   hx::ExitGCFreeZone();
   return reapStatus(p, rval);
   #endif
}
#else
int _hx_std_process_exit( Dynamic handle )
{
   vprocess *p = getOpenProcess(handle);
   #ifndef NEKO_WINDOWS
   if (p->reaped)
      return p->exitStatus;
   #endif

   hx::EnterGCFreeZone();
   #ifdef NEKO_WINDOWS
   {
      DWORD rval = 0;
      DWORD waitResult = WaitForSingleObject(p->pinf.hProcess,INFINITE);
      hx::ExitGCFreeZone();

      if (waitResult == WAIT_FAILED)
         exitStatusLost(HX_CSTRING("wait failed"));
      if( !GetExitCodeProcess(p->pinf.hProcess,&rval) )
         exitStatusLost(HX_CSTRING("GetExitCodeProcess failed"));
      return rval;
   }
   #else
   int rval=0;
   pid_t ret=-1;
   while( (ret = waitpid(p->pid,&rval,0)) != p->pid )
   {
      if( ret == -1 && errno == EINTR )
         continue;
      int err = errno;
      hx::ExitGCFreeZone();
      exitStatusLost(String(strerror(err)));
   }
   hx::ExitGCFreeZone();
   return reapStatus(p, rval);
   #endif
}
#endif

/**
   process_pid : 'process -> int
   <doc>
   Returns the process id.
   </doc>
**/
int _hx_std_process_pid( Dynamic handle )
{
   vprocess *p = getOpenProcess(handle);

   #ifdef NEKO_WINDOWS
   return p->pinf.dwProcessId;
   #else
   return p->pid;
   #endif
}

void _hx_std_process_kill( Dynamic handle )
{
   // After close() the handle/pid may have been recycled by the OS -
   // TerminateProcess could kill an unrelated process
   vprocess *p = getOpenProcess(handle);

   #ifdef NEKO_WINDOWS
   TerminateProcess(p->pinf.hProcess, -1);
   #else
   if (!p->reaped)
      kill(p->pid, SIGTERM);
   #endif
}



/**
   process_close : 'process -> void
   <doc>
   Close the process I/O.
   </doc>
**/
void _hx_std_process_close( Dynamic handle )
{
   vprocess *p = getProcess(handle);
   p->destroy();
}

namespace
{
// Keeps up to limit bytes of a stream (all of it when limit is 0) and drops the
// rest, so the child is never left blocked on a full pipe
struct CommunicateOutput
{
   std::vector<unsigned char> data;
   size_t limit;
   bool truncated;

   CommunicateOutput(size_t inLimit) : limit(inLimit), truncated(false) { }

   void add(const unsigned char *inData, size_t inLen)
   {
      size_t keep = inLen;
      if (limit)
      {
         size_t room = data.size() < limit ? limit - data.size() : 0;
         if (keep > room)
         {
            keep = room;
            truncated = true;
         }
      }
      data.insert(data.end(), inData, inData + keep);
   }
};

Array<unsigned char> communicateBytes(const std::vector<unsigned char> &inData)
{
   if (inData.empty())
      return Array_obj<unsigned char>::__new(0,0);
   return Array_obj<unsigned char>::fromData(inData.data(), (int)inData.size());
}

typedef std::chrono::steady_clock CommunicateClock;

// Once the child has exited, a pipe that one of its descendants still holds open
// is given up after nothing has arrived on either stream for this long
const std::chrono::milliseconds sCommunicateQuiet(100);

#ifdef NEKO_WINDOWS
struct CommunicatePipe
{
   HANDLE handle;
   CommunicateOutput *output;
   const std::vector<unsigned char> *input;
   std::atomic<bool> done;
   std::atomic<CommunicateClock::rep> *lastData;
   HANDLE thread;
};

DWORD WINAPI communicateRead(void *inPipe)
{
   CommunicatePipe *pipe = (CommunicatePipe *)inPipe;
   std::vector<unsigned char> buf(65536);
   DWORD got = 0;
   while (ReadFile(pipe->handle, buf.data(), (DWORD)buf.size(), &got, 0) && got > 0)
   {
      pipe->output->add(buf.data(), got);
      *pipe->lastData = CommunicateClock::now().time_since_epoch().count();
   }
   pipe->done = true;
   return 0;
}

DWORD WINAPI communicateWrite(void *inPipe)
{
   CommunicatePipe *pipe = (CommunicatePipe *)inPipe;
   const std::vector<unsigned char> &in = *pipe->input;
   size_t pos = 0;
   while (pos < in.size())
   {
      DWORD wrote = 0;
      DWORD chunk = (DWORD)std::min(in.size() - pos, (size_t)65536);
      if (!WriteFile(pipe->handle, in.data() + pos, chunk, &wrote, 0))
         break;
      pos += wrote;
   }
   CloseHandle(pipe->handle);
   pipe->done = true;
   return 0;
}

void startPipe(CommunicatePipe &ioPipe, LPTHREAD_START_ROUTINE inRun)
{
   ioPipe.thread = CreateThread(0, 0, inRun, &ioPipe, 0, 0);
   if (!ioPipe.thread)
      ioPipe.done = true;
}

// A thread blocked on a pipe that a descendant keeps open is woken with
// CancelSynchronousIo, which Windows XP does not have; there it is waited for
void finishPipe(CommunicatePipe &ioPipe)
{
   if (!ioPipe.thread)
      return;
   typedef BOOL (WINAPI *CancelSynchronousIoFunc)(HANDLE);
   static CancelSynchronousIoFunc cancelIo =
      (CancelSynchronousIoFunc)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CancelSynchronousIo");
   if (cancelIo)
      while (!ioPipe.done)
      {
         cancelIo(ioPipe.thread);
         Sleep(1);
      }
   WaitForSingleObject(ioPipe.thread, INFINITE);
   CloseHandle(ioPipe.thread);
}
#else
void setNonBlocking(int inFd)
{
   int flags = fcntl(inFd, F_GETFL);
   if (flags != -1)
      fcntl(inFd, F_SETFL, flags | O_NONBLOCK);
}
#endif
}

/**
   process_communicate : 'process -> input:bytes -> timeout:float -> maxOutput:int -> object
   <doc>
   Writes [input] to the process's stdin and closes it, while reading stdout and
   stderr at the same time, so neither side can block the other on a full pipe.
   Returns { stdout, stderr, exitCode, timedOut, stdoutTruncated, stderrTruncated }
   once the process has exited. A [timeout] above 0 is a deadline in seconds:
   the process is killed if it is still running then, and timedOut is true.
   A [maxOutput] above 0 keeps that many bytes of each stream and drops the rest.
   After the process exits, a stream that one of its descendants keeps open is
   read until nothing more arrives for 100 ms; descendants are not killed. The
   pipes are closed afterwards; exitCode() still works.
   </doc>
**/
Dynamic _hx_std_process_communicate( Dynamic handle, Array<unsigned char> input, Float timeout, int maxOutput )
{
   vprocess *p = getOpenProcess(handle);

   std::vector<unsigned char> in;
   if (input.mPtr && input->length > 0)
      in.assign((unsigned char *)input->GetBase(), (unsigned char *)input->GetBase() + input->length);
   size_t limit = maxOutput > 0 ? (size_t)maxOutput : 0;
   CommunicateOutput out(limit);
   CommunicateOutput err(limit);
   bool timedOut = false;
   bool haveExit = false;
   int exitCode = 0;

   CommunicateClock::time_point deadline = CommunicateClock::time_point::max();
   if (timeout > 0 && timeout < 1e9)
      deadline = CommunicateClock::now() +
                 std::chrono::duration_cast<CommunicateClock::duration>(std::chrono::duration<double>(timeout));

   #ifdef NEKO_WINDOWS
   CommunicatePipe pipes[3];
   HANDLE handles[3] = { p->iwrite, p->oread, p->eread };
   p->iwrite = p->oread = p->eread = 0;
   HANDLE proc = p->pinf.hProcess;
   std::atomic<CommunicateClock::rep> lastData(CommunicateClock::now().time_since_epoch().count());

   hx::EnterGCFreeZone();
   for(int i=0;i<3;i++)
   {
      pipes[i].handle = handles[i];
      pipes[i].output = i==1 ? &out : i==2 ? &err : 0;
      pipes[i].input = &in;
      pipes[i].done = handles[i]==0;
      pipes[i].lastData = &lastData;
      pipes[i].thread = 0;
   }
   if (handles[0])
      startPipe(pipes[0], communicateWrite);
   for(int i=1;i<3;i++)
      if (handles[i])
         startPipe(pipes[i], communicateRead);

   DWORD wait = INFINITE;
   if (deadline != CommunicateClock::time_point::max())
   {
      auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - CommunicateClock::now()).count();
      wait = left > 0 ? (DWORD)left : 0;
   }
   if (WaitForSingleObject(proc, wait) == WAIT_OBJECT_0)
   {
      // Let the readers take what the child wrote before it exited
      lastData = CommunicateClock::now().time_since_epoch().count();
      while (!(pipes[1].done && pipes[2].done))
      {
         CommunicateClock::time_point now = CommunicateClock::now();
         if (now >= deadline || now - CommunicateClock::time_point(CommunicateClock::duration(lastData.load())) >= sCommunicateQuiet)
            break;
         Sleep(5);
      }
   }
   else
   {
      TerminateProcess(proc, 1);
      WaitForSingleObject(proc, INFINITE);
      timedOut = true;
   }
   for(int i=0;i<3;i++)
      finishPipe(pipes[i]);
   for(int i=1;i<3;i++)
      if (handles[i])
         CloseHandle(handles[i]);
   if (handles[0] && !pipes[0].thread)
      CloseHandle(handles[0]);
   DWORD code = 0;
   if (GetExitCodeProcess(proc, &code))
   {
      haveExit = true;
      exitCode = (int)code;
   }
   hx::ExitGCFreeZone();

   #else
   int fdIn = p->iwrite;
   int fdOut = p->oread;
   int fdErr = p->eread;
   p->iwrite = p->oread = p->eread = -1;
   int pid = p->pid;
   bool exited = p->reaped;
   bool reapedHere = false;
   int status = 0;

   hx::EnterGCFreeZone();
   if (fdIn != -1 && in.empty())
   {
      do_close(fdIn);
      fdIn = -1;
   }
   if (fdIn != -1)
      setNonBlocking(fdIn);
   if (fdOut != -1)
      setNonBlocking(fdOut);
   if (fdErr != -1)
      setNonBlocking(fdErr);

   size_t inPos = 0;
   std::vector<unsigned char> buf(65536);
   CommunicateClock::time_point lastData = CommunicateClock::now();
   while (fdIn != -1 || fdOut != -1 || fdErr != -1)
   {
      if (!exited)
      {
         int r;
         do
            r = waitpid(pid, &status, WNOHANG);
         while (r < 0 && errno == EINTR);
         if (r == pid)
         {
            exited = reapedHere = true;
            lastData = CommunicateClock::now();
         }
      }
      // A child that has exited reads no more input
      if (exited && fdIn != -1)
      {
         do_close(fdIn);
         fdIn = -1;
      }
      CommunicateClock::time_point now = CommunicateClock::now();
      if (now >= deadline || (exited && now - lastData >= sCommunicateQuiet))
         break;

      struct pollfd fds[3];
      int n = 0;
      if (fdIn != -1)
      {
         fds[n].fd = fdIn;
         fds[n++].events = POLLOUT;
      }
      if (fdOut != -1)
      {
         fds[n].fd = fdOut;
         fds[n++].events = POLLIN;
      }
      if (fdErr != -1)
      {
         fds[n].fd = fdErr;
         fds[n++].events = POLLIN;
      }
      for(int i=0;i<n;i++)
         fds[i].revents = 0;
      // waitpid cannot be polled, so the child's exit is checked this often
      long long waitMs = 20;
      if (deadline != CommunicateClock::time_point::max())
         waitMs = std::min(waitMs, (long long)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count() + 1);
      int ready = poll(fds, n, (int)waitMs);
      if (ready < 0 && errno != EINTR)
         break;
      if (ready <= 0)
         continue;

      for(int i=0;i<n;i++)
      {
         if (!fds[i].revents)
            continue;
         int fd = fds[i].fd;
         if (fd == fdIn)
         {
            ssize_t wrote = writeNoSigpipe(fdIn, in.data() + inPos, in.size() - inPos);
            if (wrote > 0)
               inPos += wrote;
            if ((wrote < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || inPos == in.size())
            {
               do_close(fdIn);
               fdIn = -1;
            }
            continue;
         }
         CommunicateOutput &target = fd == fdOut ? out : err;
         while (true)
         {
            ssize_t got = read(fd, buf.data(), buf.size());
            if (got > 0)
            {
               target.add(buf.data(), got);
               lastData = CommunicateClock::now();
               continue;
            }
            if (got < 0 && errno == EINTR)
               continue;
            if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
               break;
            do_close(fd);
            if (fd == fdOut)
               fdOut = -1;
            else
               fdErr = -1;
            break;
         }
      }
   }
   if (fdIn != -1)
      do_close(fdIn);
   if (fdOut != -1)
      do_close(fdOut);
   if (fdErr != -1)
      do_close(fdErr);

   while (!exited)
   {
      bool forever = deadline == CommunicateClock::time_point::max();
      int r;
      do
         r = waitpid(pid, &status, forever ? 0 : WNOHANG);
      while (r < 0 && errno == EINTR);
      if (r == pid)
      {
         exited = reapedHere = true;
         break;
      }
      if (r < 0)
         break;
      if (CommunicateClock::now() >= deadline)
      {
         kill(pid, SIGKILL);
         timedOut = true;
         do
            r = waitpid(pid, &status, 0);
         while (r < 0 && errno == EINTR);
         exited = reapedHere = r == pid;
         break;
      }
      usleep(5000);
   }
   hx::ExitGCFreeZone();

   if (reapedHere)
      reapStatus(p, status);
   if (p->reaped)
   {
      haveExit = true;
      exitCode = p->exitStatus;
   }
   #endif

   hx::Anon result = hx::Anon_obj::Create();
   result->__SetField(HX_CSTRING("stdout"), communicateBytes(out.data), hx::paccDynamic);
   result->__SetField(HX_CSTRING("stderr"), communicateBytes(err.data), hx::paccDynamic);
   result->__SetField(HX_CSTRING("exitCode"), haveExit ? Dynamic(exitCode) : Dynamic(), hx::paccDynamic);
   result->__SetField(HX_CSTRING("timedOut"), timedOut, hx::paccDynamic);
   result->__SetField(HX_CSTRING("stdoutTruncated"), out.truncated, hx::paccDynamic);
   result->__SetField(HX_CSTRING("stderrTruncated"), err.truncated, hx::paccDynamic);
   return result;
}


#else  // !HX_WINRT

Dynamic _hx_std_process_run( String cmd, Array<String> vargs, int inShowParam ){ return null(); }
int _hx_std_process_stdout_read( Dynamic handle, Array<unsigned char> buf, int pos, int len ) { return 0; }
int _hx_std_process_stderr_read( Dynamic handle, Array<unsigned char> buf, int pos, int len ) { return 0; }
int _hx_std_process_stdin_write( Dynamic handle, Array<unsigned char> buf, int pos, int len ) { return 0; }
void _hx_std_process_stdin_close( Dynamic handle ) { }
#if (HXCPP_API_LEVEL > 420)
Dynamic _hx_std_process_exit( Dynamic handle, bool block ) { return 0; }
#else
int _hx_std_process_exit( Dynamic handle ) { return 0; }
#endif
int _hx_std_process_pid( Dynamic handle ) { return 0; }
void _hx_std_process_close( Dynamic handle ) { }
void _hx_std_process_kill( Dynamic handle ) { }
Dynamic _hx_std_process_communicate( Dynamic handle, Array<unsigned char> input, Float timeout, int maxOutput ) { return null(); }

#endif // HX_WINRT

