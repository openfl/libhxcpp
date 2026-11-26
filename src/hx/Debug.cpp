#include <hxcpp.h>
#include <list>
#include <map>
#include <vector>
#include <string>
#include <hx/Debug.h>
#include <hx/Thread.h>
#include <hx/Telemetry.h>
#include <hx/thread/Thread.hpp>
#include <hx/OS.h>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <thread>
#include <chrono>


#if defined(HXCPP_CATCH_SEGV) && !defined(_MSC_VER)
#include <signal.h>
#endif

// Where address space can be reserved, a script stack reserves its whole size
//  and commits memory as frames reach it. Elsewhere it is allocated outright
#ifdef HXCPP_SCRIPTABLE
#if defined(HX_WINDOWS) && !defined(HX_WINRT)
#define HX_RESERVE_SCRIPT_STACK
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#define HX_RESERVE_SCRIPT_STACK
#endif
#endif



#ifdef HX_WINRT
#define DBGLOG WINRT_LOG
#elif defined(ANDROID)
#include <android/log.h>
#define DBGLOG(...) __android_log_print(ANDROID_LOG_INFO, "HXCPP", __VA_ARGS__)
#else
#include <stdio.h>
#define DBGLOG printf
#endif

#if _MSC_VER
#ifndef snprintf
#define snprintf _snprintf
#endif
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif




namespace hx
{

const char* EXTERN_CLASS_NAME = "extern";

#ifdef HXCPP_STACK_IDS
typedef std::unordered_map<int, StackContext *> StackMap;
// Never destroyed: a thread leaves the map as it ends, and a thread can end
// while the main thread, having returned from main, runs the static
// destructors. A static map was destroyed under it, and the erase faulted;
// a static mutex, destroyed the same way, makes libc++ throw and abort.
static std::mutex &sStackMapMutex = *new std::mutex();
static StackMap &sStackMap = *new StackMap();
#endif

// User settable String->Void
//  String  : message
//  You can 'throw' to prevent default action
static Dynamic sCriticalErrorHandler;


static void setStaticHandler(Dynamic &inStore, Dynamic inValue)
{
   if ( (inStore==null()) != (inValue==null()) )
   {
      if ( inValue!=null() )
         GCAddRoot(&inStore.mPtr);
      else
         GCRemoveRoot(&inStore.mPtr);
   }
   inStore = inValue;
}



static void CriticalErrorHandler(String inErr, bool allowFixup)
{
#ifdef HXCPP_DEBUGGER
   if (allowFixup && __hxcpp_dbg_fix_critical_error(inErr))
      return;
#endif

#ifdef HXCPP_STACK_TRACE
   hx::StackContext *ctx = hx::StackContext::getCurrent();
   ctx->beginCatch(true);
#endif

   if (sCriticalErrorHandler!=null())
      sCriticalErrorHandler(inErr);

#ifdef HXCPP_STACK_TRACE
   ctx->dumpExceptionStack();
#endif

    DBGLOG("Critical Error: %s\n", inErr.utf8_str());

#if defined(HX_WINDOWS) && !defined(HX_WINRT)
    // Looked up here rather than imported, since importing user32 makes
    // every program load it at startup, which costs about a millisecond
    typedef int (WINAPI *MessageBoxFunc)(HWND, LPCSTR, LPCSTR, UINT);
    HMODULE user32 = LoadLibraryA("user32.dll");
    MessageBoxFunc messageBox = user32 ? (MessageBoxFunc)GetProcAddress(user32, "MessageBoxA") : 0;
    if (messageBox)
       messageBox(0, inErr.utf8_str(), "Critical Error - program must terminate",
           MB_ICONEXCLAMATION|MB_OK);
#endif

    // Good when using gdb, and to collect a core ...
    #if __has_builtin(__builtin_trap)
    __builtin_trap();
    #else
    (* (int *) 0) = 0;
    #endif

    // Just in case that didn't do it ...
    exit(1);
}

void CriticalError(const String &inErr, bool inAllowFixup)
{
    CriticalErrorHandler(inErr, inAllowFixup);
}


// ---- Haxe exceptions that no catch takes ------------------------------------
//
// A Haxe exception that escapes every C++ catch ends the process through
//  std::terminate: one thrown in a callback the OS or a library calls, through
//  a noexcept function or a destructor, from the HXCPP_CATCH_SEGV signal
//  handler, or anywhere else main's catch cannot reach. The C++ runtime then
//  says at most that a 'Dynamic' was thrown, and MSVC's says nothing, so this
//  handler reports it as main's catch does before the process ends.

#if defined(_MSC_VER)
#define HX_TERMINATE_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define HX_TERMINATE_NOINLINE __attribute__((noinline))
#else
#define HX_TERMINATE_NOINLINE
#endif

bool IsCollectingThread();

static void HaxeTerminate();

// MSVC's runtime keeps a terminate handler for each thread, so each thread the
//  collector knows installs this one as it attaches, and keeps the one it had
#ifdef _MSC_VER
static thread_local std::terminate_handler sPreviousTerminate = 0;
#else
static std::terminate_handler sPreviousTerminate = 0;
#endif
// Set on a thread in the handler, which ends the process if it gets there again
static thread_local bool sInTerminate = false;
// Set by the first thread to report, which ends the process for every thread
static std::atomic<bool> sReportingEscape(false);

void InstallTerminateHandler()
{
   if (std::get_terminate() != HaxeTerminate)
      sPreviousTerminate = std::set_terminate(HaxeTerminate);
}

static void PrintEscaped(const char *inLine)
{
#ifdef ANDROID
   __android_log_print(ANDROID_LOG_ERROR, "Exception", "%s", inLine);
#endif
   printf("%s\n", inLine);
}

#if defined(HX_WINDOWS) && !defined(HX_WINRT)
// A program linked for the Windows subsystem, as HX_WIN_MAIN's are, has no
//  console to print to, so its main shows a message box instead
static bool IsWindowsSubsystem()
{
   const char *base = (const char *)GetModuleHandleA(0);
   if (!base)
      return false;
   const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
   return nt->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI;
}
#endif

// Records the frames the exception has left, then those it has not: with no
//  catch to reach, it may not have unwound at all
static void LockInEscapedStack(StackContext *ctx)
{
#ifdef HXCPP_STACK_TRACE
   // Not set if the exception did not come from hx::Throw, so what the stack
   //  holds is an earlier exception's
   if (!ctx->mIsUnwindingException)
      ctx->mExceptionStack.clear();
   for (int i = ctx->mStackFrames.size() - 1; i >= 0; --i)
      ctx->mExceptionStack.push(*ctx->mStackFrames[i]);
   // Frames entered while reporting must not change it
   ctx->mIsUnwindingException = false;
   ctx->mUnwindResumeDepth = -1;
   ctx->mOuterExceptionStack.clear();
#endif
}

// Below the top of stack its caller set, so the collector sees its copy of the
//  exception
HX_TERMINATE_NOINLINE static void ReportEscapedException(const std::exception_ptr &inException, bool inThreadWasKnown)
{
   Dynamic e;
   try
   {
      std::rethrow_exception(inException);
   }
   catch (Dynamic &caught)
   {
      e = caught;
   }
   catch (...)
   {
      return;
   }

   StackContext *ctx = StackContext::getCurrent();
   if (inThreadWasKnown)
      LockInEscapedStack(ctx);

   String customStack;
   if (e != null())
   {
      try
      {
         customStack = e->__Field(HX_CSTRING("_hx_customStack"), HX_PROP_DYNAMIC).asString();
      }
      catch (...)
      {
         customStack = String();
      }
   }

   hx::strbuf buf;
   if (hx::IsNotNull(customStack))
   {
      try
      {
         PrintEscaped(customStack.utf8_str(&buf));
      }
      catch (...)
      {
      }
   }
#ifdef HXCPP_STACK_TRACE
   else if (inThreadWasKnown)
   {
      try
      {
         ctx->dumpExceptionStack();
      }
      catch (...)
      {
      }
   }
#endif

   String message;
   const char *text = "null";
   if (e != null())
   {
      try
      {
         message = e->toString();
         text = hx::IsNull(message) ? "null" : message.utf8_str(&buf);
      }
      catch (...)
      {
         text = "(a Haxe exception whose toString threw)";
      }
   }

   printf("Error : %s\n", text);
#ifdef ANDROID
   __android_log_print(ANDROID_LOG_ERROR, "Exception", "%s", text);
#endif
   PrintEscaped("The exception escaped through native code, ending the process");
   fflush(stdout);

#if defined(HX_WINDOWS) && !defined(HX_WINRT)
   if (IsWindowsSubsystem())
   {
      typedef int (WINAPI *MessageBoxFunc)(HWND, LPCSTR, LPCSTR, UINT);
      HMODULE user32 = LoadLibraryA("user32.dll");
      MessageBoxFunc messageBox = user32 ? (MessageBoxFunc)GetProcAddress(user32, "MessageBoxA") : 0;
      if (messageBox)
         messageBox(0, text, "Error", 0);
   }
#endif
}

static bool IsHaxeException(const std::exception_ptr &inException)
{
   if (!inException)
      return false;
   try
   {
      std::rethrow_exception(inException);
   }
   catch (Dynamic &)
   {
      return true;
   }
   catch (...)
   {
   }
   return false;
}

static void HaxeTerminate()
{
   // Terminate again on this thread, from inside the report or a handler it
   //  called: going round again would never end
   if (sInTerminate)
      abort();
   sInTerminate = true;

   std::exception_ptr exception = std::current_exception();
   if (IsHaxeException(exception))
   {
      // A collection running on this thread, as in a finalizer, cannot
      //  allocate, nor wait for another thread that does
      bool collecting = IsCollectingThread();

      if (sReportingEscape.exchange(true))
      {
         // Another thread is reporting and will end the process. It may need a
         //  collection meanwhile, so this thread keeps out of its way
         if (collecting)
            abort();
         if (StackContext::getCurrent())
            TryGCFreeZone();
         for (;;)
            std::this_thread::sleep_for(std::chrono::seconds(1));
      }

      if (collecting)
      {
         printf("Error : (a Haxe exception, which cannot be read during a collection)\n");
         PrintEscaped("The exception escaped through native code, ending the process");
         fflush(stdout);
      }
      else
      {
         // Reporting runs Haxe code, which needs a thread the collector knows,
         //  outside any GC-free zone. On a thread it did not know, the stack
         //  of frames is not the exception's
         bool known = StackContext::getCurrent() != 0;
         int topOfStack = 0;
         SetTopOfStack(&topOfStack, true);
         ReportEscapedException(exception, known);
      }
   }

   std::terminate_handler previous = sPreviousTerminate;
   if (previous && previous != HaxeTerminate)
      previous();
   abort();
}

#ifdef HXCPP_CATCH_SEGV
class hxSehException : public hx::Object
{
public:
   int code;

   inline void *operator new( size_t inSize )
   {
      return hx::InternalCreateConstBuffer(0,(int)inSize);
   }
   void operator delete( void *) { }

   hxSehException(int inCode) : code(inCode) { }

   String __ToString() const HXCPP_OVERRIDE { return  HX_CSTRING("hxSehException"); }

   int __GetType() const HXCPP_OVERRIDE { return vtObject; }
};

static hx::Object *sException = new hxSehException(1);

#ifdef _MSC_VER
void __cdecl hxSignalFunction(unsigned int, struct _EXCEPTION_POINTERS* )
{
   hx::Throw(sException);
}
#else
void hxSignalFunction(int)
{
   hx::Throw(sException);
}
#endif

#endif


String FormatStack(const char *file, const char *clazz, const char *func, int line, bool display)
{
   // Not sure if the following is even possible but the old debugger did it so ...
   char buf[1024];
   if (!file || file[0]=='?')
   {
       if (display)
          snprintf(buf, sizeof(buf), "%s::%s", clazz, func);
       else
          snprintf(buf, sizeof(buf), "%s::%s::%d", clazz, func,line);
   }
   else
   {
      // Old-style combined file::class...
      if (!clazz || !clazz[0])
      {
          if (line>0 || !display)
             snprintf(buf, sizeof(buf), display ? "%s %s line %d" : "%s::%s::%d", func, file, line);
          else
             snprintf(buf, sizeof(buf), "%s %s", func, file);
      }
      else
      {
          if (line>0 || !display)
             snprintf(buf, sizeof(buf), display ? "%s::%s %s line %d" : "%s::%s::%s::%d", clazz, func, file, line);
          else
             snprintf(buf, sizeof(buf), "%s::%s %s", clazz, func, file);
      }
   }
   return ::String(buf);
}






StackContext::StackContext()
{
   #ifdef HXCPP_STACK_TRACE
   mIsUnwindingException = false;
   mUnwindResumeDepth = -1;
   #endif

   #if HXCPP_TELEMETRY
   mTelemetry = tlmCreate(this);
   #endif

   #ifdef HXCPP_DEBUGGER
   mDebugger = 0;
   #endif

   #ifdef HXCPP_PROFILER
   mProfiler = 0;
   #endif

   #ifdef HXCPP_SCRIPTABLE
   // The jit's write barrier compares an object's mark with this, so it must
   //  match from the start: left at 0 until the first collection, it took
   //  every nursery object for an old one
   byteMarkId = ::hx::gByteMarkID;
   allocScriptStack();
   exception = 0;
   breakContReturn = 0;
   #endif

   #ifdef HXCPP_COMBINE_STRINGS
   stringSet = 0;
   #endif
}

StackContext::~StackContext()
{
   #ifdef HXCPP_DEBUGGER
   if (mDebugger)
      dbgCtxDestroy(mDebugger);
   #endif

   #ifdef HXCPP_TELEMETRY
   if (mTelemetry)
      tlmDestroy(mTelemetry);
   #endif

   #ifdef HXCPP_PROFILER
   if (mProfiler)
      profDestroy(mProfiler);
   #endif

   #ifdef HXCPP_SCRIPTABLE
   freeScriptStack();
   #endif
}

#ifdef HXCPP_SCRIPTABLE

#ifndef HXCPP_CPPIA_STACK_SIZE
#define HXCPP_CPPIA_STACK_SIZE (128*1024)
#endif

// Argument pushes precede the callee's frame check, so stackEnd stays this far
//  below the committed end
static const size_t sScriptStackMargin = 16*1024;
// Sizes and commits are whole steps, which are whole pages for 4K, 16K and 64K pages
static const size_t sScriptStackStep = 64*1024;
static const size_t sScriptStackMax = 0x7fff0000;
// Committed when a stack is made, which is all of a stack of the default size
static const size_t sScriptStackFirstCommit = 128*1024;

// The size new script stacks get
static std::atomic<int> sScriptStackSize( HXCPP_CPPIA_STACK_SIZE );

static size_t scriptStackSize(int inBytes)
{
   size_t size = inBytes < (int)sScriptStackStep ? sScriptStackStep : (size_t)inBytes;
   if (size > sScriptStackMax)
      size = sScriptStackMax;
   return (size + sScriptStackStep - 1) & ~(sScriptStackStep - 1);
}

#ifdef HX_RESERVE_SCRIPT_STACK
static unsigned char *reserveScriptStack(size_t inSize)
{
   #ifdef HX_WINDOWS
   return (unsigned char *)VirtualAlloc(0, inSize, MEM_RESERVE, PAGE_NOACCESS);
   #else
   void *result = mmap(0, inSize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
   return result==MAP_FAILED ? 0 : (unsigned char *)result;
   #endif
}

static bool commitScriptStack(unsigned char *inStart, size_t inSize)
{
   #ifdef HX_WINDOWS
   return VirtualAlloc(inStart, inSize, MEM_COMMIT, PAGE_READWRITE)!=0;
   #else
   return mprotect(inStart, inSize, PROT_READ | PROT_WRITE)==0;
   #endif
}

static void releaseScriptStack(unsigned char *inStack, size_t inSize)
{
   #ifdef HX_WINDOWS
   VirtualFree(inStack, 0, MEM_RELEASE);
   #else
   munmap(inStack, inSize);
   #endif
}
#endif

void StackContext::allocScriptStack()
{
   stackSize = scriptStackSize(sScriptStackSize.load(std::memory_order_relaxed));
   stack = 0;
   #ifdef HX_RESERVE_SCRIPT_STACK
   stack = reserveScriptStack(stackSize);
   stackCommitted = std::min(stackSize, sScriptStackFirstCommit);
   if (stack && !commitScriptStack(stack, stackCommitted))
   {
      releaseScriptStack(stack, stackSize);
      stack = 0;
   }
   #endif
   stackReserved = stack!=0;
   if (!stack)
   {
      stack = new unsigned char[stackSize];
      stackCommitted = stackSize;
   }
   stackEnd = stack + stackCommitted - sScriptStackMargin;
   pointer = stack;
   push((hx::Object *)0);
   frame = pointer;
}

void StackContext::freeScriptStack()
{
   #ifdef HX_RESERVE_SCRIPT_STACK
   if (stackReserved)
      releaseScriptStack(stack, stackSize);
   else
   #endif
      delete [] stack;
   stack = pointer = frame = stackEnd = 0;
}

bool StackContext::growScriptStack(int inBytes)
{
   size_t needed = (size_t)(pointer - stack) + inBytes + sScriptStackMargin;
   if (needed > stackSize)
      return false;
   #ifdef HX_RESERVE_SCRIPT_STACK
   if (needed > stackCommitted)
   {
      // Doubling keeps deep recursion to a few commits
      size_t commit = std::max(needed, stackCommitted*2);
      commit = std::min(stackSize, (commit + sScriptStackStep - 1) & ~(sScriptStackStep - 1));
      if (!commitScriptStack(stack + stackCommitted, commit - stackCommitted))
         return false;
      stackCommitted = commit;
      stackEnd = stack + stackCommitted - sScriptStackMargin;
   }
   #endif
   return true;
}

void StackContext::resizeScriptStack()
{
   if (stackSize==scriptStackSize(sScriptStackSize.load(std::memory_order_relaxed)))
      return;
   // Only the bottom slot is in use when no script frame is
   if (frame!=stack + sizeof(hx::Object *) || pointer!=frame)
      return;
   freeScriptStack();
   allocScriptStack();
}

#endif

void StackContext::onThreadAttach()
{
   #ifdef HXCPP_STACK_IDS
    mThreadId = hx::thread::Thread_obj::id();

   {
       std::lock_guard<std::mutex> guard(sStackMapMutex);
       sStackMap[mThreadId] = this;
   }
   #endif

   #ifdef HXCPP_DEBUGGER
   if (!mDebugger)
      mDebugger = dbgCtxCreate(this);
   if (mDebugger)
      dbgCtxAttach(mDebugger,this);
   #endif

   #ifdef HXCPP_TELEMETRY
   if (mTelemetry)
      tlmAttach(mTelemetry,this);
   #endif

   #ifdef HXCPP_PROFILER
   if (mProfiler)
      profAttach(mProfiler,this);
   #endif

   #ifdef HXCPP_CATCH_SEGV
      #ifdef _MSC_VER
      mOldSignalFunc = _set_se_translator( hxSignalFunction );
      #else
      mOldSignalFunc = signal( SIGSEGV, hxSignalFunction );
      #endif
   #endif

   #ifdef _MSC_VER
   // MSVC's runtime keeps a terminate handler for each thread, so the one
   //  hx::Boot installs covers only its own
   InstallTerminateHandler();
   #endif

   #ifdef HXCPP_SCRIPTABLE
   // A pooled context keeps the stack an earlier thread had
   resizeScriptStack();
   #endif
}

void StackContext::onThreadDetach()
{
   #ifdef HXCPP_CATCH_SEGV
      #ifdef _MSC_VER
      _set_se_translator( mOldSignalFunc );
      #else
      signal( SIGSEGV, mOldSignalFunc );
      #endif
   #endif

   #ifdef HXCPP_DEBUGGER
   if (mDebugger)
      dbgCtxDetach(mDebugger);
   #endif

   #ifdef HXCPP_TELEMETRY
   if (mTelemetry)
      tlmDetach(mTelemetry);
   #endif

   #ifdef HXCPP_PROFILER
   if (mProfiler)
      profDetach(mProfiler,this);
   #endif

   #ifdef HXCPP_STACK_IDS
   {
       std::lock_guard<std::mutex> guard(sStackMapMutex);
       sStackMap.erase(mThreadId);
   }
   mThreadId = 0;
   #endif

   #ifdef HXCPP_COMBINE_STRINGS
   stringSet = 0;
   #endif
}

#ifdef HXCPP_STACK_IDS
void StackContext::getAllStackIds( QuickVec<int> &outIds )
{
   outIds.clear();

   std::lock_guard<std::mutex> guard(sStackMapMutex);

   for(StackMap::iterator i=sStackMap.begin(); i!=sStackMap.end(); ++i)
      outIds.push(i->first);
}

StackContext *StackContext::getStackForId(int id)
{
   std::lock_guard<std::mutex> guard(sStackMapMutex);
   return sStackMap[id];
}
#endif


#ifdef HXCPP_STACK_TRACE // {

void StackContext::tracePosition( )
{
   StackFrame *frame = mStackFrames[mStackFrames.size()-1];
   #ifdef HXCPP_STACK_LINE
   DBGLOG("%s::%s : %d\n", frame->position->className, frame->position->functionName, frame->lineNumber);
   #else
   DBGLOG("%s::%s\n", frame->position->className, frame->position->functionName);
   #endif
}

void StackContext::getCurrentCallStackAsStrings(Array<String> result, bool skipLast)
{
   int n = mStackFrames.size() - (skipLast ? 1 : 0);

   for (int i = 0; i < n; i++)
   {
       // Reverse call stack to match exception stack
       StackFrame *frame = mStackFrames[n-1-i];
       result->push(frame->toString());
   }
}

void StackContext::getCurrentExceptionStackAsStrings(Array<String> result)
{
   int size = mExceptionStack.size();

   for (int i = 0; i < size; i++)
   {
      result->push(mExceptionStack[i].toString());
   }
}

void StackContext::beginCatch(bool inAll)
{
   if (inAll)
   {
      mExceptionStack.clear();
      // Copy remaineder of stack frames to exception stack...
      // This will use the default operator=, which will copy the pointers.
      // This is what we want, since the pointers are pointers to constant data
      for(int i=mStackFrames.size()-1;i>=0;--i)
         mExceptionStack.push( *mStackFrames[i] );
   }
   // Lock-in the excpetion stack
   mIsUnwindingException = false;
}



// Called when a throw occurs
void StackContext::setLastException()
{
   mExceptionStack.clear();
   mIsUnwindingException = true;
}


// Called when a throw occurs
void StackContext::pushLastException()
{
   mIsUnwindingException = true;
}



void StackContext::dumpExceptionStack()
{
   #ifdef ANDROID
   #define EXCEPTION_PRINT(...) \
        __android_log_print(ANDROID_LOG_ERROR, "HXCPP", __VA_ARGS__)
   #else
   #define EXCEPTION_PRINT(...) \
           printf(__VA_ARGS__)
   #endif

   int size = mExceptionStack.size();
   for(int i = size - 1; i >= 0; i--)
   {
      EXCEPTION_PRINT("Called from %s\n", mExceptionStack[i].toDisplay().utf8_str());
   }
}


// ---- StackFrame -------


::String StackFrame::toDisplay()
{
   #ifndef HXCPP_STACK_LINE
   int lineNumber=0;
   #endif
   return FormatStack(position->fileName, position->className, position->functionName, lineNumber,true);
}


::String StackFrame::toString()
{
   #ifndef HXCPP_STACK_LINE
   int lineNumber=0;
   #endif

   return FormatStack(position->fileName, position->className, position->functionName, lineNumber,false);
}




ExceptionStackFrame::ExceptionStackFrame(const StackFrame &inFrame)
{
   // It is safe to use the pointer in 331+
   position = inFrame.position;

   #ifdef HXCPP_STACK_LINE
   line = inFrame.lineNumber;
   #endif
}

::String ExceptionStackFrame::format(bool inForDisplay)
{
   #ifndef HXCPP_STACK_LINE
   int line=0;
   #endif

   const char *fileName = position->fileName;
   const char *className = position->className;
   const char *functionName = position->functionName;

   return FormatStack(fileName, className, functionName, line, inForDisplay);
}

::String ExceptionStackFrame::toDisplay() { return format(true); }
::String ExceptionStackFrame::toString() { return format(false); }


#endif // } HXCPP_STACK_TRACE


HXCPP_EXTERN_CLASS_ATTRIBUTES void NullReference(const char *type, bool allowFixup)
{
#ifdef HXCPP_DEBUGGER
   String err(HX_CSTRING("Null ") + String(type) + HX_CSTRING(" Reference"));
   if (__hxcpp_dbg_fix_critical_error(err))
      return;
#endif

   __hxcpp_dbg_checkedThrow(HX_CSTRING("Null Object Reference"));
}


} // end namspace hx

#ifndef HXCPP_DEBUGGER
void __hxcpp_execution_trace(int inLevel) { }
#endif

#ifdef HXCPP_SCRIPTABLE
void __scriptable_cppia_set_stack_size(int inBytes)
{
   hx::sScriptStackSize.store(inBytes, std::memory_order_relaxed);
   hx::StackContext *ctx = hx::StackContext::getCurrent();
   if (ctx)
      ctx->resizeScriptStack();
}

int __scriptable_cppia_stack_size()
{
   hx::StackContext *ctx = hx::StackContext::getCurrent();
   return ctx ? (int)ctx->stackSize : 0;
}
#endif


void __hx_dump_stack()
{
#ifdef HXCPP_STACK_TRACE
   hx::StackContext *ctx = hx::StackContext::getCurrent();
   ctx->beginCatch(false);
   ctx->dumpExceptionStack();
#endif
}


void __hx_stack_set_last_exception()
{
#ifdef HXCPP_STACK_TRACE
   hx::StackContext *ctx = hx::StackContext::getCurrent();
   ctx->setLastException();
#endif
}


void __hx_stack_push_last_exception()
{
#ifdef HXCPP_STACK_TRACE
   hx::StackContext *ctx = hx::StackContext::getCurrent();
   ctx->pushLastException();
#endif
}


void __hxcpp_stack_begin_catch()
{
#ifdef HXCPP_STACK_TRACE
   hx::StackContext *ctx = hx::StackContext::getCurrent();
   ctx->beginCatch(false);
#endif
}


Array<String> __hxcpp_get_call_stack(bool inSkipLast)
{
    Array<String> result = Array_obj<String>::__new();

#ifdef HXCPP_STACK_TRACE
   hx::StackContext *ctx = hx::StackContext::getCurrent();
   ctx->getCurrentCallStackAsStrings(result,inSkipLast);
#endif
   return result;
}


Array<String> __hxcpp_get_exception_stack()
{
    Array<String> result = Array_obj<String>::__new();

#ifdef HXCPP_STACK_TRACE
   hx::StackContext *ctx = hx::StackContext::getCurrent();
   ctx->getCurrentExceptionStackAsStrings(result);
#endif

    return result;
}

void __hxcpp_set_critical_error_handler(Dynamic inHandler)
{
   hx::setStaticHandler(hx::sCriticalErrorHandler,inHandler);
}

#ifndef HXCPP_DEBUGGER
void __hxcpp_set_debugger_info(const char **inAllClasses, const char **inFullPaths) { }
#endif


#ifndef HXCPP_PROFILER
void __hxcpp_start_profiler(::String inDumpFile)
{
   DBGLOG("Warning - profiler has no effect without HXCPP_PROFILER\n");
}
void __hxcpp_stop_profiler() { }
#endif


