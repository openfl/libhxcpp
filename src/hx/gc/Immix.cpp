#include <hxcpp.h>

#include <hx/GC.h>
#include <hx/Memory.h>
#include <hx/Thread.h>
#include <hx/thread/Thread.hpp>
#include "../Hash.h"
#include "../FastMutex.h"
#include "GcRegCapture.h"
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <inttypes.h>
#include <limits>
#include <exception>

#ifdef HXCPP_GC_INCREMENTAL
#include <chrono>
#endif

#ifdef __EMSCRIPTEN__
   #include <emscripten/stack.h>
   #ifdef HXCPP_SINGLE_THREADED_APP
      // Use provided tools to measure stack extent
      #define HXCPP_EXPLICIT_STACK_EXTENT
   #endif
#endif

#include <string>
#include <stdlib.h>

#if defined(__SANITIZE_ADDRESS__)
   #define HX_ADDRESS_SANITIZER
#elif defined(__has_feature)
   #if __has_feature(address_sanitizer)
      #define HX_ADDRESS_SANITIZER
   #endif
#endif

#ifdef HX_ADDRESS_SANITIZER
// The collector finds live objects by scanning thread stacks, and ASan's
// use-after-return check moves locals into frames off the stack, where the
// scan cannot see them; their objects would then be freed while in use.
// ASan reads its defaults from here, and ASAN_OPTIONS still overrides them.
#ifdef _MSC_VER
extern "C" const char *__asan_default_options()
#else
extern "C" __attribute__((visibility("default"), used)) const char *__asan_default_options()
#endif
{
   return "detect_stack_use_after_return=0";
}
#endif

namespace
{
    // It took until C++26 for them to add saturating arithmetic!
    // So lets have some very basic saturating maths helpers.
    // Funky branchless saturating from https://web.archive.org/web/20190213215419/https://locklessinc.com/articles/sat_arithmetic/

    template<class T>
    T saturating_add(T x, T delta)
    {
        T res = x + delta;
        res |= -(res < x);

        return res;
    }

    template<class T>
    T saturating_sub(T x, T delta)
    {
        T res = x - delta;
        res &= -(res <= x);

        return res;
    }
}

static bool sgIsCollecting = false;

namespace hx
{
   int gByteMarkID = 0x10;
   int gRememberedByteMarkID = 0x10 | HX_GC_REMEMBERED;


int gFastPath = 0;
int gSlowPath = 0;


}

using hx::gByteMarkID;
using hx::gRememberedByteMarkID;


namespace hx
{
// The GC's own containers and locks are allocated once and never destroyed:
//  exit() runs static destructors while other threads may still allocate
#ifdef HXCPP_GC_DEBUG_ALWAYS_MOVE

enum { gAlwaysMove = true };
typedef std::unordered_set<void *> PointerMovedSet;
PointerMovedSet &sgPointerMoved = *new PointerMovedSet();

#else
enum { gAlwaysMove = false };
#endif
}

#ifdef ANDROID
#include <android/log.h>
#endif

#ifdef HX_WINDOWS
#include <windows.h>
#endif

#include <vector>
#include <utility>
#include <stdio.h>

#include <hx/QuickVec.h>

// #define HXCPP_GC_BIG_BLOCKS


#ifndef __has_builtin
#define __has_builtin(x) 0
#endif
namespace {
void DebuggerTrap()
{
   static bool triggeredOnce = false;

   // The report printed before the trap would otherwise die in the buffer
   fflush(stdout);
   fflush(stderr);

   if (!triggeredOnce)
   {
      triggeredOnce = true;

      #if __has_builtin(__builtin_trap)
      __builtin_trap();
      #else
      *(int *)0=0;
      #endif
   }
}
}




static bool sgAllocInit = 0;
static bool sgInternalEnable = true;
// How far into an object a stack value may point and still keep it alive. With
// virtual inheritance a pointer can point into the middle of an object, and
// compilers keep the address of a member instead of the object: clang, keeping
// only &array->length across a call, left nothing else on the stack that
// reached the array (libhxcpp #269). That covers the fields Array and String
// keep, and the first ones of most classes
static uintptr_t sgCheckInternalOffset = sizeof(void *)*8;

int gInAlloc = false;

// This is recalculated from the other parameters
static size_t sWorkingMemorySize          = 10*1024*1024;

#ifdef HXCPP_GC_MOVING
// Just not sure what this shold be
static size_t sgMaximumFreeSpace  = 1024*1024*1024;
#else
static size_t sgMaximumFreeSpace  = 1024*1024*1024;
#endif


// #define HXCPP_GC_DEBUG_LEVEL 1

#if HXCPP_GC_DEBUG_LEVEL>1
  #define PROFILE_COLLECT
  #if HXCPP_GC_DEBUG_LEVEL>2
     #define SHOW_FRAGMENTATION
     #if HXCPP_GC_DEBUG_LEVEL>3
        #define SHOW_MEM_EVENTS
     #endif
  #endif
#endif

// #define SHOW_FRAGMENTATION_BLOCKS
#if defined SHOW_FRAGMENTATION_BLOCKS
  #define SHOW_FRAGMENTATION
#endif

#define RECYCLE_LARGE

// HXCPP_GC_DYNAMIC_SIZE

//#define HXCPP_GC_SUMMARY
//#define PROFILE_COLLECT
//#define PROFILE_THREAD_USAGE
//#define HXCPP_GC_VERIFY
//#define HX_GC_VERIFY_ALLOC_START
//#define SHOW_MEM_EVENTS
//#define SHOW_MEM_EVENTS_VERBOSE
//#define SHOW_FRAGMENTATION
//
// Setting this can make it easier to reproduce this problem since it takes
//  the timing of the zeroing out of the equation
//#define HX_GC_ZERO_EARLY

// Setting this on windows64 will mean this objects are allocated in the same place,
//  which can make native debugging easier
//#define HX_GC_FIXED_BLOCKS
//
// If the problem happens on the same object, you can print info about the object
//  every collect so you can see where it goes wrong (usually requires HX_GC_FIXED_BLOCKS)
//#define HX_WATCH

#if defined(HXCPP_GC_VERIFY) && defined(HXCPP_GC_GENERATIONAL_RUNTIME)
#define HX_GC_VERIFY_GENERATIONAL
#endif


#ifdef HX_WATCH
#ifndef HX_WATCH_ADDRESS
#define HX_WATCH_ADDRESS 0x0000000100000200
#endif
uintptr_t hxWatchList[] = {
  uintptr_t{ HX_WATCH_ADDRESS },
  uintptr_t{ 0 }
};
bool hxInWatchList(uintptr_t watch)
{
   for(uintptr_t* t = hxWatchList; *t; t++)
      if (*t==watch)
         return true;
   return false;
}
#endif

#ifdef HX_GC_FIXED_BLOCKS
#ifdef HX_WINDOWS
#include <Memoryapi.h>
#endif
#endif

#ifdef PROFILE_COLLECT
   #define HXCPP_GC_SUMMARY
#endif

#ifdef HX_GC_VERIFY_GENERATIONAL
static bool sGcVerifyGenerational = false;

#if defined(__GXX_RTTI) || defined(_CPPRTTI)
#include <typeinfo>
#define HX_GC_VERIFY_TYPE(obj) ((obj) ? typeid(*(obj)).name() : "a root")
#else
#define HX_GC_VERIFY_TYPE(obj) ((obj) ? "an object" : "a root")
#endif
#endif


#if HX_HAS_ATOMIC && (HXCPP_GC_DEBUG_LEVEL==0) && !defined(HXCPP_GC_VERIFY) && !defined(__EMSCRIPTEN__)
  #if defined(HX_MACOS) || defined(HX_WINDOWS) || defined(HX_LINUX)
  enum { MAX_GC_THREADS = 4 };
  #else
  enum { MAX_GC_THREADS = 2 };
  #endif
  
  // You can uncomment this for better call stacks if it crashes while collecting
  #define HX_MULTI_THREAD_MARKING
#else
  enum { MAX_GC_THREADS = 1 };
#endif

#ifdef PROFILE_THREAD_USAGE
static int sThreadMarkCountData[MAX_GC_THREADS+1];
static int sThreadArrayMarkCountData[MAX_GC_THREADS+1];
static int *sThreadMarkCount = sThreadMarkCountData + 1;
static int *sThreadArrayMarkCount = sThreadArrayMarkCountData + 1;
static std::atomic_int sThreadChunkPushCount;
static int sThreadChunkWakes;
static std::atomic_int sSpinCount = 0;
static int sThreadZeroWaits = 0;
static int sThreadZeroPokes = 0;
static int sThreadBlockZeroCount = 0;
static std::atomic_int sThreadZeroMisses{};
#endif

enum { MARK_BYTE_MASK = 0x0f };
enum { FULL_MARK_BYTE_MASK = 0x3f };


enum
{
   MEM_INFO_USAGE = 0,
   MEM_INFO_RESERVED = 1,
   MEM_INFO_CURRENT = 2,
   MEM_INFO_LARGE = 3,
   // The bytes of the objects that survived the last full reclaim, which
   // every major collection does, against MEM_INFO_USAGE's whole lines
   MEM_INFO_LIVE = 4,
};

enum GcMode
{
   gcmFull,
   gcmGenerational,
};

// Start with full gc - since all objects will be new
static GcMode sGcMode = gcmFull;



#ifndef HXCPP_GC_MOVING
  #ifdef HXCPP_GC_DEBUG_ALWAYS_MOVE
  #define HXCPP_GC_MOVING
  #endif
// Enable moving collector...
//#define HXCPP_GC_MOVING
#endif

// Allocate this many blocks at a time - this will increase memory usage % when rounding to block size must be done.
// However, a bigger number makes it harder to release blocks due to pinning
#define IMMIX_BLOCK_GROUP_BITS  5


struct CensusEntry
{
   size_t count;
   size_t bytes;
};
// One __hxcpp_gc_census call, which the next full mark fills
struct CensusRequest
{
   std::unordered_map<hx::Class_obj *,CensusEntry> counts;
   // Stack values the census could not tell from raw memory, with whether
   // each is a large allocation; they are marked after the precise marking
   std::vector< std::pair<void *,bool> > deferred;
   bool done = false;
};
// Census calls take turns, and the one whose turn it is waits here for a full
//  mark to fill it. A competing collection can run first, so the mark takes
//  the request rather than the call that asked
static std::mutex *sgCensusLock = new std::mutex();
static std::atomic<CensusRequest *> sgCensusRequest(nullptr);
// Set while a mark fills a census request; marking is then single-threaded
static bool sgCensus = false;
static std::unordered_map<hx::Class_obj *,CensusEntry> *sgCensusCounts = 0;
static std::vector< std::pair<void *,bool> > *sgCensusDeferred = 0;
namespace hx
{
// When set, called with each object a census counts: cppia attributes them to
// the released modules they keep loaded
void (*gCensusVisitor)(hx::Object *inObj, size_t inBytes) = 0;
}

// While the weak hash values are marked, the entries whose object key is not
//  marked yet, by key, and those whose key the marking has reached since.
//  Marking is single-threaded then
struct EphemeronEntry
{
   hx::HashRoot *hash;
   void         *element;
};
typedef std::unordered_multimap<hx::Object *,EphemeronEntry> EphemeronPending;
static EphemeronPending *sgEphemeronPending = 0;
static std::vector<EphemeronEntry> *sgEphemeronReady = 0;

#ifdef HXCPP_DEBUG
static hx::Object *gCollectTrace = 0;
static bool gCollectTraceDoPrint = false;
static int gCollectTraceCount = 0;
static int sgSpamCollects = 0;
#endif

#if defined(HXCPP_DEBUG) || defined(HXCPP_GC_DEBUG_ALWAYS_MOVE)
static std::atomic_int sgAllocsSinceLastSpam{};
#endif

#ifdef ANDROID
#define GCLOG(...) __android_log_print(ANDROID_LOG_INFO, "gclog", __VA_ARGS__)
#else
#define GCLOG printf
#endif

#ifdef PROFILE_COLLECT
   #define STAMP(t) double t = __hxcpp_time_stamp();
   #define MEM_STAMP(t) t = __hxcpp_time_stamp();
   static double sLastCollect = __hxcpp_time_stamp();
   static int sObjectMarks =0;
   static int sAllocMarks =0;

#else
   #define STAMP(t)
   #define MEM_STAMP(t)
#endif

#if defined(HXCPP_GC_SUMMARY) || defined(HXCPP_GC_DYNAMIC_SIZE)
struct ProfileCollectSummary
{
   enum { COUNT = 10 };
   double timeWindow[COUNT];
   int    windowIdx;
   double startTime;
   double lastTime;
   double totalCollecting;
   double maxStall;
   double spaceFactor;

   ProfileCollectSummary()
   {
      startTime = __hxcpp_time_stamp();
      lastTime = startTime;
      totalCollecting = 0;
      maxStall = 0;
      windowIdx = 0;
      spaceFactor = 1.0;
      for(int i=0;i<COUNT;i++)
         timeWindow[i] = 0.1;
   }
   ~ProfileCollectSummary()
   {
      #ifdef HXCPP_GC_SUMMARY
      double time = __hxcpp_time_stamp() - startTime;
      GCLOG("Total time     : %.2fms\n", time*1000.0);
      GCLOG("Collecting time: %.2fms\n", totalCollecting*1000.0);
      GCLOG("Max Stall time : %.2fms\n", maxStall*1000.0);
      if (time==0) time = 1;
      GCLOG(" Fraction      : %.2f%%\n",totalCollecting*100.0/time);

      #ifdef HXCPP_GC_DYNAMIC_SIZE
      GCLOG("Space factor   : %.2fx\n",spaceFactor);
      #endif
      #endif
   }

   void addTime(double inCollectStart)
   {
      double now = __hxcpp_time_stamp();
      double dt =  now-inCollectStart;
      if (dt>maxStall)
         maxStall = dt;
      totalCollecting += dt;
      #ifdef HXCPP_GC_DYNAMIC_SIZE
      double wholeTime = now - lastTime;
      if (wholeTime)
      {
         lastTime = now;
         double ratio = dt/wholeTime;
         windowIdx = (windowIdx +1)%COUNT;
         timeWindow[windowIdx] = ratio;
         double sum = 0;
         for(int i=0;i<COUNT;i++)
            sum += timeWindow[i];
         ratio = sum/COUNT;

         if (ratio<0.05)
         {
            if (spaceFactor>1.0)
            {
               spaceFactor -= 0.2;
               if (spaceFactor<1.0)
                  spaceFactor = 1.0;
            }
         }
         else if (ratio>0.12)
         {
            if (ratio>0.2)
               spaceFactor += 0.5;
            else
               spaceFactor += 0.2;

            if (spaceFactor>5.0)
               spaceFactor = 5.0;
         }
      }
      #endif
   }

};

static ProfileCollectSummary profileCollectSummary;
#define PROFILE_COLLECT_SUMMARY_START double collectT0 = __hxcpp_time_stamp();
#define PROFILE_COLLECT_SUMMARY_END profileCollectSummary.addTime(collectT0);

#else
#define PROFILE_COLLECT_SUMMARY_START 
#define PROFILE_COLLECT_SUMMARY_END
#endif



#ifdef HXCPP_TELEMETRY
extern void __hxt_gc_realloc(void* old_obj, void* new_obj, int new_size);
extern void __hxt_gc_start();
extern void __hxt_gc_end();
extern void __hxt_gc_after_mark(int gByteMarkID, int markIdByte);
#endif

static int sgTimeToNextTableUpdate = 1;




std::recursive_mutex *gThreadStateChangeLock=nullptr;
hx::FastMutex *gSpecialObjectLock=nullptr;

class LocalAllocator;
enum LocalAllocState { lasNew, lasRunning, lasStopped, lasWaiting, lasTerminal };


static void MarkLocalAlloc(LocalAllocator *inAlloc,hx::MarkContext *__inCtx);
#ifdef HXCPP_VISIT_ALLOCS
static void VisitLocalAlloc(LocalAllocator *inAlloc,hx::VisitContext *__inCtx);
#endif
static void WaitForSafe(LocalAllocator *inAlloc);
static void WaitForSafeUntil(LocalAllocator *inAlloc, double inSeconds);
static void ReleaseFromSafe(LocalAllocator *inAlloc);
// Set by ParkThreadsForExit once the process has started to exit, after which
//  only the exiting thread runs Haxe code
static std::atomic<bool> sgParkedForExit{false};
static LocalAllocator *sgExitAlloc = 0;
static void ClearPooledAlloc(LocalAllocator *inAlloc);
#ifdef HXCPP_GC_INCREMENTAL
static void RestoreIncrementalAlloc(LocalAllocator *inAlloc);
#endif
static void CollectFromThisThread(bool inMajor,bool inForceCompact);
// What a collection that a thread starts is, in one argument (see
//  LocalAllocator::SetupStackAndCollectFlags)
enum { collectMajor = 1, collectCompact = 2, collectLocked = 4, collectFreeIsFragged = 8 };
static void CollectFromAlloc(hx::ImmixAllocator *inAlloc, int inFlags);

// Non-zero while Gc.compact() is collecting. Only then are all empty groups given
// back to the system. Gc.run(true) asks for the same major+compact collection, but
// callers use it to collect, not to shrink: releasing there meant the next work had
// to commit and fault the whole heap back in (#231, #233). Stock hxcpp releases
// nothing on either call in the non-moving build.
static std::atomic_int sReleaseAllRequests{0};

namespace hx
{
std::atomic_uint gPauseForCollect{ 0x00000000 };

void EphemeronWait(hx::Object *inKey, HashRoot *inHash, void *inElement)
{
   if (sgEphemeronPending)
      sgEphemeronPending->insert(std::make_pair(inKey, EphemeronEntry{ inHash, inElement }));
}

StackContext *gMainThreadContext = 0;

unsigned int gImmixStartFlag[128];

unsigned int gMarkID = 0x10 << 24;
unsigned int gMarkIDWithContainer = (0x10 << 24) | IMMIX_ALLOC_IS_CONTAINER;

int gPrevByteMarkID = 0x2f;
unsigned int gPrevMarkIdMask = ((~0x2f000000) & 0x30000000) | HX_GC_CONST_ALLOC_BIT;




void ExitGCFreeZoneLocked();


DECLARE_FAST_TLS_DATA(StackContext, tlsStackContext);

#ifdef HXCPP_SCRIPTABLE
extern void scriptMarkStack(hx::MarkContext *);
extern void scriptMarkModules(hx::MarkContext *, bool inGenerational);
extern void scriptFreeModules();
#ifdef HXCPP_VISIT_ALLOCS
extern void scriptVisitModules(hx::VisitContext *);
#endif
#endif
}

//#define DEBUG_ALLOC_PTR ((char *)0xb68354)


#ifdef HX_WINDOWS
#define ZERO_MEM(ptr, n) ZeroMemory(ptr,n)
#else
#define ZERO_MEM(ptr, n) memset(ptr,0,n)
#endif


// ---  Internal GC - IMMIX Implementation ------------------------------



// Some inline implementations ...
// Use macros to allow for mark/move



/*
  IMMIX block size, and various masks for converting addresses

*/

#ifdef HXCPP_GC_BIG_BLOCKS
   #define IMMIX_BLOCK_BITS      16
   typedef unsigned int BlockIdType;
#else
   #define IMMIX_BLOCK_BITS      15
   typedef unsigned short BlockIdType;
#endif

#define IMMIX_BLOCK_SIZE        (1<<IMMIX_BLOCK_BITS)
#define IMMIX_BLOCK_OFFSET_MASK (IMMIX_BLOCK_SIZE-1)
#define IMMIX_BLOCK_BASE_MASK   (~(size_t)(IMMIX_BLOCK_OFFSET_MASK))
#define IMMIX_LINE_COUNT_BITS   (IMMIX_BLOCK_BITS-IMMIX_LINE_BITS)
#define IMMIX_LINES             (1<<IMMIX_LINE_COUNT_BITS)


#define IMMIX_HEADER_LINES      (IMMIX_LINES>>IMMIX_LINE_BITS)
#define IMMIX_USEFUL_LINES      (IMMIX_LINES - IMMIX_HEADER_LINES)

#define IMMIX_MAX_ALLOC_GROUPS_SIZE  (1<<IMMIX_BLOCK_GROUP_BITS)


// Every second line used
#define MAX_HOLES (IMMIX_USEFUL_LINES>>1)

/*

 IMMIX Alloc Header - 32 bits


 The header is placed in the 4 bytes before the object pointer, and it is placed there using a uint32.
 When addressed as the uint32, you can use the bitmasks below to extract the various data.

 The "mark id" can conveniently be interpreted as a byte, however the offsets well
  be different depending on whether the system is little endian or big endian.


  Little endian - lsb first

 7   0 15 8 23 16 31 24
 -----------------------
 |    |    |     | MID  | obj start here .....
 -----------------------



  Big endian - msb first

 31 24 23 16 15 8 7   0
 -----------------------
 |MID |    |     |      | obj start here .....
 -----------------------

MID = HX_ENDIAN_MARK_ID_BYTE = is measured from the object pointer
      HX_ENDIAN_MARK_ID_BYTE_HEADER = is measured from the header pointer (4 bytes before object)


*/

#define HX_ENDIAN_MARK_ID_BYTE_HEADER (HX_ENDIAN_MARK_ID_BYTE + 4)


// Used by strings
// HX_GC_CONST_ALLOC_BIT  0x80000000

//#define HX_GC_REMEMBERED          0x40000000
#define IMMIX_ALLOC_MARK_ID         0x3f000000
//#define IMMIX_ALLOC_IS_CONTAINER  0x00800000
//#define IMMIX_ALLOC_IS_PINNED     not used at object level
//#define HX_GX_STRING_EXTENDED     0x00200000
//#define HX_GC_STRING_HASH         0x00100000
// size will shift-right IMMIX_ALLOC_SIZE_SHIFT (6).  Low two bits are 0
#define IMMIX_ALLOC_SIZE_MASK       0x000fff00
#define IMMIX_ALLOC_ROW_COUNT       0x000000ff

#define IMMIX_HEADER_PRESERVE       0x00f00000


#define IMMIX_OBJECT_HAS_MOVED 0x000000fe


// Bigger than this, and they go in the large object pool
#define IMMIX_LARGE_OBJ_SIZE 4000

#ifdef allocString
#undef allocString
#endif


void CriticalGCError(const char *inMessage)
{
   // Can't perfrom normal handling because it needs the GC system
   #ifdef ANDROID
   __android_log_print(ANDROID_LOG_ERROR, "HXCPP", "Critical Error: %s", inMessage);
   #elif defined(HX_WINRT)
      WINRT_LOG("HXCPP Critical Error: %s\n", inMessage);
   #else
   printf("Critical Error: %s\n", inMessage);
   #endif

   DebuggerTrap();
}





enum AllocType { allocNone, allocString, allocObject, allocMarked };

struct BlockDataInfo *gBlockStack = 0;
typedef hx::QuickVec<hx::Object *> ObjectStack;

static std::mutex &sThreadPoolLock = *new std::mutex();

// For threaded marking/block reclaiming
static std::atomic<unsigned int> sRunningThreads{0};
static unsigned int sAllThreads = 0;
static bool sLazyThreads = false;
static bool sThreadPoolInit = false;

enum ThreadPoolJob
{
   tpjNone,
   tpjMark,
   tpjReclaim,
   tpjReclaimFull,
   tpjCountRows,
   tpjAsyncZero,
   tpjAsyncZeroJit,
   tpjGetStats,
   tpjVisitBlocks,
};

int sgThreadCount = 0;
static ThreadPoolJob sgThreadPoolJob = tpjNone;
static bool sgThreadPoolAbort = false;

// Pthreads enters the sleep state while holding a mutex, so it no cost to update
//  the sleeping state and thereby avoid over-signalling the condition
bool                         sThreadSleeping[MAX_GC_THREADS];
// Set under sThreadPoolLock when a woken worker leaves its wait. Until then it
//  has taken no work, so an incremental slice can withdraw the wake
static bool                  sWorkerStarted[MAX_GC_THREADS];
std::condition_variable_any* sThreadWake[MAX_GC_THREADS];
bool                         sThreadJobDoneSleeping = false;
std::condition_variable_any* sThreadJobDone;


static inline void SignalThreadPool(std::condition_variable_any* ioSignal, bool sThreadSleeping)
{
    if (sThreadSleeping)
        ioSignal->notify_one();
}

static void wakeThreadLocked(int inThreadId)
{
   sRunningThreads |= (1<<inThreadId);
   sWorkerStarted[inThreadId] = false;
   sLazyThreads = sRunningThreads != sAllThreads;
   SignalThreadPool(sThreadWake[inThreadId],sThreadSleeping[inThreadId]);
}

union BlockData
{
   // First 2/4 bytes are not needed for row markers (first 2/4 rows are for flags)
   BlockIdType mId;

   // First 2/4 rows contain a byte-flag-per-row 
   unsigned char mRowMarked[IMMIX_LINES];
   // Row data as union - don't use first 2/4 rows
   unsigned char mData[IMMIX_LINES * IMMIX_LINE_LEN];

   inline unsigned char* row(size_t r)
   {
       return mData + (r * IMMIX_LINE_LEN);
   }
};

struct BlockDataStats
{
   void clear() { ZERO_MEM(this, sizeof(BlockDataStats)); }
   void add(const BlockDataStats &inOther)
   {
      rowsInUse += inOther.rowsInUse;
      bytesInUse += inOther.bytesInUse;
      emptyBlocks += inOther.emptyBlocks;
      fraggedBlocks += inOther.fraggedBlocks;
      fragScore += inOther.fragScore;
      fraggedRows += inOther.fraggedRows;
   }

   size_t rowsInUse;
   size_t bytesInUse;
   size_t emptyBlocks;
   size_t fragScore;
   size_t fraggedBlocks;
   size_t fraggedRows;
};

static BlockDataStats sThreadBlockDataStats[MAX_GC_THREADS];
#ifdef HXCPP_VISIT_ALLOCS
static hx::VisitContext *sThreadVisitContext = 0;
#endif
   


namespace hx { void MarkerReleaseWorkerLocked(); }




struct GroupInfo
{
   int  blocks;
   char *alloc;

   bool pinned;
   bool isEmpty;
   int  usedBytes;
   int  usedSpace;

   void clear()
   {
      pinned = false;
      usedBytes = 0;
      usedSpace = 0;
      isEmpty = true;
   }
   int getMoveScore()
   {
      return pinned ?  0 : usedSpace-usedBytes;
   }

};
 
hx::QuickVec<GroupInfo> &gAllocGroups = *new hx::QuickVec<GroupInfo>();



struct HoleRange
{
   unsigned short start;
   unsigned short length;
};


hx::QuickVec<struct BlockDataInfo *> *gBlockInfo = 0;
// Ids of destroyed blocks, reused before gBlockInfo grows
static hx::QuickVec<int> &gBlockInfoFreeIds = *new hx::QuickVec<int>();

#define FRAG_THRESH 14

#define ZEROED_NOT    0
#define ZEROED_THREAD 1
#define ZEROED_AUTO   2

// Align padding based on block offset
#ifdef HXCPP_ALIGN_ALLOC
    #define ALIGN_PADDING(x) (4-(x&4))
#else
    #define ALIGN_PADDING(x) 0
#endif

struct BlockDataInfo
{
   int             mId;
   int             mGroupId;
   BlockData       *mPtr;

   unsigned int allocStart[IMMIX_LINES];

   HoleRange    mRanges[MAX_HOLES];
   uint8_t      mHoles;

   uint16_t     mUsedRows;
   uint16_t     mMaxHoleSize;
   int          mMoveScore;
   uint16_t     mUsedBytes;
   uint16_t     mFraggedRows;
   bool         mPinned;
   unsigned char mZeroed;
   bool         mReclaimed;
   bool         mOwned;
   #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
   bool         mHasSurvivor;
   #endif
   std::atomic_int mZeroLock;


   BlockDataInfo(int inGid, BlockData *inData)
   {
      if (!gBlockInfoFreeIds.empty())
      {
         mId = gBlockInfoFreeIds.pop();
         (*gBlockInfo)[mId] = this;
      }
      else
      {
         if (gBlockInfo==0)
            gBlockInfo = new hx::QuickVec<BlockDataInfo *>;
         mId = gBlockInfo->size();
         gBlockInfo->push( this );
      }


      mZeroLock = 0;
      mOwned = false;
      mGroupId = inGid;
      mPtr     = inData;
      inData->mId = mId;
      #ifdef SHOW_MEM_EVENTS
      //GCLOG("  create block %d : %p -> %p\n",  mId, this, mPtr );
      #endif
      clear();
   }

   void clear()
   {
      mUsedRows = 0;
      mUsedBytes = 0;
      mFraggedRows = 0;
      mPinned = false;
      ZERO_MEM(allocStart,sizeof(int)*IMMIX_LINES);
      ZERO_MEM(mPtr->mRowMarked+IMMIX_HEADER_LINES, IMMIX_USEFUL_LINES); 
      mRanges[0].start = IMMIX_HEADER_LINES << IMMIX_LINE_BITS;
      mRanges[0].length = IMMIX_USEFUL_LINES << IMMIX_LINE_BITS;
      mMaxHoleSize = mRanges[0].length;
      mMoveScore = 0;
      mHoles = 1;
      mZeroed = ZEROED_NOT;
      mReclaimed = true;
      mZeroLock = 0;
      mOwned = false;
   }

   void makeFull()
   {
      mUsedRows = IMMIX_USEFUL_LINES;
      mUsedBytes = uint16_t{ mUsedRows } << IMMIX_LINE_BITS;
      mFraggedRows = 0;
      memset(mPtr->mRowMarked+IMMIX_HEADER_LINES, 1,IMMIX_USEFUL_LINES); 
      mRanges[0].start = 0;
      mRanges[0].length = 0;
      mMaxHoleSize = 0;
      mMoveScore = 0;
      mHoles = 0;
      mZeroed = ZEROED_AUTO;
      mReclaimed = true;
      mZeroLock = 0;
      mOwned = false;
   }


   void clearBlockMarks()
   {
      mPinned = false;
      #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
      mHasSurvivor = false;
      #endif
   }

   void clearRowMarks()
   {
      clearBlockMarks();
      ZERO_MEM((char *)mPtr+IMMIX_HEADER_LINES, IMMIX_USEFUL_LINES);
   }

   inline int GetFreeRows() const { return (IMMIX_USEFUL_LINES - mUsedRows); }
   inline int GetFreeData() const { return (IMMIX_USEFUL_LINES - mUsedRows)<<IMMIX_LINE_BITS; }


   bool zeroAndUnlock()
   {
      bool doZero = !mZeroed;
      if (doZero)
      {
         if (!mReclaimed)
            reclaim<false>(0);

         for (uint8_t i{ 0 }; i < mHoles; i++)
             ZERO_MEM( (char *)mPtr+mRanges[i].start, mRanges[i].length );
         mZeroed = ZEROED_THREAD;
      }
      mZeroLock = 0;
      return doZero;
   }

   #ifdef HXCPP_GC_INCREMENTAL
   bool tryReclaim()
   {
      int expected{ 0 };
      if (!mZeroLock.compare_exchange_strong(expected, 1))
         return false;
      bool doReclaim = !mReclaimed && !mOwned;
      if (doReclaim)
         reclaim<false>(0);
      mZeroLock = 0;
      return doReclaim;
   }
   #endif

   bool tryZero()
   {
      if (mZeroed)
         return false;

      int expected{ 0 };
      if (mZeroLock.compare_exchange_strong(expected, 1))
         return zeroAndUnlock();

      return false;
   }

   void destroy()
   {
      #ifdef SHOW_MEM_EVENTS_VERBOSE
      GCLOG("  release block %d : %p\n",  mId, this );
      #endif
      (*gBlockInfo)[mId] = 0;
      gBlockInfoFreeIds.push(mId);
      delete this;
   }

   bool isEmpty() const { return mUsedRows == 0; }
   int getUsedRows() const { return mUsedRows; }

   // A row count above the block's useful lines is corrupted GC state, about to be
   // added into the heap stats that size working memory.
   void checkRowCounts() const
   {
      #ifdef HXCPP_DEBUG
      if (mUsedRows>IMMIX_USEFUL_LINES || mFraggedRows>IMMIX_USEFUL_LINES)
      {
         GCLOG("Bad block row counts: used %d, fragged %d, max %d\n", (int)mUsedRows, (int)mFraggedRows, (int)IMMIX_USEFUL_LINES);
         DebuggerTrap();
      }
      #endif
   }

   void getStats(BlockDataStats &outStats)
   {
      checkRowCounts();
      outStats.rowsInUse += mUsedRows;
      outStats.bytesInUse += mUsedBytes;
      outStats.fraggedRows += mFraggedRows;
      //outStats.bytesInUse += 0;
      outStats.fragScore += mPinned ? (mMoveScore>0?1:0) : mMoveScore;

      if (mUsedRows==0)
         outStats.emptyBlocks++;
      if (mMoveScore> FRAG_THRESH )
         outStats.fraggedBlocks++;
   }

   #ifdef HX_GC_VERIFY_ALLOC_START
   void verifyAllocStart()
   {
      unsigned char *rowMarked = mPtr->mRowMarked;

      for(int r = IMMIX_HEADER_LINES; r<IMMIX_LINES; r++)
      {
         if (!rowMarked[r] && allocStart[r])
         {
            printf("allocStart set without marking\n");
            DebuggerTrap();
         }
      }
   }
   #endif

   void countRows(BlockDataStats &outStats)
   {
      #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
      // Only a minor collection counts rows, and it marks rows in a block only
      // for an object it marks there, which sets mHasSurvivor. Without one,
      // the row marks are the ones mUsedRows was worked out from, so it stands,
      // and reading them again in every block was most of an idle minor
      // collection (libhxcpp #329). The state below is reset all the same.
      if (mHasSurvivor)
      #endif
         countMarkedRows();

      mZeroLock = 0;
      mOwned = false;
      checkRowCounts();
      outStats.rowsInUse += mUsedRows;
      outStats.bytesInUse += mUsedBytes;
      outStats.fraggedRows += mFraggedRows;
      mFraggedRows = 0;
      mHoles = 0;

      if (mUsedRows==IMMIX_USEFUL_LINES)
      {
         // All rows used - write the block off
         mMoveScore = 0;
         mZeroed = ZEROED_AUTO;
         mReclaimed = true;
      }
      else
      {
         mZeroed = ZEROED_NOT;
         mReclaimed = false;
      }

      #ifdef HXCPP_GC_INCREMENTAL
      mMaxHoleSize = largestHole();
      #else
      uint16_t left{ static_cast<uint16_t>((IMMIX_USEFUL_LINES - mUsedRows) << IMMIX_LINE_BITS) };
      if (left<mMaxHoleSize)
         mMaxHoleSize = left;
      #endif
   }

   #ifdef HXCPP_GC_INCREMENTAL
   // For the free list, which a block joins before it has been reclaimed
   uint16_t largestHole() const
   {
      const unsigned char *rowMarked = mPtr->mRowMarked;
      int largest = 0;
      int hole = 0;
      int r = IMMIX_HEADER_LINES;
      while(r<IMMIX_LINES)
      {
         if ((r & 3)==0 && r+4<=IMMIX_LINES)
         {
            unsigned int rows = *reinterpret_cast<const unsigned int *>(rowMarked+r);
            if (rows==0)
            {
               hole += 4;
               r += 4;
               continue;
            }
         }
         if (rowMarked[r])
         {
            if (hole>largest)
               largest = hole;
            hole = 0;
         }
         else
            hole++;
         r++;
      }
      if (hole>largest)
         largest = hole;
      return static_cast<uint16_t>(largest << IMMIX_LINE_BITS);
   }
   #endif

   // mUsedRows and mUsedBytes from the row marks
   void countMarkedRows()
   {
      unsigned char *rowMarked = mPtr->mRowMarked;
      unsigned int *rowTotals = ((unsigned int *)rowMarked) + 1;

      // TODO - sse/neon
      #ifdef HXCPP_GC_BIG_BLOCKS
      unsigned int total = 0;
      #else
      unsigned int total = rowMarked[2] + rowMarked[3];
      #endif

      total +=
       rowTotals[0]  + rowTotals[1]  + rowTotals[2]  + rowTotals[3]  + rowTotals[4] +
       rowTotals[5]  + rowTotals[6]  + rowTotals[7]  + rowTotals[8]  + rowTotals[9] +
       rowTotals[10] + rowTotals[11] + rowTotals[12] + rowTotals[13] + rowTotals[14] +
       rowTotals[15] + rowTotals[16] + rowTotals[17] + rowTotals[18] + rowTotals[19] +
       rowTotals[20] + rowTotals[21] + rowTotals[22] + rowTotals[23] + rowTotals[24] +
       rowTotals[25] + rowTotals[26] + rowTotals[27] + rowTotals[28] + rowTotals[29] +
       rowTotals[30] + rowTotals[31] + rowTotals[32] + rowTotals[33] + rowTotals[34] +
       rowTotals[35] + rowTotals[36] + rowTotals[37] + rowTotals[38] + rowTotals[39] +
       rowTotals[40] + rowTotals[41] + rowTotals[42] + rowTotals[43] + rowTotals[44] +
       rowTotals[45] + rowTotals[46] + rowTotals[47] + rowTotals[48] + rowTotals[49] +
       rowTotals[50] + rowTotals[51] + rowTotals[52] + rowTotals[53] + rowTotals[54] +
       rowTotals[55] + rowTotals[56] + rowTotals[57] + rowTotals[58] + rowTotals[59] +
       rowTotals[60] + rowTotals[61] + rowTotals[62];


      #ifdef HXCPP_GC_BIG_BLOCKS
      rowTotals += 63;
      total +=
       rowTotals[0]  + rowTotals[1]  + rowTotals[2]  + rowTotals[3]  + rowTotals[4] +
       rowTotals[5]  + rowTotals[6]  + rowTotals[7]  + rowTotals[8]  + rowTotals[9] +
       rowTotals[10] + rowTotals[11] + rowTotals[12] + rowTotals[13] + rowTotals[14] +
       rowTotals[15] + rowTotals[16] + rowTotals[17] + rowTotals[18] + rowTotals[19] +
       rowTotals[20] + rowTotals[21] + rowTotals[22] + rowTotals[23] + rowTotals[24] +
       rowTotals[25] + rowTotals[26] + rowTotals[27] + rowTotals[28] + rowTotals[29] +
       rowTotals[30] + rowTotals[31] + rowTotals[32] + rowTotals[33] + rowTotals[34] +
       rowTotals[35] + rowTotals[36] + rowTotals[37] + rowTotals[38] + rowTotals[39] +
       rowTotals[40] + rowTotals[41] + rowTotals[42] + rowTotals[43] + rowTotals[44] +
       rowTotals[45] + rowTotals[46] + rowTotals[47] + rowTotals[48] + rowTotals[49] +
       rowTotals[50] + rowTotals[51] + rowTotals[52] + rowTotals[53] + rowTotals[54] +
       rowTotals[55] + rowTotals[56] + rowTotals[57] + rowTotals[58] + rowTotals[59] +
       rowTotals[60] + rowTotals[61] + rowTotals[62] + rowTotals[63];

      #endif

      mUsedRows = (total & 0xff) + ((total>>8) & 0xff) + ((total>>16)&0xff) + ((total>>24)&0xff);
      mUsedBytes = uint16_t{ mUsedRows } << IMMIX_LINE_BITS;
   }

   template<bool FULL>
   void reclaim(BlockDataStats *outStats)
   {
      HoleRange *ranges = mRanges;
      ranges[0].length = 0;
      mZeroed = ZEROED_NOT;
      int usedBytes = 0;

      unsigned char *rowMarked = mPtr->mRowMarked;

      int r = IMMIX_HEADER_LINES;
      // Count unused rows ....
     
      // start on 4-byte boundary...
      #ifdef HXCPP_ALIGN_ALLOC
      while(r<4 && rowMarked[r]==0)
         r++;
      if (!rowMarked[r])
      #endif
      {
         while(r<(IMMIX_LINES-4) && *(int *)(rowMarked+r)==0 )
            r += 4;
         while(r<(IMMIX_LINES) && rowMarked[r]==0)
            r++;
      }

      if (r==IMMIX_LINES)
      {
         ranges[0].start  = IMMIX_HEADER_LINES<<IMMIX_LINE_BITS;
         ranges[0].length = (IMMIX_USEFUL_LINES)<<IMMIX_LINE_BITS;
         mMaxHoleSize = static_cast<uint16_t>(IMMIX_USEFUL_LINES << IMMIX_LINE_BITS);
         mUsedRows = 0;
         mHoles = 1;
         mMoveScore = 0;
         ZERO_MEM(allocStart+IMMIX_HEADER_LINES, IMMIX_USEFUL_LINES*sizeof(int));
      }
      else
      {
         int hole = 0;

         if (r>IMMIX_HEADER_LINES)
         {
            ranges[hole].start  = IMMIX_HEADER_LINES;
            ranges[hole].length = r-IMMIX_HEADER_LINES;
            hole++;
         }

         while(r<IMMIX_LINES)
         {
            if (rowMarked[r])
            {
               if (FULL)
               {
                  unsigned int &starts = allocStart[r];
                  if (starts)
                  {
                     unsigned int* headerPtr{ reinterpret_cast<unsigned int*>(mPtr->row(static_cast<size_t>(r))) };
                     #define CHECK_FLAG(i,byteMask) \
                     { \
                        unsigned int mask = 1<<i; \
                        if ( starts & mask ) \
                        { \
                           unsigned int header = headerPtr[i]; \
                           if ( (header & IMMIX_ALLOC_MARK_ID) != hx::gMarkID ) \
                           { \
                              starts ^= mask; \
                              if (!(starts & byteMask)) \
                                 break; \
                           } \
                           else \
                              usedBytes += sizeof(int) + ((header & IMMIX_ALLOC_SIZE_MASK) >> IMMIX_ALLOC_SIZE_SHIFT); \
                        } \
                     }

                     if (starts & 0x000000ff)
                        for(int i=0;i<8;i++)
                           CHECK_FLAG(i,0x000000ff);

                     if (starts & 0x0000ff00)
                        for(int i=8;i<16;i++)
                           CHECK_FLAG(i,0x0000ff00);

                     if (starts & 0x00ff0000)
                        for(int i=16;i<24;i++)
                           CHECK_FLAG(i,0x00ff0000);

                     if (starts & 0xff000000)
                        for(int i=24;i<32;i++)
                           CHECK_FLAG(i,0xff000000);
                  }
               }
               r++;
            }
            else
            {
               int start = r;
               ranges[hole].start = start;

               #ifdef HXCPP_ALIGN_ALLOC
               int alignR = (r+3) & ~3;
               while(r<alignR && rowMarked[r]==0)
                  r++;
               if (!rowMarked[r])
               #endif
               {
                  while(r<(IMMIX_LINES-4) && *(int *)(rowMarked+r)==0 )
                     r += 4;
                  while(r<(IMMIX_LINES) && rowMarked[r]==0)
                     r++;
               }
               ranges[hole].length = r-start;
               hole++;
            }
         }


         int freeLines = 0;
         // Convert hole rows to hole bytes...
         mMaxHoleSize = 0;
         for(int h=0;h<hole;h++)
         {
            uint16_t s{ ranges->start };
            uint16_t l{ ranges->length };
            freeLines += l;
            ZERO_MEM(allocStart+s, l*sizeof(int));

            uint16_t sBytes{ static_cast<uint16_t>(s << IMMIX_LINE_BITS) };
            ranges->start = sBytes;

            uint16_t lBytes{ static_cast<uint16_t>(l << IMMIX_LINE_BITS) };
            ranges->length = lBytes;

            if (lBytes>mMaxHoleSize)
              mMaxHoleSize = lBytes;

            ranges++;
         }
         mUsedRows = IMMIX_USEFUL_LINES - freeLines;
         mHoles = hole;
      }

      mUsedBytes = FULL ? usedBytes : uint16_t{ mUsedRows } << IMMIX_LINE_BITS;
      mMoveScore = calcFragScore();
      mReclaimed = true;

      checkRowCounts();
      if (outStats)
      {
         outStats->rowsInUse += mUsedRows;
         outStats->bytesInUse += mUsedBytes;
         outStats->fragScore += mMoveScore;
         outStats->fraggedRows += mFraggedRows;

         if (mUsedRows==0)
            outStats->emptyBlocks++;
         if (mMoveScore> FRAG_THRESH )
            outStats->fraggedBlocks++;
      }

      mFraggedRows = 0;
   }

   int calcFragScore() const
   {
      return mPinned ? 0 : saturating_sub<uint8_t>(mHoles, 3) + 8 * (mUsedRows<<IMMIX_LINE_BITS) / (mUsedBytes+IMMIX_LINE_LEN);
   }


   // When known to be an actual object start...
   AllocType GetAllocTypeChecked(uintptr_t inOffset, bool allowPrevious) const
   {
      unsigned char time{ mPtr->mData[inOffset + HX_ENDIAN_MARK_ID_BYTE_HEADER] };
      if (((time + 1) & MARK_BYTE_MASK) != (gByteMarkID & MARK_BYTE_MASK))
      {
         // Object is either out-of-date, or already marked....
         return time==gByteMarkID ? allocMarked : allocNone;
      }

      if (!allowPrevious)
         return allocNone;

      if (*reinterpret_cast<unsigned int *>(mPtr->mData + inOffset) & IMMIX_ALLOC_IS_CONTAINER)
      {
         // See if object::new has been called, but not constructed yet ...
         void** vtable{ reinterpret_cast<void**>(mPtr->mData + inOffset + sizeof(int)) };
         if (nullptr == vtable[0])
         {
            // GCLOG("Partially constructed object.");
            return allocString;
         }
         return allocObject;
      }

      return allocString;
   }

   #ifdef HXCPP_GC_NURSERY
   AllocType GetEnclosingNurseryType(uintptr_t inOffset, uintptr_t* outPtr) const
   {
      // The block did not get used in the previous cycle, so allocStart is invalid and
      //  no new objects should be in here
      if (!mZeroed)
         return allocNone;
      // For the nursery(generational) case, the allocStart markers are not set
      // So trace tne new object links through the new allocation holes
      for (uint8_t h{ 0 }; h < mHoles; h++)
      {
         size_t scan{ mRanges[h].start };
         if (inOffset<scan)
            break;

         size_t last{ scan + mRanges[h].length };
         if (inOffset<last)
         {
            // Found hole that the object was possibly allocated in
            while(scan<=inOffset)
            {
               // Trace along the hole...
               unsigned int header{ *reinterpret_cast<unsigned int*>(mPtr->mData + scan) };
               #ifdef HXCPP_ALIGN_ALLOC
               // The hole was zeroed before use, so a zero word here is the
               //  padding that started the next allocation 8-byte aligned.
               //  String data skips it (libhxcpp #370), so a header can
               //  start at any int
               if (!header)
               {
                  scan += 4;
                  if (scan>inOffset || scan>=last)
                     break;
                  header = *reinterpret_cast<unsigned int*>(mPtr->mData + scan);
               }
               #endif
               unsigned int size{
                  !(header & 0xff000000)
                     ? (header & 0x0000ffff)
                     : ((header & IMMIX_ALLOC_SIZE_MASK) >> IMMIX_ALLOC_SIZE_SHIFT) };

               size_t end{ scan + size + sizeof(int) };
               if (!size || end > last)
                  return allocNone;

               if (inOffset>=scan && inOffset<end)
               {
                  if (inOffset > scan + sgCheckInternalOffset)
                     return allocNone;

                  if (nullptr != outPtr)
                  {
                     *outPtr = reinterpret_cast<uintptr_t>(mPtr->mData + scan + sizeof(int));
                  }

                  if (header & IMMIX_ALLOC_IS_CONTAINER)
                  {
                     // See if object::new has been called, but not constructed yet ...
                     void **vtable{ reinterpret_cast<void **>(mPtr->mData + scan + sizeof(int)) };
                     if (vtable[0]==0)
                     {
                        // GCLOG("Partially constructed object.");
                        return allocString;
                     }
                     return allocObject;
                  }
                  return allocString;
               }
               scan = end;
            }
            break;
         }
      }
      return allocNone;
}
   #endif

   AllocType GetAllocType(uintptr_t inOffset, bool inAllowPrevious) const
   {
      // Row that the header would be on
      uintptr_t r{ inOffset >> IMMIX_LINE_BITS };

      // Out of bounds - can't be a new object start
      if (r < IMMIX_HEADER_LINES || r >= IMMIX_LINES)
      {
         return allocNone;
      }

      // Does a live object start on this row
      if ( !( allocStart[r] & hx::gImmixStartFlag[inOffset &127]) )
      {
         #ifdef HXCPP_GC_NURSERY
         return GetEnclosingNurseryType(inOffset, nullptr);
         #endif
         //Not a actual start...
         return allocNone;
      }

      return GetAllocTypeChecked(inOffset,inAllowPrevious);
   }

   AllocType GetEnclosingAllocType(uintptr_t inOffset, uintptr_t* outPtr, bool inAllowPrevious) const
   {
      for (uintptr_t dx{ 0 }; dx <= sgCheckInternalOffset; dx += 4)
      {
         uintptr_t blockOffset{ inOffset - dx };
         if (blockOffset >= 0)
         {
            uintptr_t r{ blockOffset >> IMMIX_LINE_BITS };
            if (r >= IMMIX_HEADER_LINES && r < IMMIX_LINES)
            {
               // Normal, good alloc
               unsigned int rowPos{ hx::gImmixStartFlag[blockOffset & 127] };
               if ( allocStart[r] & rowPos )
               {
                  // Found last valid object - is it big enough?
                  unsigned int header{ *reinterpret_cast<unsigned int*>(reinterpret_cast<char*>(mPtr) + blockOffset) };
                  size_t size{ (header & IMMIX_ALLOC_SIZE_MASK) >> IMMIX_ALLOC_SIZE_SHIFT };
                  // Valid object not big enough...
                  if (blockOffset + size + sizeof(int) <= inOffset )
                     break;

                  // If the object is old, it could be the tail end of an old row that
                  //  thinks is is covering this row, but it is not because this row is reused.
                  // So we can say there is no other object that could be covering
                  //  this spot, but not that it is not a nursery object.
                  AllocType result{ GetAllocTypeChecked(blockOffset,inAllowPrevious) };
                  if (result!=allocNone)
                  {
                     if (nullptr != outPtr)
                     {
                        *outPtr = reinterpret_cast<uintptr_t>(mPtr->mData + blockOffset + sizeof(int));
                     }
                     
                     return result;
                  }
                  break;
               }
            }
          }
      }

      #ifdef HXCPP_GC_NURSERY
      return GetEnclosingNurseryType(inOffset,outPtr);
      #endif
      // Not a actual start...
      return allocNone;
   }


   void pin() { mPinned = true; }


   #ifdef HXCPP_VISIT_ALLOCS
   void VisitBlock(hx::VisitContext *inCtx)
   {
      if (isEmpty())
         return;

      unsigned char *rowMarked = mPtr->mRowMarked;
      for(int r=IMMIX_HEADER_LINES;r<IMMIX_LINES;r++)
      {
         if (rowMarked[r])
         {
            unsigned int starts{ allocStart[r] };
            if (!starts)
               continue;
            unsigned char* row{ mPtr->row(static_cast<size_t>(r)) };
            for(int i=0;i<32;i++)
            {
               int pos = i<<2;
               if ( (starts & (1<<i)) &&
                   (row[pos+HX_ENDIAN_MARK_ID_BYTE_HEADER]&FULL_MARK_BYTE_MASK) == gByteMarkID)
                  {
                     if ( (*(unsigned int *)(row+pos)) & IMMIX_ALLOC_IS_CONTAINER )
                     {
                        hx::Object *obj = (hx::Object *)(row+pos+4);
                        // May be partially constructed
                        if (*(void **)obj)
                           obj->__Visit(inCtx);
                     }
                  }
            }
         }
      }
   }
   #endif

   #ifdef HXCPP_GC_VERIFY
   void verify(const char *inWhere)
   {
      for(int i=IMMIX_HEADER_LINES;i<IMMIX_LINES;i++)
      {
         for(int j=0;j<32;j++)
            if (allocStart[i] & (1<<j))
            {
               unsigned int header = *(unsigned int *)( (char *)mPtr + i*IMMIX_LINE_LEN + j*4 );
               if ((header & IMMIX_ALLOC_MARK_ID) == hx::gMarkID )
               {
                  int rows = header & IMMIX_ALLOC_ROW_COUNT;
                  if (rows==0)
                  {
                     printf("BAD ROW0 %s\n", inWhere);
                     DebuggerTrap();
                  }
                  for(int r=0;r<rows;r++)
                     if (!mPtr->mRowMarked[r+i])
                     {
                        printf("Unmarked row %dx %d/%d %s, t=%d!\n", i, r, rows, inWhere,sgTimeToNextTableUpdate);
                        DebuggerTrap();
                     }
               }
            }
         }

   }
   #endif
};

static_assert(IMMIX_USEFUL_LINES <= std::numeric_limits<decltype(BlockDataInfo::mUsedRows)>::max(),
              "BlockDataInfo::mUsedRows must hold IMMIX_USEFUL_LINES");
static_assert(IMMIX_USEFUL_LINES <= std::numeric_limits<decltype(BlockDataInfo::mFraggedRows)>::max(),
              "BlockDataInfo::mFraggedRows must hold IMMIX_USEFUL_LINES");
static_assert(MAX_HOLES <= std::numeric_limits<decltype(BlockDataInfo::mHoles)>::max(),
              "BlockDataInfo::mHoles must hold MAX_HOLES");
static_assert((IMMIX_USEFUL_LINES<<IMMIX_LINE_BITS) <= std::numeric_limits<decltype(BlockDataInfo::mUsedBytes)>::max(),
              "BlockDataInfo::mUsedBytes must hold a block's useful bytes");
static_assert((IMMIX_USEFUL_LINES<<IMMIX_LINE_BITS) <= std::numeric_limits<decltype(BlockDataInfo::mMaxHoleSize)>::max(),
              "BlockDataInfo::mMaxHoleSize must hold a block's useful bytes");
static_assert((IMMIX_LINES<<IMMIX_LINE_BITS)-1 <= std::numeric_limits<decltype(HoleRange::start)>::max() &&
              (IMMIX_USEFUL_LINES<<IMMIX_LINE_BITS) <= std::numeric_limits<decltype(HoleRange::length)>::max(),
              "HoleRange must hold offsets and lengths within a block");

static bool SmallestFreeFirst(BlockDataInfo *inA, BlockDataInfo *inB)
{
   return inA->mMaxHoleSize < inB->mMaxHoleSize;
}

static bool SortMoveOrder(BlockDataInfo *inA, BlockDataInfo *inB)
{
   return inA->mMoveScore > inB->mMoveScore;
}

namespace hx
{


// Kept out of line: inlined with its report, it made GetLocalAlloc too large to
// inline into InternalNew and the other allocation paths
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void BadImmixAlloc()
{

   #ifdef HX_WINRT
   WINRT_LOG("Bad local allocator - requesting memory from unregistered thread!");
   #else
   #ifdef ANDROID
   __android_log_print(ANDROID_LOG_ERROR, "hxcpp",
   #else
   fprintf(stderr,
   #endif

   "Bad local allocator - requesting memory from unregistered thread!"

   #ifdef ANDROID
   );
   #else
   );
   #endif
   #endif

   DebuggerTrap();
}



void GCCheckPointer(void *inPtr)
{
   #ifdef HXCPP_GC_DEBUG_ALWAYS_MOVE
   if (hx::sgPointerMoved.find(inPtr)!=hx::sgPointerMoved.end())
   {
      GCLOG("Accessing moved pointer %p\n", inPtr);
      DebuggerTrap();
   }
   #endif

   unsigned char&mark = ((unsigned char *)inPtr)[HX_ENDIAN_MARK_ID_BYTE];
   #ifdef HXCPP_GC_NURSERY
   if (mark)
   #endif
   if ( !(mark & HX_GC_CONST_ALLOC_MARK_BIT) && (mark&FULL_MARK_BYTE_MASK)!=gByteMarkID  )
   {
      GCLOG("Old object access %p\n", inPtr);
      NullReference("Object", false);
   }
}


void GCOnNewPointer(void *inPtr, bool inAligned)
{
   #ifdef HXCPP_ALIGN_ALLOC
   if ( reinterpret_cast<uintptr_t>(inPtr) & (inAligned ? 0x7 : 0x3) )
   {
      GCLOG("Misaligned pointer %p\n", inPtr);
      NullReference("Object", false);
   }
   #endif

   #ifdef HXCPP_GC_DEBUG_ALWAYS_MOVE
   hx::sgPointerMoved.erase(inPtr);
   sgAllocsSinceLastSpam++;
   #endif
}

// --- Marking ------------------------------------

struct MarkInfo
{
   const char *mClass;
   const char *mMember;
};

struct GlobalChunks
{
   std::atomic<MarkChunk*> processList;
   std::atomic_int         processListPopLock;
   std::atomic<MarkChunk*> freeList;
   std::atomic_int         freeListPopLock;

   GlobalChunks() : processList(), processListPopLock(), freeList(), freeListPopLock() {}

   MarkChunk *pushJobNoWake(MarkChunk *inChunk)
   {
      MarkChunk* head{};

      do
      {
         head = processList;
         inChunk->next = head;
      } while (false == processList.compare_exchange_strong(head, inChunk));

      return alloc();
   }

   MarkChunk *pushJob(MarkChunk *inChunk,bool inAndAlloc)
   {
      MarkChunk* head{};

      do
      {
         head = processList;
         inChunk->next = head;
      } while (false == processList.compare_exchange_strong(head, inChunk));

      #ifdef PROFILE_THREAD_USAGE
      sThreadChunkPushCount++;
      #endif

      if (MAX_GC_THREADS>1 && sLazyThreads)
      {
         std::lock_guard<std::mutex> l(sThreadPoolLock);

         #ifdef PROFILE_THREAD_USAGE
           #define CHECK_THREAD_WAKE(tid) \
            if (MAX_GC_THREADS >tid && sgThreadCount>tid && (!(sRunningThreads & (1<<tid)))) { \
            wakeThreadLocked(tid); \
            sThreadChunkWakes++; \
           } 
         #else
           #define CHECK_THREAD_WAKE(tid)  \
            if (MAX_GC_THREADS >tid && sgThreadCount>tid && (!(sRunningThreads & (1<<tid)))) { \
            wakeThreadLocked(tid); \
           }
         #endif


         CHECK_THREAD_WAKE(0)
         else CHECK_THREAD_WAKE(1)
         else CHECK_THREAD_WAKE(2)
         else CHECK_THREAD_WAKE(3)
         else CHECK_THREAD_WAKE(4)
         else CHECK_THREAD_WAKE(5)
         else CHECK_THREAD_WAKE(6)
         else CHECK_THREAD_WAKE(7)
      }

      if (inAndAlloc)
         return alloc();
      return 0;
   }

   void addLocked(MarkChunk *inChunk)
   {
      inChunk->next = processList;
      processList = inChunk;
   }

   int markPointers(bool andFree)
   {
      int size{ 0 };
      for (MarkChunk* c{ processList }; c; c = c->next)
      {
         for(int i=0;i<c->count;i++)
            ((unsigned char *)c->stack[i])[HX_ENDIAN_MARK_ID_BYTE] = gByteMarkID;
         size += c->count;
      }

      if (andFree)
      {
         while(processList)
         {
            MarkChunk* c{ processList };
            processList = c->next;
            c->count = 0;
            c->next = freeList;
            freeList = c;
         }
      }
      return size;
   }

   int takeArrayJob(hx::Object **inPtr, int inLen)
   {
      #ifdef HXCPP_GC_INCREMENTAL
      if (gIncrementalActive.load(std::memory_order_relaxed)) return 0;
      #endif
      if (sLazyThreads)
      {
         int n = (inLen/2) & ~15;

         if (n)
         {
            MarkChunk *chunk = alloc();
            chunk->count = MarkChunk::OBJ_ARRAY_JOB;
            chunk->arrayBase = inPtr;
            chunk->arrayElements = n;

            pushJob(chunk,false);
         }

         // Return how many to skip
         return n;
      }

      return 0;
   }

   inline void release(MarkChunk *inChunk)
   {
      MarkChunk* head{};

      do
      {
         head = freeList;
         inChunk->next = head;
      } while (false == freeList.compare_exchange_strong(head, inChunk));
   }


   MarkChunk *popJobLocked(MarkChunk *inChunk)
   {
      if (inChunk)
         release(inChunk);

      int expected{ 0 };
      while(false == processListPopLock.compare_exchange_strong(expected, 1))
      {
         // A failed exchange leaves the value it found in expected:
         // reset it, or the next attempt compares 1 with 1 and
         // "takes" a lock another thread holds.
         expected = 0;
         // Spin
         #ifdef PROFILE_THREAD_USAGE
         sSpinCount++;
         #endif
      }

      while(true)
      {
         MarkChunk* head{ processList };
         if (!head)
         {
            processListPopLock = 0;
            return 0;
         }
         MarkChunk *next = head->next;
         if (processList.compare_exchange_strong(head, next))
         {
            processListPopLock = 0;

            head->next = 0;
            return head;
         }
      }
      return 0;
   }


   void completeThreadLocked(int inThreadId)
   {
      if (!(sRunningThreads & (1<<inThreadId)))
      {
         printf("Complete non-running thread?\n");
         DebuggerTrap();
      }
      sRunningThreads &= ~(1<<inThreadId);
      sLazyThreads = sRunningThreads != sAllThreads;

      if (!sRunningThreads)
         SignalThreadPool(sThreadJobDone,sThreadJobDoneSleeping);
   }


   // Optionally returns inChunk to empty pool (while we have the lock),
   //  and returns a new job if there is one
   MarkChunk *popJobOrFinish(MarkChunk *inChunk,int inThreadId)
   {
      #ifdef HX_MULTI_THREAD_MARKING
      if (sAllThreads)
      {
         MarkChunk *result =  popJobLocked(inChunk);
         #ifdef HXCPP_GC_INCREMENTAL
         // The collecting thread, marking beside the workers in a slice. It is
         //  not one of them, so it has nothing to complete. It stays while they
         //  can still hand it work, which a zeroing job left running will not.
         if (inThreadId<0)
         {
            for(int spinCount = 0; !result && spinCount<10000 && sgThreadPoolJob==tpjMark && sRunningThreads; spinCount++)
               if (processList)
                  result = popJobLocked(0);
            return result;
         }
         #endif
         if (!result)
         {
            for(int spinCount = 0; spinCount<10000; spinCount++)
            {
               if ( sgThreadPoolAbort || sAllThreads == (1<<inThreadId) )
                  break;
               if (processList)
               {
                  result =  popJobLocked(0);
                  if (result)
                     return result;
               }
            }
            std::lock_guard<std::mutex> l(sThreadPoolLock);
            completeThreadLocked(inThreadId);
         }
         return result;
      }
      #endif

      return popJobLocked(inChunk);
   }


   void free(MarkChunk *inChunk)
   {
      release(inChunk);
   }

   // Every chunk taken was kept for reuse, so a burst of remembered referrers
   //  or marking left the free list at its peak for good: 8,000 chunks, 4 MB,
   //  after one million old owners took a young child (libhxcpp #374). This
   //  keeps inKeep and frees half of the rest, at most inMost, so a large
   //  surplus decays over several collections rather than going in one pause,
   //  and a workload that keeps needing it keeps most of it. Only while every
   //  thread is stopped and no marking runs
   void trimFree(int inKeep, int inMost)
   {
      MarkChunk *last = freeList;
      for(int i=1;last && i<inKeep;i++)
         last = last->next;
      if (!last)
         return;
      int surplusCount = 0;
      for(MarkChunk *c = last->next; c && surplusCount<inMost*2; c = c->next)
         surplusCount++;
      int toFree = std::min(inMost, surplusCount/2);
      MarkChunk *surplus = last->next;
      for(int i=0;surplus && i<toFree;i++)
      {
         MarkChunk *next = surplus->next;
         delete surplus;
         surplus = next;
      }
      last->next = surplus;
   }

   inline MarkChunk *alloc()
   {
      int expected{ 0 };
      while(false == freeListPopLock.compare_exchange_strong(expected, 1))
      {
         // A failed exchange leaves the value it found in expected:
         // reset it, or the next attempt compares 1 with 1 and
         // "takes" a lock another thread holds.
         expected = 0;
         // Spin
         #ifdef PROFILE_THREAD_USAGE
         sSpinCount++;
         #endif
      }

      while(true)
      {
         MarkChunk* head{ freeList };
         if (!head)
         {
            freeListPopLock = 0;
            return new MarkChunk;
         }
         MarkChunk* next{ head->next };
         if (freeList.compare_exchange_strong(head, next))
         {
            freeListPopLock = 0;

            head->next = 0;
            return head;
         }
      }
   }




   MarkChunk *getInitJob()
   {
      MarkChunk *result = popJobLocked(0);
      if (result)
      {
         // A chunk left by the last slice stays a job, for any marker
         if (result->count==MarkChunk::RANGE_CHUNK_JOB)
            pushJob(result,false);
         else if (result->count==MarkChunk::OBJ_ARRAY_JOB)
         {
            GCLOG("Popped array job?\n");
            pushJob(result,false);
         }
         else
            return result;
      }
      return alloc();
   }

};

GlobalChunks sGlobalChunks;

#ifdef HXCPP_GC_GENERATIONAL_RUNTIME
// A young value stored in an old array or map made the whole of it a
// remembered referrer, so the next minor collection marked every element,
// however few changed: one store in an old four-million-element array added
// 2-3 ms to it (libhxcpp #301), and one in an old million-entry map 1.3 ms
// (#305). For a large one, the young values themselves are logged now, and the
// minor collection marks them as roots. That keeps a value alive wherever it
// is moved to, by a sort, splice or blit in an array or a rehash in a map,
// which a range of dirty slots would not. A value overwritten before then
// lives one collection longer. Past a thirty-second of the length in one
// cycle, marking the whole is cheaper, so it is remembered whole, as before.
static MarkChunk *sYoungValueChunks = 0;
// Never destroyed: hx::FastMutex is a std::mutex off Windows, which a
//  thread still storing into an array at exit could take after it went (#371)
static hx::FastMutex &sYoungValueLock = *new hx::FastMutex();
static unsigned int sCollectCycle = 1;

enum { YOUNG_LOG_MIN_LENGTH = 1024 };

void YoungStore(hx::Object *inOwner, unsigned int &ioLogged, int inLength, const void *const *inValues, int inCount, StackContext *inCtx)
{
   if (inCtx->mYoungValues && inLength>=YOUNG_LOG_MIN_LENGTH)
   {
      // Counted per cycle: the cycle is in the top 12 bits. A lost update
      //  between threads only logs more than the limit
      unsigned int logged = ioLogged;
      unsigned int cycle = sCollectCycle & 0xfff;
      unsigned int count = (logged>>20)==cycle ? (logged & 0xfffff) : 0;
      unsigned int limit = std::min( (unsigned int)inLength>>5, 0xfffffu );
      if (count<limit)
      {
         ioLogged = (cycle<<20) | (count+1);
         for(int v=0;v<inCount;v++)
         {
            const void *value = inValues[v];
            if (!value || ((unsigned char *)value)[HX_ENDIAN_MARK_ID_BYTE])
               continue;
            MarkChunk *chunk = inCtx->mYoungValues;
            chunk->push( (hx::Object *)value );
            if (chunk->count==MarkChunk::SIZE)
            {
               {
                  std::lock_guard<hx::FastMutex> lock(sYoungValueLock);
                  chunk->next = sYoungValueChunks;
                  sYoungValueChunks = chunk;
               }
               inCtx->mYoungValues = sGlobalChunks.alloc();
            }
         }
         return;
      }
   }
   unsigned char &mark = ((unsigned char *)inOwner)[HX_ENDIAN_MARK_ID_BYTE];
   mark |= HX_GC_REMEMBERED;
   inCtx->pushReferrer(inOwner);
}

void ArrayYoungStore(ArrayBase *inArray, const void *inValue, StackContext *inCtx)
{
   YoungStore(inArray, inArray->mYoungLogged, inArray->length, &inValue, 1, inCtx);
}

// Takes a thread's log, under the lock or with the world stopped
static void TakeYoungValues(MarkChunk *&ioChunk)
{
   if (ioChunk)
   {
      if (ioChunk->count)
      {
         ioChunk->next = sYoungValueChunks;
         sYoungValueChunks = ioChunk;
      }
      else
         sGlobalChunks.free(ioChunk);
      ioChunk = 0;
   }
}

// Marks the logged values in a minor collection, and drops the log
static void MarkYoungValues(bool inMark, hx::MarkContext *inCtx)
{
   while(sYoungValueChunks)
   {
      MarkChunk *c = sYoungValueChunks;
      sYoungValueChunks = c->next;
      if (inMark)
         for(int i=0;i<c->count;i++)
         {
            void *value = c->stack[i];
            // An object with members to mark has the container bit; a string's
            //  data or a plain buffer is marked as an allocation
            if ( ((unsigned int *)value)[-1] & IMMIX_ALLOC_IS_CONTAINER )
               hx::MarkObjectAlloc((hx::Object *)value, inCtx);
            else
               hx::MarkAlloc(value, inCtx);
         }
      c->count = 0;
      sGlobalChunks.free(c);
   }
}
#endif

#ifdef HXCPP_GC_INCREMENTAL
static double IncrementalNow()
{
   return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static bool IncrementalExpired();
static void IncrementalScanned(Object *inObject);
#endif

class MarkContext
{
    #ifdef HXCPP_DEBUG
    MarkInfo  *mInfo;
    int       mPos;
    // Gc.trace's recursion, and where the path it prints starts for an
    //  object taken from the mark stack, or -1
    int       mTraceDepth;
    int       mTraceCut;
    #endif

    int       mThreadId;
    MarkChunk *marking;


public:
    enum { StackSize = 8192 };

    bool isGenerational;

    #ifdef HXCPP_GC_INCREMENTAL_VERIFY
    bool incrementalOracle = false;
    std::unordered_map<void *,bool> oracleVisited;
    std::vector<Object *> oracleQueue;
    #endif

    #ifdef HX_GC_VERIFY_GENERATIONAL
    // The object whose members are being marked, so a nursery object that
    //  escaped a generational collection can be reported with what held it
    hx::Object *mVerifyParent = 0;
    #endif

    MarkContext(int inThreadId = -1)
    {
       #ifdef HXCPP_DEBUG
       mInfo = new MarkInfo[StackSize];
       mPos = 0;
       mTraceDepth = 0;
       mTraceCut = -1;
       #endif
       mThreadId = inThreadId;
       marking = sGlobalChunks.alloc();

       isGenerational = false;
    }
    ~MarkContext()
    {
       if (marking) sGlobalChunks.free(marking);
       #ifdef HXCPP_DEBUG
       delete [] mInfo;
       #endif
       // TODO: Free slabs
    }


    #ifdef HXCPP_DEBUG
    void PushClass(const char *inClass)
    {
       if (mPos<StackSize-1)
       {
          mInfo[mPos].mClass = inClass;
          mInfo[mPos].mMember = 0;
       }
       mPos++;
    }


    void SetMember(const char *inMember)
    {
       // Should not happen...
       if (mPos==0)
          return;
       if (mPos<StackSize)
          mInfo[mPos-1].mMember = inMember ? inMember : "Unknown";
    }
    void PopClass() { mPos--; }

    // Gc.trace prints a path from the classes and members of the __Mark
    //  calls on the native stack, so it marks by recursing, down to this
    //  depth. Deeper objects go on the mark stack, so a deep graph cannot
    //  overflow the native one, and the paths below them start there
    enum { TraceDepthMax = 256 };

    // True when it marked inObj's members
    bool TraceRecurse(hx::Object *inObj)
    {
       if (!gCollectTraceDoPrint || mTraceDepth>=TraceDepthMax)
          return false;
       mTraceDepth++;
       inObj->__Mark(this);
       mTraceDepth--;
       return true;
    }

    void Trace()
    {
       int n = mPos < StackSize ? mPos : StackSize;
       #ifdef ANDROID
       __android_log_print(ANDROID_LOG_ERROR, "trace", "Class referenced from");
       #else
       printf("Class referenced from:\n");
       #endif

       int first = 0;
       if (mTraceCut>=0)
       {
          first = mTraceCut < n ? mTraceCut : n;
          #ifdef ANDROID
          __android_log_print(ANDROID_LOG_INFO, "trace", "... + higher");
          #else
          printf("... + higher\n");
          #endif
       }

       for(int i=first;i<n;i++)
          #ifdef ANDROID
          __android_log_print(ANDROID_LOG_INFO, "trace", "%s.%s",  mInfo[i].mClass, mInfo[i].mMember );
          #else
          printf("%s.%s\n",  mInfo[i].mClass, mInfo[i].mMember );
          #endif

       if (mPos>=StackSize)
       {
          #ifdef ANDROID
          __android_log_print(ANDROID_LOG_INFO, "trace", "... + deeper");
          #else
          printf("... + deeper\n");
          #endif
       }
    }
    #endif


    void pushObj(hx::Object *inObject)
    {
       if (marking->count < MarkChunk::SIZE)
       {
          marking->push(inObject);
       }
       else
       {
          marking = sGlobalChunks.pushJob(marking,true);
          marking->push(inObject);
       }
    }

    void init()
    {
       if (!marking)
          marking = sGlobalChunks.getInitJob();
    }

    void releaseJobs()
    {
       if (marking && marking->count)
       {
          sGlobalChunks.pushJob(marking,false);
       }
       else if (marking)
       {
          sGlobalChunks.release(marking);
       }
       marking = 0;
    }

    void processMarkStack()
    {
       while(true)
       {
          #ifdef HXCPP_GC_INCREMENTAL
          if (IncrementalExpired()) { releaseIncrementalJobs(); return; }
          #endif
          if (!marking || !marking->count)
          {
             #ifdef HX_MULTI_THREAD_MARKING
             if (sgThreadPoolAbort)
             {
                releaseJobs();
                return;
             }
             #endif

             marking = sGlobalChunks.popJobOrFinish(marking,mThreadId);
             if (!marking)
                break;

             if (marking->count==MarkChunk::OBJ_ARRAY_JOB)
             {
                int n = marking->arrayElements;
                hx::Object **elems = marking->arrayBase;
                marking->count = 0;
                MarkObjectArray(elems, n, this);
                continue;
             }
             #ifdef HXCPP_GC_INCREMENTAL
             if (marking->count==MarkChunk::RANGE_CHUNK_JOB)
             {
                hx::Object *owner = marking->rangeOwner;
                IncrementalRangeScan scan = marking->rangeScan;
                size_t cursor = marking->rangeStart;
                size_t item = 0;
                marking->count = 0;
                scan(owner, cursor, item, this);
                continue;
             }
             #endif
          }

          while(marking)
          {
             #ifdef HXCPP_GC_INCREMENTAL
             if (IncrementalExpired()) { releaseIncrementalJobs(); return; }
             #endif
             hx::Object *obj = marking->pop();
             if (obj)
             {
                #ifdef HXCPP_DEBUG
                // Gc.trace printed this object's path when it was pushed;
                //  the paths of what it holds start here
                if (gCollectTrace)
                   mTraceCut = mPos;
                #endif
                #ifdef HX_GC_VERIFY_GENERATIONAL
                mVerifyParent = obj;
                obj->__Mark(this);
                mVerifyParent = 0;
                #else
                obj->__Mark(this);
                #endif
                #ifdef HXCPP_DEBUG
                mTraceCut = -1;
                #endif
                #ifdef HXCPP_GC_INCREMENTAL
                IncrementalScanned(obj);
                #endif
                #ifdef HX_MULTI_THREAD_MARKING
                // Load balance
                if (sLazyThreads && marking->count>32)
                {
                   MarkChunk *c = sGlobalChunks.alloc();
                   marking->count -= 16;
                   c->count = 16;
                   memcpy( c->stack, marking->stack + marking->count, 16*sizeof(hx::Object *));
                   sGlobalChunks.pushJob(c,false);
                }
                #ifdef PROFILE_THREAD_USAGE
                sThreadMarkCount[mThreadId]++;
                #endif
                #endif
             }
             else
                break;
          }
       }
    }

    #ifdef HXCPP_GC_INCREMENTAL
    void releaseIncrementalJobs()
    {
       if (marking && marking->count)
       {
          MarkChunk *head = sGlobalChunks.processList.load();
          do
          {
             marking->next = head;
          } while (!sGlobalChunks.processList.compare_exchange_weak(head, marking));
       }
       else if (marking)
          sGlobalChunks.release(marking);
       marking = nullptr;
       if (mThreadId >= 0)
       {
          std::lock_guard<std::mutex> lock(sThreadPoolLock);
          if (sRunningThreads & (1 << mThreadId))
             sGlobalChunks.completeThreadLocked(mThreadId);
       }
    }
    bool hasWork() const { return (marking && marking->count) || sGlobalChunks.processList; }
    void discardJobs()
    {
       if (marking)
       {
          marking->count = 0;
          sGlobalChunks.free(marking);
          marking = nullptr;
       }
       while (sGlobalChunks.processList)
       {
          MarkChunk *chunk = sGlobalChunks.processList;
          sGlobalChunks.processList = chunk->next;
          chunk->count = 0;
          sGlobalChunks.free(chunk);
       }
    }
#endif
};


/*
void MarkerReleaseWorkerLocked( )
{
   //printf("Release...\n");
   for(int i=0;i<sAllThreads;i++)
   {
      if ( ! (sRunningThreads & (1<<i) ) )
      {
         //printf("Wake %d\n",i);
         sRunningThreads |= (1<<i);
         sThreadWake[i]->Set();
         return;
      }
   }
}
*/



#ifdef HXCPP_DEBUG
static bool breakOnce = 1;
void MarkSetMember(const char *inName,hx::MarkContext *__inCtx)
{
   if (gCollectTrace)
      __inCtx->SetMember(inName);
}

void MarkPushClass(const char *inName,hx::MarkContext *__inCtx)
{
   if (gCollectTrace)
      __inCtx->PushClass(inName);
}

void MarkPopClass(hx::MarkContext *__inCtx)
{
   if (gCollectTrace)
      __inCtx->PopClass();
}
#endif




struct AutoMarkPush
{
   hx::MarkContext *mCtx;
   AutoMarkPush(hx::MarkContext *ctx, const char *cls, const char *member)
   {
      #ifdef HXCPP_DEBUG
      mCtx = ctx;
      MarkPushClass(cls,mCtx);
      MarkSetMember(member,mCtx);
      #endif
   }
   ~AutoMarkPush()
   {
      #ifdef HXCPP_DEBUG
      MarkPopClass(mCtx);
      #endif
   }
};






#ifdef HXCPP_GC_INCREMENTAL_VERIFY
bool IncrementalOracleMark(void *inPointer, bool inObject, MarkContext *inCtx)
{
   if (!inCtx || !inCtx->incrementalOracle)
      return false;
   if (!inPointer)
      return true;
   unsigned int flags;
   memcpy(&flags, static_cast<char *>(inPointer) - sizeof(flags), sizeof(flags));
   if (flags & HX_GC_CONST_ALLOC_BIT)
      return true;
   auto result = inCtx->oracleVisited.emplace(inPointer, inObject);
   // A backing allocation can also have been encountered through an object
   // pointer. In that case its outgoing members still need the object walk.
   if (inObject && (flags & IMMIX_ALLOC_IS_CONTAINER) && (result.second || !result.first->second))
   {
      result.first->second = true;
      inCtx->oracleQueue.push_back(static_cast<Object *>(inPointer));
   }
   return true;
}
#endif

void MarkAllocUnchecked(void *inPtr,hx::MarkContext *__inCtx)
{
   #ifdef HXCPP_GC_INCREMENTAL_VERIFY
   if (IncrementalOracleMark(inPtr,false,__inCtx)) return;
   #endif
   #ifdef PROFILE_COLLECT
   sAllocMarks++;
   #endif

   size_t ptr_i = ((size_t)inPtr)-sizeof(int);
   unsigned int flags =  *((unsigned int *)ptr_i);

   #ifdef HXCPP_GC_NURSERY
   if (!(flags & 0xff000000))
   {
      #ifdef HX_GC_VERIFY_GENERATIONAL
      if (sGcVerifyGenerational)
      {
         hx::Object *parent = __inCtx->mVerifyParent;
         printf("Nursery alloc escaped generational collection %p, held by %s %p\n", inPtr, HX_GC_VERIFY_TYPE(parent), parent);
         fflush(stdout);
         DebuggerTrap();
      }
      #endif

      int size = flags & 0xffff;
      // Size will be 0 for large allocs -> no need to mark block
      if (size)
      {
         int start = (int)(ptr_i & IMMIX_BLOCK_OFFSET_MASK);
         int startRow = start>>IMMIX_LINE_BITS;
         int blockId = *(BlockIdType *)(ptr_i & IMMIX_BLOCK_BASE_MASK);
         BlockDataInfo *info = (*gBlockInfo)[blockId];

         int endRow = (start + size + sizeof(int) + IMMIX_LINE_LEN-1)>>IMMIX_LINE_BITS;
         *(unsigned int *)ptr_i = flags = (flags & IMMIX_HEADER_PRESERVE) |
                                          (endRow -startRow) |
                                          (size<<IMMIX_ALLOC_SIZE_SHIFT) |
                                          gMarkID;

         unsigned int *pos = info->allocStart + startRow;
         unsigned int val = *pos;
         while(_hx_atomic_compare_exchange((volatile int *)pos, val,val|gImmixStartFlag[start&127]) != val)
            val = *pos;

         #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
         info->mHasSurvivor = true;
         #endif
      }
      else
      {
         // Large nursury object
         ((unsigned char *)inPtr)[HX_ENDIAN_MARK_ID_BYTE] = gByteMarkID;
      }
   }
   else
   #endif
   {
      #ifdef HX_GC_VERIFY_GENERATIONAL
      if (sGcVerifyGenerational && ((unsigned char *)inPtr)[HX_ENDIAN_MARK_ID_BYTE] != gPrevByteMarkID)
      {
         printf("Alloc missed int generational collection %p\n", inPtr);
         DebuggerTrap();
      }
      #endif
      ((unsigned char *)inPtr)[HX_ENDIAN_MARK_ID_BYTE] = gByteMarkID;
   }

   int rows = flags & IMMIX_ALLOC_ROW_COUNT;
   if (rows)
   {
      #if HXCPP_GC_DEBUG_LEVEL>0
      if ( ((ptr_i & IMMIX_BLOCK_OFFSET_MASK)>>IMMIX_LINE_BITS) + rows > IMMIX_LINES) DebuggerTrap();
      #endif

      char *block = (char *)(ptr_i & IMMIX_BLOCK_BASE_MASK);
      char *rowMark = block + ((ptr_i & IMMIX_BLOCK_OFFSET_MASK)>>IMMIX_LINE_BITS);
      // Several objects share a row, so the byte is usually set already -
      // checking first keeps the cache line shared between marker threads
      // instead of ping-ponging it with redundant stores
      if (!*rowMark)
         *rowMark = 1;
      if (rows>1)
      {
         if (!rowMark[1])
            rowMark[1] = 1;
         if (rows>2)
         {
            if (!rowMark[2])
               rowMark[2] = 1;
            if (rows>3)
            {
               if (!rowMark[3])
                  rowMark[3] = 1;
               for(int r=4; r<rows; r++)
                  if (!rowMark[r])
                     rowMark[r]=1;
            }
         }
      }

   // MARK_ROWS_UNCHECKED_END
   }
}

static void CensusCount(hx::Object *inPtr, unsigned int inFlags)
{
   // A large object keeps its size in the word before its header
   size_t size = (inFlags & IMMIX_ALLOC_ROW_COUNT) ? (inFlags & IMMIX_ALLOC_SIZE_MASK) >> IMMIX_ALLOC_SIZE_SHIFT :
                                                     ((unsigned int *)inPtr)[-2];
   CensusEntry &entry = (*sgCensusCounts)[ inPtr->__GetClass().GetPtr() ];
   entry.count++;
   entry.bytes += size;
   if (hx::gCensusVisitor)
      hx::gCensusVisitor(inPtr, size);
}

void MarkObjectAllocUnchecked(hx::Object *inPtr,hx::MarkContext *__inCtx)
{
   #ifdef HXCPP_GC_INCREMENTAL_VERIFY
   if (IncrementalOracleMark(inPtr,true,__inCtx)) return;
   #endif
   size_t ptr_i = ((size_t)inPtr)-sizeof(int);
   unsigned int flags =  *((unsigned int *)ptr_i);
   #ifdef HXCPP_GC_NURSERY
   if (!(flags & 0xff000000))
   {
      #if defined(HX_GC_VERIFY_GENERATIONAL)
         if (sGcVerifyGenerational)
         {
            hx::Object *parent = __inCtx->mVerifyParent;
            printf("Nursery object escaped generational collection %p, a %s held by %s %p\n", inPtr, HX_GC_VERIFY_TYPE(inPtr), HX_GC_VERIFY_TYPE(parent), parent);
            fflush(stdout);
            DebuggerTrap();
         }
      #endif


      int size = flags & 0xffff;
      if (size)
      {
         int start = (int)(ptr_i & IMMIX_BLOCK_OFFSET_MASK);
         int startRow = start>>IMMIX_LINE_BITS;
         int blockId = *(BlockIdType *)(ptr_i & IMMIX_BLOCK_BASE_MASK);
         BlockDataInfo *info = (*gBlockInfo)[blockId];

         int endRow = (start + size + sizeof(int) + IMMIX_LINE_LEN-1)>>IMMIX_LINE_BITS;
         *(unsigned int *)ptr_i = flags = (flags & IMMIX_HEADER_PRESERVE) |
                                          (endRow -startRow) |
                                          (size<<IMMIX_ALLOC_SIZE_SHIFT) |
                                          gMarkID;

         unsigned int *pos = info->allocStart + startRow;
         unsigned int val = *pos;
         while(_hx_atomic_compare_exchange( (volatile int *)pos, val, val|gImmixStartFlag[start&127]) != val)
            val = *pos;
         #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
         info->mHasSurvivor = true;
         #endif
      }
      else
      {
         // Large nursery object
         ((unsigned char *)inPtr)[HX_ENDIAN_MARK_ID_BYTE] = gByteMarkID;
      }
   }
   else
   #endif
      ((unsigned char *)inPtr)[HX_ENDIAN_MARK_ID_BYTE] = gByteMarkID;

   if (sgCensus)
      CensusCount(inPtr, flags);

   #ifdef HXCPP_DEBUG
   // Here, not with the containers, so it counts large objects and objects
   //  without members too
   if (gCollectTrace && gCollectTrace==inPtr->__GetClass().GetPtr())
   {
      gCollectTraceCount++;
      if (gCollectTraceDoPrint)
         __inCtx->Trace();
   }
   #endif

   if (sgEphemeronPending && !sgEphemeronPending->empty())
   {
      auto range = sgEphemeronPending->equal_range(inPtr);
      for(auto i=range.first; i!=range.second; ++i)
         sgEphemeronReady->push_back(i->second);
      sgEphemeronPending->erase(range.first, range.second);
   }

   int rows = flags & IMMIX_ALLOC_ROW_COUNT;
   if (rows)
   {
      char *block = (char *)(ptr_i & IMMIX_BLOCK_BASE_MASK);
      char *rowMark = block + ((ptr_i & IMMIX_BLOCK_OFFSET_MASK)>>IMMIX_LINE_BITS);
      #if HXCPP_GC_DEBUG_LEVEL>0
      if ( ((ptr_i & IMMIX_BLOCK_OFFSET_MASK)>>IMMIX_LINE_BITS) + rows > IMMIX_LINES) DebuggerTrap();
      #endif

      // Check before storing - see MarkAllocUnchecked
      if (!*rowMark)
         *rowMark = 1;
      if (rows>1)
      {
         if (!rowMark[1])
            rowMark[1] = 1;
         if (rows>2)
         {
            if (!rowMark[2])
               rowMark[2] = 1;
            if (rows>3)
            {
               if (!rowMark[3])
                  rowMark[3] = 1;
               for(int r=4; r<rows; r++)
                  if (!rowMark[r])
                     rowMark[r]=1;
            }
         }
      }

      if (flags & IMMIX_ALLOC_IS_CONTAINER)
      {
         #ifdef PROFILE_COLLECT
         sObjectMarks++;
         #endif

         #ifdef HXCPP_DEBUG
            // Recursive mark so stack stays intact..
            #if (HXCPP_GC_DEBUG_LEVEL>0)
            inPtr->__Mark(__inCtx);
            #else
            // Printing a path needs it on the native stack, as far as it goes
            if (!gCollectTrace || !__inCtx->TraceRecurse(inPtr))
               __inCtx->pushObj(inPtr);
            #endif
         #else // Not debug

            // There is a slight performance gain by calling recursively, but you
            //   run the risk of stack overflow.  Also, a parallel mark algorithm could be
            //   done when the marking is stack based.
            //inPtr->__Mark(__inCtx);
            #if (HXCPP_GC_DEBUG_LEVEL>0)
            inPtr->__Mark(__inCtx);
            #else
            __inCtx->pushObj(inPtr);
            #endif
        #endif
      }
      else
      {
         #ifdef PROFILE_COLLECT
         sAllocMarks++;
         #endif
      }

   // MARK_ROWS_UNCHECKED_END
   }
   else if (flags & IMMIX_ALLOC_IS_CONTAINER)
   {
      // Large objects have no rows, but their members still need marking
      #ifdef HXCPP_DEBUG
      if (gCollectTrace && __inCtx->TraceRecurse(inPtr))
         return;
      #endif
      __inCtx->pushObj(inPtr);
   }
}


void MarkObjectArray(hx::Object **inPtr, int inLength, hx::MarkContext *__inCtx)
{
   hx::Object *tmp;

   int extra = inLength & 0x0f;
   for(int i=0;i<extra;i++)
      if (inPtr[i]) MarkObjectAlloc(inPtr[i],__inCtx);
   if (inLength==extra)
      return;

   inLength -= extra;
   hx::Object **ptrI = inPtr + extra;
   hx::Object **end = ptrI + inLength;


   #define MARK_PTR_I \
   tmp = *ptrI++; \
   if (tmp) MarkObjectAlloc(tmp,__inCtx);



   #ifdef HX_MULTI_THREAD_MARKING
   if (sAllThreads && inLength>4096)
   {
      hx::Object **dishOffEnd = end - 4096;
      while(ptrI<end)
      {
         // Are the other threads slacking off?
         if ((sRunningThreads != sAllThreads) && ptrI<dishOffEnd)
         {
            ptrI += sGlobalChunks.takeArrayJob(ptrI, end-ptrI);
         }
         else
         {
            MARK_PTR_I;
            MARK_PTR_I;
            MARK_PTR_I;
            MARK_PTR_I;

            MARK_PTR_I;
            MARK_PTR_I;
            MARK_PTR_I;
            MARK_PTR_I;

            MARK_PTR_I;
            MARK_PTR_I;
            MARK_PTR_I;
            MARK_PTR_I;

            MARK_PTR_I;
            MARK_PTR_I;
            MARK_PTR_I;
            MARK_PTR_I;
         }
      }
   }
   else
   #endif
   {
      while(ptrI<end)
      {
         MARK_PTR_I;
         MARK_PTR_I;
         MARK_PTR_I;
         MARK_PTR_I;

         MARK_PTR_I;
         MARK_PTR_I;
         MARK_PTR_I;
         MARK_PTR_I;

         MARK_PTR_I;
         MARK_PTR_I;
         MARK_PTR_I;
         MARK_PTR_I;

         MARK_PTR_I;
         MARK_PTR_I;
         MARK_PTR_I;
         MARK_PTR_I;
      }
   }
}

void MarkStringArray(String *inPtr, int inLength, hx::MarkContext *__inCtx)
{
   #if 0
   if (MAX_GC_THREADS>1 && sAllThreads && inLength>4096)
   {
      int extra = inLength & 0x0f;
      for(int i=0;i<extra;i++)
         if (inPtr[i]) MarkObjectAlloc(inPtr[i],__inCtx);

      hx::Object **ptrI = inPtr + extra;
      hx::Object **end = ptrI + (inLength& ~0x0f);
      hx::Object **dishOffEnd = end - 4096;
      while(ptrI<end)
      {
         // Are the other threads slacking off?
         if ((sRunningThreads != sAllThreads) && ptrI<dishOffEnd)
         {
            ptrI += sGlobalChunks.takeArrayJob(ptrI, end-ptrI);
         }

         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
         if (*ptrI) MarkObjectAlloc(*ptrI,__inCtx); ptrI++;
      }
   }
   else
   #endif
   {
      for(int i=0;i<inLength;i++)
      {
         const char *str = inPtr[i].raw_ptr();
         HX_MARK_STRING(str);
      }
   }
}

#ifdef HXCPP_DEBUG
#define FILE_SCOPE
#else
#define FILE_SCOPE static
#endif

// --- Roots -------------------------------

// Guards both root sets. GCAddRoot and the others hold it to insert or erase,
// and the collector holds it while it walks the sets (MarkAll, VisitAll): the
// threads that change them do not wait for a collection. A thread that is
// starting adds its root before it registers with the collector, one that is
// ending removes its root after it has unregistered, and native code can add
// or remove a root from a GC-free zone or from a thread hxcpp never knew.
//
// So whoever holds it must not allocate GC memory, reach a safe point, or wait
// for another thread while it does. Then a thread waits for the collector here
// only while a walk runs, and the collector waits only for an insert or an
// erase to finish; neither ever waits for the other to do anything more. The
// collector releases it before the finalizers run, since they may remove roots.
FILE_SCOPE std::mutex* sGCRootLock = nullptr;
typedef std::unordered_set<hx::Object **> RootSet;
// Made in InitAlloc, as the lock is, and never destroyed. A thread's root is
// removed as the thread ends, by hx::thread's thread_local holder, and a
// thread can end while the main thread, having returned from main, runs the
// process's static destructors. A static set was destroyed under it: the
// erase read freed memory and faulted, holding the lock, so the next thread
// to end waited on the lock for ever -- on Windows inside the loader lock,
// which hung the process's exit.
static RootSet *sgRootSet = nullptr;

typedef std::unordered_map<void *,int> OffsetRootSet;
static OffsetRootSet *sgOffsetRootSet=0;

void GCAddRoot(hx::Object **inRoot)
{
   std::lock_guard<std::mutex> lock(*sGCRootLock);
   sgRootSet->insert(inRoot);
}

void GCRemoveRoot(hx::Object **inRoot)
{
   std::lock_guard<std::mutex> lock(*sGCRootLock);
   sgRootSet->erase(inRoot);
}


void GcAddOffsetRoot(void *inRoot, int inOffset)
{
   std::lock_guard<std::mutex> lock(*sGCRootLock);
   if (!sgOffsetRootSet)
      sgOffsetRootSet = new OffsetRootSet();
   (*sgOffsetRootSet)[inRoot] = inOffset;
}

void GcSetOffsetRoot(void *inRoot, int inOffset)
{
   std::lock_guard<std::mutex> lock(*sGCRootLock);
   (*sgOffsetRootSet)[inRoot] = inOffset;
}

void GcRemoveOffsetRoot(void *inRoot)
{
   std::lock_guard<std::mutex> lock(*sGCRootLock);
   OffsetRootSet::iterator r = sgOffsetRootSet->find(inRoot);
   sgOffsetRootSet->erase(r);
}




// --- Finalizers -------------------------------


class WeakRef;
typedef hx::QuickVec<WeakRef *> WeakRefs;

// The finalizer tables and the weak references are each in two parts. An
// entry is new until a collection finds its object alive, and then old: an
// old object dies only in a full collection, so a minor collection looks at
// the new entries alone, and moves those that survive to the old part. It used
// to walk every entry, which made a minor collection with many long-lived
// finalized objects or weak references take as long as their number
// (libhxcpp #302, #314).
FILE_SCOPE std::mutex *sFinalizerLock = 0;
FILE_SCOPE WeakRefs &sWeakRefs = *new WeakRefs();
FILE_SCOPE WeakRefs &sOldWeakRefs = *new WeakRefs();

class WeakRef : public hx::Object
{
public:
   HX_IS_INSTANCE_OF enum { _hx_ClassId = hx::clsIdWeakRef };

   WeakRef(Dynamic inRef)
   {
      mRef = inRef;
      if (mRef.mPtr)
      {
         std::lock_guard<std::mutex> lock(*sFinalizerLock);
         sWeakRefs.push(this);
      }
   }

   // Don't mark our ref !

   #ifdef HXCPP_VISIT_ALLOCS
   void __Visit(hx::VisitContext *__inCtx) HXCPP_OVERRIDE { HX_VISIT_MEMBER(mRef); }
   #endif

   Dynamic mRef;
};



typedef hx::QuickVec<InternalFinalizer *> FinalizerList;

FILE_SCOPE FinalizerList *sgFinalizers = 0;
FILE_SCOPE FinalizerList *sgOldFinalizers = 0;

typedef std::unordered_map<hx::Object *,hx::finalizer> FinalizerMap;
FILE_SCOPE FinalizerMap &sFinalizerMap = *new FinalizerMap();
FILE_SCOPE FinalizerMap &sOldFinalizerMap = *new FinalizerMap();

typedef void (*HaxeFinalizer)(Dynamic);
typedef std::unordered_map<hx::Object *,HaxeFinalizer> HaxeFinalizerMap;
FILE_SCOPE HaxeFinalizerMap &sHaxeFinalizerMap = *new HaxeFinalizerMap();
FILE_SCOPE HaxeFinalizerMap &sOldHaxeFinalizerMap = *new HaxeFinalizerMap();

hx::QuickVec<int> &sFreeObjectIds = *new hx::QuickVec<int>();
typedef std::unordered_map<hx::Object *,int> ObjectIdMap;
typedef hx::QuickVec<hx::Object *> IdObjectMap;
FILE_SCOPE ObjectIdMap &sObjectIdMap = *new ObjectIdMap();
FILE_SCOPE IdObjectMap &sIdObjectMap = *new IdObjectMap();
// The objects given an ID since the last collection, which are all a minor
//  collection can find dead. Emptied by every collection, before any moves
FILE_SCOPE hx::QuickVec<hx::Object *> &sYoungObjectIds = *new hx::QuickVec<hx::Object *>();

typedef std::unordered_set<hx::Object *> MakeZombieSet;
FILE_SCOPE MakeZombieSet &sMakeZombieSet = *new MakeZombieSet();
// The objects given to doNotKill since the last collection, which are all a
//  minor collection can find dead. Emptied by every collection, before any moves
FILE_SCOPE hx::QuickVec<hx::Object *> &sYoungZombieCandidates = *new hx::QuickVec<hx::Object *>();

typedef hx::QuickVec<hx::Object *> ZombieList;
FILE_SCOPE ZombieList &sZombieList = *new ZombieList();

typedef hx::QuickVec<hx::HashRoot *> WeakHashList;
FILE_SCOPE WeakHashList &sWeakHashList = *new WeakHashList();


InternalFinalizer::InternalFinalizer(hx::Object *inObj, finalizer inFinalizer)
{
   mValid = true;
   mObject = inObj;
   mFinalizer = inFinalizer;

   // Ensure this survives generational collect
   std::lock_guard<hx::FastMutex> lock(*gSpecialObjectLock);
   sgFinalizers->push(this);
}

#ifdef HXCPP_VISIT_ALLOCS
void InternalFinalizer::Visit(VisitContext *__inCtx)
{
   HX_VISIT_OBJECT(mObject);
}
#endif

void InternalFinalizer::Detach()
{
   mValid = false;
}

static void MakeZombie(hx::Object *inObj, MarkContext &inContext)
{
   sZombieList.push(inObj);

   // Mark now to prevent secondary zombies...
   inContext.init();
   hx::MarkObjectAlloc(inObj , &inContext );
   inContext.processMarkStack();
}

// inMinor: a minor collection, in which an old object cannot die, so only the
//  registrations made since the last collection are looked at. Walking every
//  one made a large stable set cost tens of milliseconds a minor collection
//  (libhxcpp #383)
void FindZombies(MarkContext &inContext, bool inMinor)
{
   if (inMinor)
   {
      for(int i=0;i<sYoungZombieCandidates.size();i++)
      {
         hx::Object *obj = sYoungZombieCandidates[i];
         unsigned char mark = ((unsigned char *)obj)[HX_ENDIAN_MARK_ID_BYTE];
         // A zombie already, or registered again, is no longer in the set
         if ( mark!=gByteMarkID && sMakeZombieSet.erase(obj) )
            MakeZombie(obj, inContext);
      }
      sYoungZombieCandidates.clear();
      return;
   }
   sYoungZombieCandidates.clear();
   for(MakeZombieSet::iterator i=sMakeZombieSet.begin(); i!=sMakeZombieSet.end(); )
   {
      hx::Object *obj = *i;
      MakeZombieSet::iterator next = i;
      ++next;

      unsigned char mark = ((unsigned char *)obj)[HX_ENDIAN_MARK_ID_BYTE];
      if ( mark!=gByteMarkID )
      {
         sMakeZombieSet.erase(i);
         MakeZombie(obj, inContext);
      }

      i = next;
   }
}

bool IsWeakRefValid(const HX_CHAR *inPtr)
{
   if (((unsigned int *)inPtr)[-1] & HX_GC_CONST_ALLOC_BIT)
      return true;
   unsigned char mark = ((unsigned char *)inPtr)[HX_ENDIAN_MARK_ID_BYTE];

    // Special case of member closure - check if the 'this' pointer is still alive
   return  mark==gByteMarkID;
}

bool IsWeakRefValid(hx::Object *inPtr)
{
   if (((unsigned int *)inPtr)[-1] & HX_GC_CONST_ALLOC_BIT)
      return true;
   unsigned char mark = ((unsigned char *)inPtr)[HX_ENDIAN_MARK_ID_BYTE];

    // Special case of member closure - check if the 'this' pointer is still alive.
    // Adapters return the callable they wrap, so follow the chain to the receiver.
    bool isCurrent = mark==gByteMarkID;
    // A chain of any length is followed: a limit of 16 hops cleared the
    //  callbacks behind 16 adapters while their receiver lived (libhxcpp #364).
    //  A second pointer follows at half speed, so a cycle of dead adapters
    //  catches up with it and ends the walk.
    if ( !isCurrent && inPtr->__GetType()==vtFunction)
    {
        hx::Object *fn = inPtr;
        hx::Object *slow = inPtr;
        bool moveSlow = false;
        while(true)
        {
            hx::Object *thiz = (hx::Object *)fn->__GetHandle();
            if (!thiz)
               break;
            mark = ((unsigned char *)thiz)[HX_ENDIAN_MARK_ID_BYTE];
            if ( (((unsigned int *)thiz)[-1] & HX_GC_CONST_ALLOC_BIT) || mark==gByteMarkID)
            {
               // The object is still alive, so mark the closures that lead to it
               for(hx::Object *link=inPtr; link!=thiz; link=(hx::Object *)link->__GetHandle())
                  MarkAlloc(link,0);
               return true;
            }
            // A String's member closures hand back its characters, not an object
            if (!(((unsigned int *)thiz)[-1] & IMMIX_ALLOC_IS_CONTAINER) || thiz->__GetType()!=vtFunction)
               break;
            fn = thiz;
            if (moveSlow)
               slow = (hx::Object *)slow->__GetHandle();
            moveSlow = !moveSlow;
            if (fn==slow)
               break;
        }
    }
    return isCurrent;
}

static void someHackyFunc(hx::Object *)
{
}

static void (*hackyFunctionCall)(hx::Object *) = someHackyFunc;

struct Finalizable
{
   union
   {
      _hx_member_finalizer member;
      _hx_alloc_finalizer  alloc;
   };
   void *base;
   bool pin;
   bool isMember;

   Finalizable(hx::Object *inBase, _hx_member_finalizer inMember, bool inPin)
   {
      base = inBase;
      member = inMember;
      pin = inPin;
      isMember = true;
   }

   Finalizable(void *inBase, _hx_alloc_finalizer inAlloc, bool inPin)
   {
      base = inBase;
      alloc = inAlloc;
      pin = inPin;
      isMember = false;
   }
   void run()
   {
      if (isMember)
      {
         hx::Object *object = (hx::Object *)base;
         // I can't tell if it is msvc over-optimizing this code, to I am not
         //  quite calling things right, but this seems to fix it...
         hackyFunctionCall(object);
         (object->*member)();
      }
      else
         alloc( base );
   }
};
typedef hx::QuickVec< Finalizable > FinalizableList;
FILE_SCOPE FinalizableList &sFinalizableList = *new FinalizableList();
FILE_SCOPE FinalizableList &sOldFinalizableList = *new FinalizableList();


static double tFinalizers;
static int finalizerCount;
static int localCount;
static int localObjects;
static int localAllocs;
static int rootObjects;
static int rootAllocs;

// The first exception a finalizer threw in this collection. The collection
//  goes on, running the other finalizers, and Collect throws it to its caller
//  once the other threads are released: thrown straight out of the finalizer,
//  it left the collector paused and the collection half done (libhxcpp #358)
std::exception_ptr sFinalizerException;
// The context of the thread whose collection it was, which alone throws it,
//  set once the collection is over
std::atomic<void *> sFinalizerExceptionOwner{ nullptr };

// Throws what a finalizer threw in this thread's last collection. Collect
//  calls it when it holds no lock; a collection made for an allocation, under
//  the state lock, leaves it to the allocator once it has its block
void ThrowFinalizerException()
{
   void *owner = sFinalizerExceptionOwner.load(std::memory_order_acquire);
   if (!owner || owner!=(void *)hx::tlsStackContext)
      return;
   std::exception_ptr thrown = sFinalizerException;
   sFinalizerException = nullptr;
   sFinalizerExceptionOwner.store(nullptr, std::memory_order_relaxed);
   std::rethrow_exception(thrown);
}

template<typename CALL>
static void CallFinalizer(CALL inCall)
{
   try
   {
      inCall();
   }
   catch(...)
   {
      if (!sFinalizerException)
         sFinalizerException = std::current_exception();
   }
}

// Runs the finalizers in inList whose object died. With inOld, the survivors
//  move there; without, they stay
static void RunInternalFinalizers(FinalizerList &inList, FinalizerList *inOld)
{
   int idx = 0;
   while(idx<inList.size())
   {
      InternalFinalizer *f = inList[idx];
      if (!f->mValid)
      {
         inList.qerase(idx);
         delete f;
      }
      else if (((unsigned char *)(f->mObject))[HX_ENDIAN_MARK_ID_BYTE] != gByteMarkID)
      {
         if (f->mFinalizer)
         {
            CallFinalizer([&]{ f->mFinalizer(f->mObject); });
            finalizerCount++;
         }
         inList.qerase(idx);
         delete f;
      }
      else if (inOld)
      {
         inOld->push(f);
         inList.qerase(idx);
      }
      else
      {
         idx++;
      }
   }
}

static void RunFinalizables(FinalizableList &inList, FinalizableList *inOld)
{
   int idx = 0;
   while(idx<inList.size())
   {
      Finalizable &f = inList[idx];
      unsigned char mark = ((unsigned char *)f.base)[HX_ENDIAN_MARK_ID_BYTE];
      if ( mark!=gByteMarkID )
      {
         finalizerCount++;
         // A copy: run may register another, which can move the list
         Finalizable run = f;
         inList.qerase(idx);
         CallFinalizer([&]{ run.run(); });
      }
      else if (inOld)
      {
         inOld->push(f);
         inList.qerase(idx);
      }
      else
         idx++;
   }
}

// Takes the entries of inMap whose object died into outRun. With inOld, the
//  survivors move there
template<typename MAP, typename RUN>
static void TakeDeadFinalizers(MAP &inMap, MAP *inOld, RUN &outRun)
{
   for(typename MAP::iterator i=inMap.begin(); i!=inMap.end(); )
   {
      hx::Object *obj = i->first;
      unsigned char mark = ((unsigned char *)obj)[HX_ENDIAN_MARK_ID_BYTE];
      if ( mark!=gByteMarkID )
      {
         finalizerCount++;
         outRun.push_back( std::make_pair(obj, i->second) );
         i = inMap.erase(i);
      }
      else
         ++i;
   }

   // The two hold different objects, so the larger keeps its nodes and the
   //  smaller is copied into it. Moving each survivor into the old map made a
   //  node and freed one for every entry, which for a batch of new
   //  registrations was most of a collection (libhxcpp #375)
   if (inOld && !inMap.empty())
   {
      if (inMap.size() > inOld->size())
         inMap.swap(*inOld);
      inOld->insert(inMap.begin(), inMap.end());
      inMap.clear();
   }
}

// Clears the references in inList whose target died, and drops those whose
//  reference died. With inOld, the rest move there
static void UpdateWeakRefs(WeakRefs &inList, WeakRefs *inOld)
{
   for(int i=0;i<inList.size();    )
   {
      WeakRef *ref = inList[i];
      unsigned char mark = ((unsigned char *)ref)[HX_ENDIAN_MARK_ID_BYTE];
      // Object itself is gone ...
      if ( mark!=gByteMarkID )
      {
         inList.qerase(i);
         // no i++ ...
      }
      else
      {
         // what about the reference?
         if (!IsWeakRefValid(ref->mRef.mPtr))
         {
            ref->mRef.mPtr = 0;
            inList.qerase(i);
         }
         else if (inOld)
         {
            inOld->push(ref);
            inList.qerase(i);
         }
         else
            i++;
      }
   }
}

// Gives back what a vector holds beyond four times what it uses
template<typename T>
static void ShrinkQuickVec(hx::QuickVec<T> &ioVec)
{
   if (ioVec.mAlloc > 1024 && ioVec.mAlloc/4 > ioVec.size())
   {
      size_t alloc = 10 + ioVec.size()*3/2;
      T *ptr = static_cast<T*>(realloc(ioVec.mPtr, alloc*sizeof(T)));
      if (ptr)
      {
         ioVec.mPtr = ptr;
         ioVec.mAlloc = alloc;
      }
   }
}

// After a burst of IDs whose objects died, the tables kept the storage of
//  their peak: a million left 13 MB of vectors and a million hash buckets
//  (libhxcpp #379). The unused IDs at the top of the range are dropped, with
//  their slots, and storage far beyond what is left goes back. Live objects
//  keep their IDs; one in the middle keeps the free IDs below it
static void TrimObjectIds()
{
   size_t slots = sIdObjectMap.size();
   while(slots>0 && !sIdObjectMap[slots-1])
      slots--;
   if (slots<sIdObjectMap.size())
   {
      sIdObjectMap.setSize(slots);
      int kept = 0;
      for(int i=0;i<sFreeObjectIds.size();i++)
         if (sFreeObjectIds[i]<(int)slots)
            sFreeObjectIds[kept++] = sFreeObjectIds[i];
      sFreeObjectIds.setSize(kept);
   }
   ShrinkQuickVec(sIdObjectMap);
   ShrinkQuickVec(sFreeObjectIds);
   if (sObjectIdMap.bucket_count() > 1024 && sObjectIdMap.bucket_count()/4 > sObjectIdMap.size())
      ObjectIdMap(sObjectIdMap.begin(), sObjectIdMap.end()).swap(sObjectIdMap);
}

// Marks the callbacks in inList that IsWeakRefValid keeps for a live receiver
static void KeepWeakCallbacks(WeakRefs &inList)
{
   for(int i=0;i<inList.size();i++)
   {
      WeakRef *ref = inList[i];
      if ( ((unsigned char *)ref)[HX_ENDIAN_MARK_ID_BYTE]==gByteMarkID && ref->mRef.mPtr)
         IsWeakRefValid(ref->mRef.mPtr);
   }
}

// inMinor: a minor collection, which leaves the old entries alone
void RunFinalizers(bool inMinor)
{
   finalizerCount = 0;

   // A weak reference keeps a callback while its receiver lives, marking it
   //  only when asked. Ask before choosing what to finalize: asked after, in
   //  UpdateWeakRefs, it kept callbacks whose finalizers had run (libhxcpp #376)
   if (!inMinor)
      KeepWeakCallbacks(sOldWeakRefs);
   KeepWeakCallbacks(sWeakRefs);

   if (!inMinor)
      RunInternalFinalizers(*sgOldFinalizers, 0);
   RunInternalFinalizers(*sgFinalizers, sgOldFinalizers);

   if (!inMinor)
      RunFinalizables(sOldFinalizableList, 0);
   RunFinalizables(sFinalizableList, &sOldFinalizableList);

   // Unregister dead entries before invoking their callbacks: a finalizer
   // that itself calls val_gc/_hx_set_finalizer(obj,0) (eg, a close()
   // shared with the manual-cleanup path) erases the node the loop is
   // holding, and one that registers a new finalizer can rehash the map -
   // either invalidates the iterators mid-walk
   static std::vector< std::pair<hx::Object *,hx::finalizer> > sRunNativeFinalizers;
   sRunNativeFinalizers.clear();
   if (!inMinor)
      TakeDeadFinalizers(sOldFinalizerMap, (FinalizerMap *)0, sRunNativeFinalizers);
   TakeDeadFinalizers(sFinalizerMap, &sOldFinalizerMap, sRunNativeFinalizers);
   for(size_t run=0; run<sRunNativeFinalizers.size(); run++)
      CallFinalizer([&]{ (*sRunNativeFinalizers[run].second)(sRunNativeFinalizers[run].first); });
   sRunNativeFinalizers.clear();


   static std::vector< std::pair<hx::Object *,HaxeFinalizer> > sRunHaxeFinalizers;
   sRunHaxeFinalizers.clear();
   if (!inMinor)
      TakeDeadFinalizers(sOldHaxeFinalizerMap, (HaxeFinalizerMap *)0, sRunHaxeFinalizers);
   TakeDeadFinalizers(sHaxeFinalizerMap, &sOldHaxeFinalizerMap, sRunHaxeFinalizers);
   for(size_t run=0; run<sRunHaxeFinalizers.size(); run++)
      CallFinalizer([&]{ (*sRunHaxeFinalizers[run].second)(sRunHaxeFinalizers[run].first); });
   sRunHaxeFinalizers.clear();

   MEM_STAMP(hx::tFinalizers);

   // A minor collection frees no old object, so only the IDs given since the
   //  last collection can belong to a dead one. Walking every registration
   //  made a large stable table cost tens of milliseconds a minor collection
   //  (libhxcpp #357)
   if (inMinor)
   {
      for(int i=0;i<sYoungObjectIds.size();i++)
      {
         hx::Object *o = sYoungObjectIds[i];
         unsigned char mark = ((unsigned char *)o)[HX_ENDIAN_MARK_ID_BYTE];
         if ( mark!=gByteMarkID && !(((unsigned int *)o)[-1] & HX_GC_CONST_ALLOC_BIT))
         {
            ObjectIdMap::iterator id = sObjectIdMap.find(o);
            if (id!=sObjectIdMap.end())
            {
               sFreeObjectIds.push(id->second);
               sIdObjectMap[id->second] = 0;
               sObjectIdMap.erase(id);
            }
         }
      }
   }
   else
   {
      for(ObjectIdMap::iterator i=sObjectIdMap.begin(); i!=sObjectIdMap.end(); )
      {
         ObjectIdMap::iterator next = i;
         next++;

         hx::Object *o = i->first;
         unsigned char mark = ((unsigned char *)o)[HX_ENDIAN_MARK_ID_BYTE];
         if ( mark!=gByteMarkID && !(((unsigned int *)o)[-1] & HX_GC_CONST_ALLOC_BIT))
         {
            sFreeObjectIds.push(i->second);
            sIdObjectMap[i->second] = 0;
            sObjectIdMap.erase(i);
         }

         i = next;
      }
      TrimObjectIds();
   }
   sYoungObjectIds.clear();

   for(int i=0;i<sWeakHashList.size();    )
   {
      HashRoot *ref = sWeakHashList[i];
      unsigned char mark = ((unsigned char *)ref)[HX_ENDIAN_MARK_ID_BYTE];
      // Object itself is gone - no need to worry about that again
      if ( mark!=gByteMarkID )
      {
         sWeakHashList.qerase(i);
         // no i++ ...
      }
      else
      {
         ref->updateAfterGc();
         i++;
      }
   }



   if (!inMinor)
      UpdateWeakRefs(sOldWeakRefs, 0);
   UpdateWeakRefs(sWeakRefs, &sOldWeakRefs);
}

// Callback finalizer on non-abstract type;
void  GCSetFinalizer( hx::Object *obj, hx::finalizer f )
{
   if (!obj)
      throw Dynamic(HX_CSTRING("set_finalizer - invalid null object"));
   if (((unsigned int *)obj)[-1] & HX_GC_CONST_ALLOC_BIT)
      throw Dynamic(HX_CSTRING("set_finalizer - invalid const object"));

   std::lock_guard<hx::FastMutex> lock(*gSpecialObjectLock);
   // A replacement is new, so the old one goes from either part
   sOldFinalizerMap.erase(obj);
   if (f==0)
      sFinalizerMap.erase(obj);
   else
      sFinalizerMap[obj] = f;
}


// Callback finalizer on non-abstract type;
void  GCSetHaxeFinalizer( hx::Object *obj, HaxeFinalizer f )
{
   if (!obj)
      throw Dynamic(HX_CSTRING("set_finalizer - invalid null object"));
   if (((unsigned int *)obj)[-1] & HX_GC_CONST_ALLOC_BIT)
      throw Dynamic(HX_CSTRING("set_finalizer - invalid const object"));

   std::lock_guard<hx::FastMutex> lock(*gSpecialObjectLock);
   // A replacement is new, so the old one goes from either part
   sOldHaxeFinalizerMap.erase(obj);
   if (f==0)
      sHaxeFinalizerMap.erase(obj);
   else
      sHaxeFinalizerMap[obj] = f;
}

void GCDoNotKill(hx::Object *inObj)
{
   if (!inObj)
      throw Dynamic(HX_CSTRING("doNotKill - invalid null object"));
   if (((unsigned int *)inObj)[-1] & HX_GC_CONST_ALLOC_BIT)
      throw Dynamic(HX_CSTRING("doNotKill - invalid const object"));

   std::lock_guard<hx::FastMutex> lock(*gSpecialObjectLock);
   if (sMakeZombieSet.insert(inObj).second)
      sYoungZombieCandidates.push(inObj);
}

hx::Object *GCGetNextZombie()
{
   std::lock_guard<hx::FastMutex> lock(*gSpecialObjectLock);
   if (sZombieList.empty())
      return 0;
   hx::Object *result = sZombieList.pop();
   return result;
}

void RegisterWeakHash(HashBase<String> *inHash)
{
   std::lock_guard<hx::FastMutex> lock(*gSpecialObjectLock);
   sWeakHashList.push(inHash);
}

void RegisterWeakHash(HashBase<Dynamic> *inHash)
{
   std::lock_guard<hx::FastMutex> lock(*gSpecialObjectLock);
   sWeakHashList.push(inHash);
}



void InternalEnableGC(bool inEnable)
{
   #ifdef HXCPP_GC_INCREMENTAL
   if (!inEnable && gIncrementalActive.load()) InternalCollectBytes(true,false);
   #endif
   //printf("Enable %d\n", sgInternalEnable);
   sgInternalEnable = inEnable;
}

bool IsConstAlloc(const void *inData)
{
   unsigned int *header = (unsigned int *)inData;
   return header[-1] & HX_GC_CONST_ALLOC_BIT;
}

void *InternalCreateConstBuffer(const void *inData, size_t inSize, bool inAddStringHash)
{
   bool addHash{ inAddStringHash && inSize > 0 };
   // Two words before the data keep it 8-byte aligned, like the GC heap
   unsigned int* result{ static_cast<unsigned int*>(HxAlloc(inSize + 2 * sizeof(int))) };

   if (addHash)
   {
      unsigned int hash{ 0 };
      if (inData)
         for(int i=0;i<inSize-1;i++)
            hash = hash * 223 + reinterpret_cast<const unsigned char*>(inData)[i];

      *result++ = hash;
      *result++ = HX_GC_CONST_ALLOC_BIT | HX_GC_STRING_HASH;
   }
   else
   {
      *result++ = 0;
      *result++ = HX_GC_CONST_ALLOC_BIT;
   }

   if (inData)
   {
      memcpy(result,inData,inSize);
   }
   else if (inSize)
   {
      ZERO_MEM(result,inSize);
   }

   return result;
}

// Used when the allocation size is zero for a non-null pointer
void *emptyAlloc = InternalCreateConstBuffer(0,0,false);


} // namespace hx


hx::Object *__hxcpp_weak_ref_create(Dynamic inObject)
{
   return new hx::WeakRef(inObject);
}

hx::Object *__hxcpp_weak_ref_get(Dynamic inRef)
{
   #ifdef HXCPP_DEBUG
   hx::WeakRef *ref = dynamic_cast<hx::WeakRef *>(inRef.mPtr);
   #else
   hx::WeakRef *ref = static_cast<hx::WeakRef *>(inRef.mPtr);
   #endif
   return ref->mRef.mPtr;
}


// --- GlobalAllocator -------------------------------------------------------

typedef hx::QuickVec<BlockDataInfo *> BlockList;

typedef hx::QuickVec<unsigned int *> LargeList;

// The first of the recycled large buffers of each size
typedef std::unordered_map<unsigned int, unsigned int *> RecycleLists;

enum MemType { memUnmanaged, memBlock, memLarge };




#ifdef HXCPP_VISIT_ALLOCS
class AllocCounter : public hx::VisitContext
{
public:
   int count;

   AllocCounter() { count = 0; }

   void visitObject(hx::Object **ioPtr) HXCPP_OVERRIDE { count ++; }
   void visitAlloc(void **ioPtr) HXCPP_OVERRIDE { count ++; }
};


#endif


static const size_t sLargeAllocSpaceFloor = 40 << 20;

class GlobalAllocator *sGlobalAlloc = 0;


inline bool SortByBlockPtr(BlockDataInfo *inA, BlockDataInfo *inB)
{
   return inA->mPtr < inB->mPtr;
}

struct MoveBlockJob
{
   BlockList blocks;
   int from;
   int to;

   MoveBlockJob(BlockList &inAllBlocks)
   {
      blocks.setSize(inAllBlocks.size());
      memcpy(&blocks[0], &inAllBlocks[0], inAllBlocks.size()*sizeof(void*));

      std::sort(&blocks[0], &blocks[0]+blocks.size(), SortMoveOrder );
      from = 0;
      to = blocks.size()-1;
   }

   BlockDataInfo *getFrom()
   {
      while(true)
      {
         if (from>=to)
         {
            //printf("Caught up!\n");
            return 0;
         }
         #ifndef HXCPP_GC_DEBUG_ALWAYS_MOVE
         // Pinned / full
         if (blocks[from]->mMoveScore<2)
         {
            //printf("All other blocks good!\n");
            while(from<to)
            {
               //printf("Ignore DEFRAG %p ... %p\n", blocks[from]->mPtr, blocks[from]->mPtr+1);
               from++;
            }
            return 0;
         }
         #endif
         if (blocks[from]->mPinned)
         {
            //printf("PINNED DEFRAG %p ... %p\n", blocks[from]->mPtr, blocks[from]->mPtr+1);
            from++;
         }
         else // Found one...
            break;
      }
      //printf("From block %d (id=%d)\n", from, blocks[from]->mId);
      BlockDataInfo *result = blocks[from++];
      ////printf("FROM DEFRAG %p ... %p\n", result->mPtr, result->mPtr + 1 );
      return result;
   }
   BlockDataInfo *getTo()
   {
      if (from>=to)
      {
         //printf("No more room!\n");
         return 0;
      }
      //printf("To block %d (id=%d)\n", to, blocks[to]->mId);
      BlockDataInfo *result = blocks[to--];
      //printf("TO DEFRAG %p ... %p\n", result->mPtr, result->mPtr + 1 );
      return result;
   }
};


#ifndef HX_MEMORY_H_OVERRIDE

#if 0
static hx::QuickVec<void *> &sBlockPool = *new hx::QuickVec<void *>();
static int gcBlockAllocs = 0;

void *HxAllocGCBlock(size_t size)
{
   if (sBlockPool.size())
      return sBlockPool.pop();
   gcBlockAllocs++;
   GCLOG("===========================================> New Chunk (%d)\n", gcBlockAllocs);
   return HxAlloc(size);
}

void HxFreeGCBlock(void *p)
{
   sBlockPool.push(p);
}

#else

#ifdef HX_GC_FIXED_BLOCKS
unsigned char *chunkData = 0;
void *HxAllocGCBlock(size_t inSize)
{
   if (!chunkData)
   {
      size_t size = 65536;
      size *= IMMIX_BLOCK_SIZE;
      #if defined(HX_WINDOWS) && defined(HXCPP_M64)
      chunkData = (unsigned char *)0x100000000;
      VirtualAlloc(chunkData,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
      #else
      chunkData = (unsigned char *)HxAlloc(size);
      size_t ptr_i = (size_t)(chunkData+0xffff) & ~((size_t)0xffff);
      chunkData = (unsigned char *)ptr_i;
      #endif
      //printf("Using fixed blocks %p...%p\n", chunkData, chunkData+size);
   }
   void *result = chunkData;
   //printf(" %p\n", result);
   chunkData += inSize;
   return result;
}
void HxFreeGCBlock(void *p)
{
   printf("todo - HX_GC_FIXED_BLOCKS %p\n",p);
}
#else
void *HxAllocGCBlock(size_t size) { return HxAlloc(size); }
void HxFreeGCBlock(void *p) { HxFree(p); }
#endif

#endif

#endif // HX_MEMORY_H_OVERRIDE


//#define VERIFY_STACK_READ

#ifdef VERIFY_STACK_READ
// Just have something to do...
int gVerifyVoidCount = 0;
void VerifyStackRead(int *inBottom, int *inTop)
{
   #ifdef HXCPP_STACK_UP
   int *start = inTop-1;
   inTop = inBottom+1;
   inBottom = start;
   #endif

   int n = inTop - inBottom;
   int check = std::min(n,5);
   for(int c=0;c<check;c++)
   {
      if (inBottom[c]==0)
         gVerifyVoidCount++;
      if (inTop[-1-c]==0)
         gVerifyVoidCount++;
   }
}
#endif

// TODO - work out best size based on cache size?
#ifdef HXCPP_GC_BIG_BLOCKS
static int sMinZeroQueueSize = 4;
static int sMaxZeroQueueSize = 16;
#else
static int sMinZeroQueueSize = 8;
static int sMaxZeroQueueSize = 32;
#endif

#define BLOCK_OFSIZE_COUNT 12


#ifdef HXCPP_GC_INCREMENTAL
#include "Incremental.h"
static void IncrementalAutomatic(hx::ImmixAllocator *inAlloc, bool inPressure, size_t inAllocated);
#endif

class GlobalAllocator
{
   enum { LOCAL_POOL_SIZE = 2 };

public:
   GlobalAllocator()
   {
      memset((void *)mNextFreeBlockOfSize,0,sizeof(mNextFreeBlockOfSize));
      mRowsInUse = 0;
      mLargeAllocated = 0;
      mLargeMin = (size_t)-1;
      mLargeMax = 0;
      mLargeOld = 0;
      mLargeOldMin = (size_t)-1;
      mLargeOldMax = 0;
      mLargeOldReleased = false;
      mRecycleSize = 0;
      mRecycleBytes = 0;
      mDroppedBytes = 0;
      mLargeAllocSpace = sLargeAllocSpaceFloor;
      mLargeAllocForceRefresh = mLargeAllocSpace;
      // Start at 1 Meg...
      mTotalAfterLastCollect = 1<<20;
      mCurrentRowsInUse = 0;
      mAllBlocksCount = 0;
      mLiveBytes = 0;
      mGenerationalRetainEstimate = 0.5;
      for(int p=0;p<LOCAL_POOL_SIZE;p++)
         mLocalPool[p] = 0;

      createFreeList();
   }
   void AddLocal(LocalAllocator *inAlloc)
   {
      if (!gThreadStateChangeLock)
      {
         gThreadStateChangeLock = new std::recursive_mutex();
         gSpecialObjectLock = new hx::FastMutex();
      }
      // Until we add ourselves, the collector will not wait
      //  on us - ie, we are assumed ot be in a GC free zone.
      std::lock_guard<std::recursive_mutex> lock(*gThreadStateChangeLock);
      #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
      // Under the lock, so a collection cannot switch modes between the
      //  check and the push
      ((hx::StackContext *)inAlloc)->mOldReferrers =
         sGcMode==gcmGenerational ? hx::sGlobalChunks.alloc() : 0;
      ((hx::StackContext *)inAlloc)->mYoungValues =
         sGcMode==gcmGenerational ? hx::sGlobalChunks.alloc() : 0;
      #endif
      mLocalAllocs.push(inAlloc);
   }

   bool ReturnToPoolLocked(LocalAllocator *inAlloc)
   {
      // Until we add ourselves, the colled will not wait
      //  on us - ie, we are assumed ot be in a GC free zone.
      for(int p=0;p<LOCAL_POOL_SIZE;p++)
      {
         if (!mLocalPool[p])
         {
            mLocalPool[p] = inAlloc;
            return true;
         }
      }
      // I told him we already got one
      return false;
   }

   LocalAllocator *GetPooledAllocator()
   {
      std::lock_guard<std::recursive_mutex> lock(*gThreadStateChangeLock);
      for(int p=0;p<LOCAL_POOL_SIZE;p++)
      {
         if (mLocalPool[p])
         {
            LocalAllocator *result = mLocalPool[p];
            mLocalPool[p] = 0;
            // Drop the range its last thread was allocating from. Until
            //  AttachThread adds it to mLocalAllocs, this allocator is in
            //  neither that list nor the pool, so a collection in between
            //  resets neither it nor the block it points into - it reclaims
            //  that block's free rows for other threads, while this one goes
            //  on allocating from the same rows.
            ClearPooledAlloc(result);
            return result;
         }
      }
      return 0;
   }

   void RemoveLocalLocked(LocalAllocator *inAlloc)
   {
      // You should be in the GC free zone before you call this...
      if (!mLocalAllocs.qerase_val(inAlloc))
      {
         CriticalGCError("LocalAllocator removed without being added");
      }
   }

   // Mutators raise it unlocked, so it only ever grows
   void growLargeAllocSpace(size_t inSpace)
   {
      size_t space = mLargeAllocSpace.load(std::memory_order_relaxed);
      while(inSpace > space && !mLargeAllocSpace.compare_exchange_weak(space, inSpace, std::memory_order_relaxed))
      {
      }
   }

   void FreeLarge(void *inLarge)
   {
      // Only a large allocation is freed here. A block object can be as big:
      // InternalNew rounds 3997 to 3999 bytes up to 4000 after choosing a
      // block, and objects up to IMMIX_MAX_BLOCK_OBJECT_SIZE are allocated in
      // blocks inline. Recycling one queued part of a block to be freed by the
      // next collection (libhxcpp #257). The header tells them apart: a block
      // object's gives its rows, or in the nursery its size, and only a large
      // allocation has the size before it that largeSlot reads past
      unsigned int header = ((unsigned int *)inLarge)[-1];
      #ifdef HXCPP_GC_NURSERY
      if ( (header & 0xff000000) ? (header & IMMIX_ALLOC_ROW_COUNT)!=0 : (header & 0x00ffffff)!=0 )
      #else
      if (header & IMMIX_ALLOC_ROW_COUNT)
      #endif
         return;

      #ifdef HXCPP_GC_INCREMENTAL
      // Pending marking jobs may still refer to replaced backing storage.
      if (hx::gIncrementalActive.load(std::memory_order_relaxed)) return;
      #endif
#ifdef HXCPP_TELEMETRY
       __hxt_gc_free_large(inLarge);
#endif

      unsigned int *blob = ((unsigned int *)inLarge) - 2;
      mLargeListLock.lock();
      // The buffer names its place in the list. Searching for it took time
      //  in the number of other large allocations (libhxcpp #298). One not
      //  in the list, such as one released twice, is left alone
      int idx = largeSlot(blob);
      if (idx<0 || idx>=mLargeList.size() || mLargeList[idx]!=blob)
      {
         mLargeListLock.unlock();
         return;
      }
      ((unsigned char *)inLarge)[HX_ENDIAN_MARK_ID_BYTE] = 0;
      // With no room to recycle it, the sweep frees it instead, so the next
      //  sweep must look at the old allocations too. The cache holds up to
      //  the large allocation allowance
      if (mRecycleBytes.load(std::memory_order_relaxed) + *blob > mLargeAllocSpace.load(std::memory_order_relaxed))
      {
         mLargeOldReleased = true;
         mLargeListLock.unlock();
         return;
      }
      takeLargeAllocated(*blob);
      eraseLarge(idx);
      pushRecycle(blob);
      mLargeListLock.unlock();
   }

   // The next buffer of the same size in the recycle cache, in the first
   //  word of the dead buffer's data
   static unsigned int *&recycleNext(unsigned int *inBlob)
   {
      return *(unsigned int **)(inBlob + 2);
   }

   // Under mLargeListLock or in a collection
   void pushRecycle(unsigned int *inBlob)
   {
      unsigned int *&head = mRecycle[inBlob[0]];
      recycleNext(inBlob) = head;
      head = inBlob;
      mRecycleBytes.fetch_add(inBlob[0], std::memory_order_relaxed);
      mRecycleSize.store(mRecycleSize.load(std::memory_order_relaxed)+1, std::memory_order_relaxed);
   }

   // Under mLargeListLock
   unsigned int *popRecycle(size_t inSize)
   {
      RecycleLists::iterator list = mRecycle.find((unsigned int)inSize);
      if (list==mRecycle.end() || !list->second)
         return 0;
      unsigned int *result = list->second;
      list->second = recycleNext(result);
      mRecycleBytes.fetch_sub(result[0], std::memory_order_relaxed);
      mRecycleSize.store(mRecycleSize.load(std::memory_order_relaxed)-1, std::memory_order_relaxed);
      return result;
   }

   // Returns the data bytes it freed
   static size_t freeRecycle(RecycleLists &ioLists)
   {
      size_t bytes = 0;
      for(RecycleLists::iterator list=ioLists.begin(); list!=ioLists.end(); ++list)
         for(unsigned int *blob=list->second; blob; )
         {
            unsigned int *next = recycleNext(blob);
            bytes += blob[0];
            HxFree(blob);
            blob = next;
         }
      ioLists.clear();
      return bytes;
   }

   // A collection drops the recycle cache it found, and frees it once every
   //  thread runs again: freeing tens of thousands of buffers took tens of
   //  milliseconds while they all waited (libhxcpp #332). A thread of its
   //  own frees each batch, so the thread that collected does not wait for
   //  it either. A compacting collection frees its batch, and any waiting,
   //  before it returns
   void releaseDropped(RecycleLists &ioDropped, bool inNow)
   {
      if (ioDropped.empty() && !inNow)
         return;
      #if !defined(HXCPP_SINGLE_THREADED_APP) && !defined(__EMSCRIPTEN__)
      if (!inNow)
      {
         std::unique_lock<std::mutex> lock(mReleaseLock);
         // At most one batch waits, so a releaser that falls behind does
         //  not let them pile up
         if (!mReleaseQueue.empty())
            inNow = true;
         else if (!mReleaseStarted)
         {
            try
            {
               std::thread thread(SReleaseLoop, this);
               thread.detach();
               mReleaseStarted = true;
            }
            catch(...)
            {
               inNow = true;
            }
         }
         if (!inNow)
         {
            mReleaseQueue.push_back(RecycleLists());
            mReleaseQueue.back().swap(ioDropped);
            mReleaseWake.notify_one();
            return;
         }
      }
      #endif

      mDroppedBytes.fetch_sub(freeRecycle(ioDropped), std::memory_order_relaxed);

      #if !defined(HXCPP_SINGLE_THREADED_APP) && !defined(__EMSCRIPTEN__)
      std::unique_lock<std::mutex> lock(mReleaseLock);
      while(!mReleaseQueue.empty())
      {
         RecycleLists waiting;
         waiting.swap(mReleaseQueue.back());
         mReleaseQueue.pop_back();
         lock.unlock();
         mDroppedBytes.fetch_sub(freeRecycle(waiting), std::memory_order_relaxed);
         lock.lock();
      }
      mReleaseIdle.wait(lock, [this]() { return !mReleaseBusy; });
      #endif
   }

   #if !defined(HXCPP_SINGLE_THREADED_APP) && !defined(__EMSCRIPTEN__)
   static void SReleaseLoop(GlobalAllocator *inAlloc) { inAlloc->ReleaseLoop(); }

   void ReleaseLoop()
   {
      std::unique_lock<std::mutex> lock(mReleaseLock);
      while(true)
      {
         mReleaseWake.wait(lock, [this]() { return !mReleaseQueue.empty(); });
         RecycleLists batch;
         batch.swap(mReleaseQueue.back());
         mReleaseQueue.pop_back();
         mReleaseBusy = true;
         lock.unlock();
         mDroppedBytes.fetch_sub(freeRecycle(batch), std::memory_order_relaxed);
         lock.lock();
         mReleaseBusy = false;
         mReleaseIdle.notify_all();
      }
   }
   #endif

   void *AllocLarge(size_t inSize, bool inClear, bool inContainer=false)
   {
      if (hx::gPauseForCollect)
         __hxcpp_gc_safe_point();

      #ifdef HXCPP_GC_INCREMENTAL
      size_t incrementalAllocated = inSize+mIncrementalAllocated.load(std::memory_order_relaxed);
      if (sgInternalEnable && (hx::gIncrementalActive.load(std::memory_order_relaxed) ||
          incrementalAllocated > mIncrementalStartAt.load(std::memory_order_relaxed)))
         IncrementalAutomatic(HX_CTX_GET,incrementalAllocated > mIncrementalPressureAt.load(std::memory_order_relaxed),incrementalAllocated);
      #endif

      //Should we force a collect ? - the 'large' data are not considered when allocating objects
      // from the blocks, and can 'pile up' between smalll object allocations
      if ((inSize+mLargeAllocated > mLargeAllocForceRefresh) && sgInternalEnable)
      {
         #ifdef SHOW_MEM_EVENTS
         //GCLOG("Large alloc causing collection");
         #endif
         CollectFromThisThread(false,false);
      }

      inSize = (inSize + 3) & ~size_t{ 3 };

      growLargeAllocSpace(inSize<<1);

      unsigned int* result{};
      #ifndef HXCPP_SINGLE_THREADED_APP
      bool do_lock{ true };
      #else
      bool do_lock{ false };
      #endif
      bool isLocked{};


      // Probe the count only - the lists themselves change under the lock.
      //  They are kept by size: a search through every cached buffer for
      //  one of this size took time in the number of other sizes cached
      //  (libhxcpp #300)
      if (mRecycleSize.load(std::memory_order_relaxed))
      {
         if (do_lock && !isLocked)
         {
            mLargeListLock.lock();
            isLocked = true;
         }
         result = popRecycle(inSize);
         // A recycled buffer is out of the shared list now, so it is cleared
         // below without the lock, as a fresh one is; there is no safe point
         // before it is published, so no collection can miss it
         if (do_lock)
         {
            mLargeListLock.unlock();
            isLocked = false;
         }
      }

      if (!result)
         result = static_cast<unsigned int*>(HxAlloc(inSize + sizeof(int)*3));

      if (!result)
      {
         #ifdef SHOW_MEM_EVENTS
         GCLOG("Large alloc failed - forcing collect\n");
         #endif

         if (isLocked)
         {
            mLargeListLock.unlock();
            isLocked = false;
         }

         CollectFromThisThread(true,true);
         result = static_cast<unsigned int*>(HxAlloc(inSize + sizeof(int) * 3));
      }

      if (!result)
      {
         GCLOG("Memory Exhausted!\n");
         DebuggerTrap();
      }

      if (inClear)
         ZERO_MEM(result, inSize + sizeof(int)*2);

      result[0] = static_cast<unsigned int>(inSize);
      #ifdef HXCPP_GC_NURSERY
      result[1] = inContainer ? IMMIX_ALLOC_IS_CONTAINER : 0;
      #else
      result[1] = hx::gMarkID | (inContainer ? IMMIX_ALLOC_IS_CONTAINER : 0);
      #endif

      if (do_lock && !isLocked)
         mLargeListLock.lock();

      largeSlot(result) = mLargeList.size();
      mLargeList.push(result);
      mLargeAllocated += inSize;
      mLargeSinceFull += inSize;

      // Grow-only object-pointer bounds, used to reject conservative-mark
      // candidates without scanning the list.  Stale-wide after frees is
      // fine - the bounds only ever skip definite non-members.
      size_t objPtr = (size_t)(result+2);
      if (objPtr < mLargeMin)
         mLargeMin = objPtr;
      if (objPtr > mLargeMax)
         mLargeMax = objPtr;

      if (do_lock)
         mLargeListLock.unlock();

#ifdef HXCPP_TELEMETRY
      __hxt_gc_alloc(result + 2, inSize);
#endif

      #ifdef HXCPP_GC_INCREMENTAL
      mIncrementalAllocated.fetch_add(inSize,std::memory_order_relaxed);
      if (hx::gIncrementalActive.load(std::memory_order_relaxed)) hx::IncrementalAllocated(result+2,inContainer,HX_CTX_GET);
      #endif
      return result+2;
   }

   // A large allocation's index in mLargeList, in an int after its data
   static int &largeSlot(unsigned int *inBlob)
   {
      return *(int *)((char *)(inBlob + 2) + inBlob[0]);
   }

   // qerase, keeping the slot of the entry it moves
   void qeraseLarge(int inIdx)
   {
      mLargeList.qerase(inIdx);
      if (inIdx<mLargeList.size())
         largeSlot(mLargeList[inIdx]) = inIdx;
   }

   // Removes mLargeList[inIdx], keeping the old allocations before the new.
   //  Under mLargeListLock or in a collection
   void eraseLarge(int inIdx)
   {
      if (inIdx<mLargeOld)
      {
         // The last old one fills the gap, and the last new one its place
         mLargeIndexReady = false;
         mLargeOld--;
         mLargeList[inIdx] = mLargeList[mLargeOld];
         largeSlot(mLargeList[inIdx]) = inIdx;
         qeraseLarge(mLargeOld);
      }
      else
         qeraseLarge(inIdx);
   }

   // Under mLargeListLock or in a collection. Callers can release more
   // managed memory than they reported, which must not wrap the count
   void takeLargeAllocated(size_t inBytes)
   {
      mLargeAllocated -= std::min(inBytes, (size_t)mLargeAllocated);
   }

   void onMemoryRelease(size_t inBytes)
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      mLargeListLock.lock();
      #endif
      takeLargeAllocated(inBytes);
      #ifndef HXCPP_SINGLE_THREADED_APP
      mLargeListLock.unlock();
      #endif
   }

   void onMemoryChange(int inDelta, const char *inWhy)
   {
      if (hx::gPauseForCollect)
         __hxcpp_gc_safe_point();

      if (inDelta>0)
      {
         //Should we force a collect ? - the 'large' data are not considered when allocating objects
         // from the blocks, and can 'pile up' between smalll object allocations
         if (inDelta>0 && (inDelta+mLargeAllocated > mLargeAllocForceRefresh) && sgInternalEnable)
         {
            //GCLOG("onMemoryChange alloc causing collection");
            CollectFromThisThread(false,false);
         }

         // In size_t, which holds any positive int rounded up: in int, a delta
         //  near the top of its range overflowed to an allowance near the size_t
         //  maximum, which the halving at each collection took far too long to
         //  undo. Twice that is clamped for a 32-bit size_t.
         size_t rounded = ((size_t)inDelta + 3) & ~(size_t)3;
         const size_t maxSpace = std::numeric_limits<size_t>::max()/2;
         growLargeAllocSpace(rounded > maxSpace ? maxSpace : rounded<<1);
      }

      #ifndef HXCPP_SINGLE_THREADED_APP
      bool do_lock = true;
      #else
      bool do_lock = false;
      #endif

      if (do_lock)
         mLargeListLock.lock();

      if (inDelta<0)
         takeLargeAllocated((size_t)-(ptrdiff_t)inDelta);
      else
      {
         mLargeAllocated += inDelta;
         mLargeSinceFull += inDelta;
      }

      if (do_lock)
         mLargeListLock.unlock();
   }

   // Gets a block with the 'zeroLock' acquired, which means the zeroing thread
   //  will leave it alone from now on
   //
   // This contains some "lock free" code that triggers the thread sanitizer.
   //  * mOwned can only be changed with the mZeroLock
   //  * AllocMoreBlocks may add to mFreeBlocks meanwhile, into spare capacity so
   //     the buffer does not move. It writes each slot before raising the size,
   //     so this reads the size once, then only below it: a slot read past a
   //     size seen early may not be written yet on a weakly ordered CPU
   //  * it is not critical whether the mNextFreeBlockOfSize is increased since
   //     skipping only leads to a bit of extra searching for the next allocation
   #if defined(__has_feature)
     #if __has_feature(thread_sanitizer)
      __attribute__((no_sanitize("thread")))
     #endif
   #endif
   BlockDataInfo *GetNextFree(size_t inRequiredBytes)
   {
      bool failedLock = true;
      size_t sizeSlot{ inRequiredBytes >> IMMIX_LINE_BITS };
      if (sizeSlot>=BLOCK_OFSIZE_COUNT)
         sizeSlot = BLOCK_OFSIZE_COUNT-1;
      //volatile int &nextFreeBlock = mNextFreeBlockOfSize[sizeSlot];
      int nextFreeBlock{ mNextFreeBlockOfSize[sizeSlot] };
      size_t count{ mFreeBlocks.size() };
      std::atomic_thread_fence(std::memory_order_acquire);
      while(failedLock && nextFreeBlock<count)
      {
         failedLock = false;

         for (int i{ nextFreeBlock }; i < count; i++)
         {
             BlockDataInfo *info = mFreeBlocks[i];
             if (!info->mOwned && info->mMaxHoleSize>=inRequiredBytes)
             {
                // Acquire the zero-lock
                int expected{ 0 };
                if (info->mZeroLock.compare_exchange_strong(expected, 1))
                {
                   // Acquire ownership...
                   if (info->mOwned)
                   {
                      // Someone else got it...
                      info->mZeroLock = 0;
                   }
                   else
                   {
                      info->mOwned = true;

                      #ifdef HXCPP_GC_INCREMENTAL
                      mIncrementalAllocated.fetch_add(info->GetFreeData(),std::memory_order_relaxed);
                      #endif

                      // Increase the mNextFreeBlockOfSize
                      int idx = nextFreeBlock;
                      while(idx<count && mFreeBlocks[idx]->mOwned)
                      {
                         _hx_atomic_compare_exchange(mNextFreeBlockOfSize+sizeSlot, idx, idx+1);
                         idx++;
                      }

                      if (sgThreadPoolJob==tpjAsyncZeroJit)
                      {
                         if (info->mZeroed==ZEROED_THREAD)
                            onZeroedBlockDequeued();
                         #ifdef PROFILE_THREAD_USAGE
                         else
                         {
                            if (!info->mZeroed)
                                sThreadZeroMisses++;
                         }
                         #endif
                       }

                      return info;
                   }
                 }
                 else if (!info->mOwned)
                 {
                    // Zeroing thread is currently working on this block
                    // Go to next one or spin around again
                    failedLock = true;
                 }
             }
         }
      }

      return 0;
   }

   inline size_t GetWorkingMemory()
   {
       return ((size_t)mAllBlocks.size()) << IMMIX_BLOCK_BITS;
   }

   // Making this function "virtual" is actually a (big) performance enhancement!
   // On the iphone, sjlj (set-jump-long-jump) exceptions are used, which incur a
   //  performance overhead.  It seems that the overhead in only in routines that call
   //  malloc/new.  This is not called very often, so the overhead should be minimal.
   //  However, gcc inlines this function!  requiring every alloc the have sjlj overhead.
   //  Making it virtual prevents the overhead.
   virtual bool AllocMoreBlocks(bool &outForceCompact, bool inJustBorrowing)
   {
      enum { newBlockCount = 1<<(IMMIX_BLOCK_GROUP_BITS) };

      #ifdef HXCPP_GC_MOVING
      if (!inJustBorrowing)
      {
         // Do compact next collect sooner
         if (sgTimeToNextTableUpdate>1)
         {
            #ifdef SHOW_FRAGMENTATION
              #ifdef SHOW_MEM_EVENTS_VERBOSE
                GCLOG("  alloc -> enable full collect\n");
              #endif
            #endif
            //sgTimeToNextTableUpdate = 1;
         }
      }
      #endif

      #ifndef HXCPP_GC_BIG_BLOCKS
      // Currently, we only have 2 bytes for a block header
      if (mAllBlocks.size()+newBlockCount >= 0xfffe )
      {
         #if defined(SHOW_MEM_EVENTS) || defined(SHOW_FRAGMENTATION)
         GCLOG("Block id count used - collect");
         #endif
         // The problem is we are out of blocks, not out of memory
         outForceCompact = false;
         return false;
      }
      #endif

      // Find spare group...
      int gid = -1;
      for(int i=0;i<gAllocGroups.size();i++)
         if (!gAllocGroups[i].alloc)
         {
            gid = i;
            break;
         }
      if (gid<0)
      {
         if (!gAllocGroups.safeReserveExtra(1))
         {
            outForceCompact = true;
            return false;
         }
         gid = gAllocGroups.next();
         gAllocGroups[gid].alloc = 0;
      }

      int n = 1<<IMMIX_BLOCK_GROUP_BITS;


      if (!mAllBlocks.safeReserveExtra(n) || !mFreeBlocks.hasExtraCapacity(n))
      {
         outForceCompact = true;
         return false;
      }

      char *chunk = (char *)HxAllocGCBlock( 1<<(IMMIX_BLOCK_GROUP_BITS + IMMIX_BLOCK_BITS) );
      if (!chunk)
      {
         //DebuggerTrap();
         #ifdef SHOW_MEM_EVENTS
         GCLOG("Alloc failed - try collect\n");
         #endif
         outForceCompact = true;
         return false;
      }

      char *aligned = (char *)( (((size_t)chunk) + IMMIX_BLOCK_SIZE-1) & IMMIX_BLOCK_BASE_MASK);
      if (aligned!=chunk)
         n--;
      gAllocGroups[gid].alloc = chunk;
      gAllocGroups[gid].blocks = n;
      gAllocGroups[gid].clear();

      int newSize = mFreeBlocks.size();
      for(int i=0;i<n;i++)
      {
         BlockData *block = (BlockData *)(aligned + i*IMMIX_BLOCK_SIZE);
         BlockDataInfo *info = new BlockDataInfo(gid,block);

         mAllBlocks.push(info);
         // GetNextFree reads mFreeBlocks without the lock held here, so the
         //  slot must be visible before the size that lets it look there. The
         //  capacity was checked above, so the buffer itself does not move.
         mFreeBlocks.mPtr[mFreeBlocks.mSize] = info;
         std::atomic_thread_fence(std::memory_order_release);
         mFreeBlocks.mSize = mFreeBlocks.mSize + 1;
      }
      std::stable_sort(&mAllBlocks[0], &mAllBlocks[0] + mAllBlocks.size(), SortByBlockPtr );
      mAllBlocksCount = mAllBlocks.size();
      for(int i=0;i<BLOCK_OFSIZE_COUNT;i++)
         mNextFreeBlockOfSize[i] = newSize;

      #ifdef HXCPP_GC_VERIFY
      VerifyBlockOrder();
      #endif

      #if defined(SHOW_MEM_EVENTS_VERBOSE) || defined(SHOW_FRAGMENTATION_BLOCKS)
      if (inJustBorrowing)
      {
         GCLOG("Borrow Blocks %d = %d k\n", mAllBlocks.size(), (int)(GetWorkingMemory() >> 10));
      }
      else
      {
         GCLOG("Blocks %d = %d k\n", mAllBlocks.size(), (int)(GetWorkingMemory() >> 10));
      }
      #endif


      return true;
   }

   bool allowMoreBlocks()
   {
      #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
      return sGcMode==gcmFull;
      #else
      return true;
      #endif
   }

   void repoolReclaimedBlock(BlockDataInfo *block)
   {
      // The mMaxHoleSize has changed - possibly return to one of the pools
   }


   BlockDataInfo *GetFreeBlock(size_t inRequiredBytes, hx::ImmixAllocator *inAlloc)
   {
      #ifdef HXCPP_GC_INCREMENTAL
      size_t incrementalAllocated = mIncrementalAllocated.load(std::memory_order_relaxed);
      if (sgInternalEnable && (hx::gIncrementalActive.load(std::memory_order_relaxed) ||
          incrementalAllocated > mIncrementalStartAt.load(std::memory_order_relaxed)))
         IncrementalAutomatic(inAlloc,incrementalAllocated > mIncrementalPressureAt.load(std::memory_order_relaxed),incrementalAllocated);
      #endif
      while(true)
      {
         if (hx::gPauseForCollect)
         {
            hx::PauseForCollect();
            continue;
         }

         BlockDataInfo *result = GetNextFree(inRequiredBytes);
         if (result)
         {
            result->zeroAndUnlock();

            // After zero/reclaim, it might be that the hole size is smaller than we thought.
            if (result->mMaxHoleSize>=inRequiredBytes)
               return result;

            repoolReclaimedBlock(result);
            continue;
         }

         if (hx::gPauseForCollect)
         {
            hx::PauseForCollect();
            continue;
         }

         #ifndef HXCPP_SINGLE_THREADED_APP
         hx::EnterGCFreeZone();
         gThreadStateChangeLock->lock();
         hx::ExitGCFreeZoneLocked();

         result = GetNextFree(inRequiredBytes);
         #endif

         bool forceCompact = false;
         if (!result && allowMoreBlocks() && (!sgInternalEnable || GetWorkingMemory()<sWorkingMemorySize))
         {
            if (AllocMoreBlocks(forceCompact,false))
               result = GetNextFree(inRequiredBytes);
         }

         if (!result)
         {
            CollectFromAlloc(inAlloc, (forceCompact ? collectCompact : 0) | collectLocked | collectFreeIsFragged);
            result = GetNextFree(inRequiredBytes);
         }

         if (!result && !forceCompact)
         {
            // Try with compact this time...
            forceCompact = true;
            CollectFromAlloc(inAlloc, (forceCompact ? collectCompact : 0) | collectLocked | collectFreeIsFragged);
            result = GetNextFree(inRequiredBytes);
         }

         if (!result)
         {
            if (AllocMoreBlocks(forceCompact,false))
            {
               result = GetNextFree(inRequiredBytes);
               // Other threads take free blocks without this lock, so with
               // many allocating at once they can claim every block just
               // added before this thread looks: the heap grew, so try again
               if (!result)
               {
                  #ifndef HXCPP_SINGLE_THREADED_APP
                  gThreadStateChangeLock->unlock();
                  #endif
                  continue;
               }
            }
         }

         if (!result)
         {
            GCLOG("Memory exhausted.\n");
            #ifndef HXCPP_M64
            GCLOG(" try 64 bit build.\n");
            #endif
            #ifndef HXCPP_GC_BIG_BLOCKS
            GCLOG(" try HXCPP_GC_BIG_BLOCKS.\n");
            #endif
            DebuggerTrap();
         }

         // Assume all wil be used
         mCurrentRowsInUse += result->GetFreeRows();

         #ifndef HXCPP_SINGLE_THREADED_APP
         gThreadStateChangeLock->unlock();
         #endif


         result->zeroAndUnlock();

         // After zero/reclaim, it might be that the hole size is smaller than we thought.
         if (result->mMaxHoleSize>=inRequiredBytes)
            return result;

         repoolReclaimedBlock(result);
      }
  }

   #if  defined(HXCPP_VISIT_ALLOCS) // {

  void MoveSpecial(hx::Object *inTo, hx::Object *inFrom, int size)
   {
      #ifdef HX_WATCH
      if (hxInWatchList(reinterpret_cast<uintptr_t>(inFrom)))
         GCLOG("****** watch MOVE from %p\n",inFrom);
      if (hxInWatchList(reinterpret_cast<uintptr_t>(inTo)))
         GCLOG("****** watch MOVE to %p\n",inTo);
      #endif
       // FinalizerList will get visited...

       hx::FinalizerMap *finalizerMaps[] = { &hx::sFinalizerMap, &hx::sOldFinalizerMap };
       for(hx::FinalizerMap *map : finalizerMaps)
       {
          hx::FinalizerMap::iterator i = map->find(inFrom);
          if (i!=map->end())
          {
             hx::finalizer f = i->second;
             map->erase(i);
             (*map)[inTo] = f;
          }
       }

       hx::HaxeFinalizerMap *haxeMaps[] = { &hx::sHaxeFinalizerMap, &hx::sOldHaxeFinalizerMap };
       for(hx::HaxeFinalizerMap *map : haxeMaps)
       {
          hx::HaxeFinalizerMap::iterator h = map->find(inFrom);
          if (h!=map->end())
          {
             hx::HaxeFinalizer f = h->second;
             map->erase(h);
             (*map)[inTo] = f;
          }
       }

       hx::MakeZombieSet::iterator mz = hx::sMakeZombieSet.find(inFrom);
       if (mz!=hx::sMakeZombieSet.end())
       {
          hx::sMakeZombieSet.erase(mz);
          hx::sMakeZombieSet.insert(inTo);
       }

       hx::ObjectIdMap::iterator id = hx::sObjectIdMap.find(inFrom);
       if (id!=hx::sObjectIdMap.end())
       {
          hx::sIdObjectMap[id->second] = inTo;
          hx::sObjectIdMap[inTo] = id->second;
          hx::sObjectIdMap.erase(id);
       }

       // Maybe do something faster with weakmaps

#ifdef HXCPP_TELEMETRY
       //printf(" -- moving %018x to %018x, size %d\n", inFrom, inTo, size);
       __hxt_gc_realloc(inFrom, inTo, size);
#endif

   }



   bool MoveBlocks(MoveBlockJob &inJob,BlockDataStats &ioStats)
   {
      BlockData *dest = 0;
      BlockDataInfo *destInfo = 0;
      unsigned int *destStarts = 0;
      int hole = -1;
      int holes = 0;
      int destPos = 0;
      int destLen = 0;

      int moveObjs = 0;
      int clearedBlocks = 0;

      while(true)
      {
         BlockDataInfo *from = inJob.getFrom();
         if (!from)
            break;
         if (from->calcFragScore()>FRAG_THRESH)
            ioStats.fraggedBlocks--;
         ioStats.rowsInUse -= from->mUsedRows;
         if (!from->isEmpty())
            ioStats.emptyBlocks ++;

         unsigned int *allocStart = from->allocStart;
         #ifdef SHOW_MEM_EVENTS
         //GCLOG("Move from %p (%d x %d)\n", from, from->mUsedRows, from->mHoles );
         #endif

         const unsigned char *rowMarked = from->mPtr->mRowMarked;
         for(int r=IMMIX_HEADER_LINES;r<IMMIX_LINES;r++)
         {
            if (rowMarked[r])
            {
               unsigned int starts = allocStart[r];
               if (!starts)
                  continue;
               for(int i=0;i<32;i++)
               {
                  if ( starts & (1<<i))
                  {
                     unsigned int* row{ reinterpret_cast<unsigned int*>(from->mPtr->row(static_cast<size_t>(r))) };
                     unsigned int &header{ row[i] };

                     if ((header&IMMIX_ALLOC_MARK_ID) == hx::gMarkID)
                     {
                        int size = ((header & IMMIX_ALLOC_SIZE_MASK) >> IMMIX_ALLOC_SIZE_SHIFT);
                        int allocSize = size + sizeof(int);

                        while(allocSize + ALIGN_PADDING(destPos)>destLen)
                        {
                           hole++;
                           if (hole<holes)
                           {
                              destPos = destInfo->mRanges[hole].start;
                              destLen = destInfo->mRanges[hole].length;
                           }
                           else
                           {
                              if (destInfo)
                                 destInfo->makeFull();
                              do
                              {
                                 destInfo = inJob.getTo();
                                 if (!destInfo)
                                    goto all_done;
                              } while(destInfo->mHoles==0);


                              ioStats.rowsInUse += IMMIX_USEFUL_LINES - destInfo->mUsedRows;
                              //destInfo->zero();
                              dest = destInfo->mPtr;
                              destStarts = destInfo->allocStart;

                              hole = 0;
                              holes = destInfo->mHoles;
                              destPos = destInfo->mRanges[hole].start;
                              destLen = destInfo->mRanges[hole].length;
                           }
                        }

                        #ifdef HXCPP_ALIGN_ALLOC
                        // The fit test above reserved space for this alignment
                        // padding (allocSize + ALIGN_PADDING), so the padding must
                        // be charged against destLen as well as destPos - otherwise
                        // destLen over-counts the free space and a later object can
                        // be placed past the end of the block.
                        {
                           int alignPad = ALIGN_PADDING(destPos);
                           destPos += alignPad;
                           destLen -= alignPad;
                        }
                        #endif

                        int startRow = destPos>>IMMIX_LINE_BITS;

                        destStarts[ startRow ] |= hx::gImmixStartFlag[destPos&127];

                        unsigned int *buffer = (unsigned int *)((char *)dest + destPos);

                        unsigned int headerPreserve = header & IMMIX_HEADER_PRESERVE;

                        int end = destPos + allocSize;

                        *buffer++ =  (( (end+(IMMIX_LINE_LEN-1))>>IMMIX_LINE_BITS) -startRow) |
                                        (size<<IMMIX_ALLOC_SIZE_SHIFT) |
                                        headerPreserve |
                                        hx::gMarkID;
                        destPos = end;
                        destLen -= allocSize;

                        unsigned int *src = row + i + 1;

                        MoveSpecial((hx::Object *)buffer,(hx::Object *)src, size);

                        // Result has moved - store movement in original position...
                        memcpy(buffer, src, size);
                        //GCLOG("   move %p -> %p %d (%08x %08x )\n", src, buffer, size, buffer[-1], buffer[1] );

                        // An object, string data among them, need only be 4-byte
                        //  aligned, so the pointer is copied rather than stored
                        memcpy(src, &buffer, sizeof(buffer));
                        header = IMMIX_OBJECT_HAS_MOVED;
                        moveObjs++;

                        starts &= ~(1<<i);
                        if (!starts)
                           break;
                     }
                  }
               }
            }
         }

         all_done:
         if (destInfo)
         {
            // Still have space, which means from finished.
            destInfo->makeFull();
            from->clear();
            clearedBlocks++;
            // Could remove some used rows from stats
         }
         else
         {
            // Partialy cleared, then ran out of to blocks - remove from allocation this round
            if (from)
            {
               // Could maybe be a bit smarter here
               ioStats.rowsInUse += from->mUsedRows;
               from->makeFull();
            }
         }
      }

      #ifdef SHOW_FRAGMENTATION
      GCLOG("Moved %d objects (%d/%d blocks)\n", moveObjs, clearedBlocks, (int)mAllBlocks.size());
      #endif

      if (moveObjs)
      {
         AdjustPointers(0);
      }


      return moveObjs;
   }

   void AdjustPointers(hx::QuickVec<hx::Object *> *inRemembered)
   {
      class AdjustPointer : public hx::VisitContext
      {
         GlobalAllocator *mAlloc;
      public:
         AdjustPointer(GlobalAllocator *inAlloc) : mAlloc(inAlloc) {  }

         void visitObject(hx::Object **ioPtr) HXCPP_OVERRIDE
         {
            if ( ((*(unsigned int **)ioPtr)[-1]) == IMMIX_OBJECT_HAS_MOVED )
            {
               //GCLOG("  patch object to  %p -> %p\n", *ioPtr,  (*(hx::Object ***)ioPtr)[0]);
               // Copied: the moved object may be only 4-byte aligned
               memcpy(ioPtr, *ioPtr, sizeof(*ioPtr));
               //GCLOG("    %08x %08x ...\n", ((int *)(*ioPtr))[0], ((int *)(*ioPtr))[1] );
            }
         }

         void visitAlloc(void **ioPtr) HXCPP_OVERRIDE
         {
            if ( ((*(unsigned int **)ioPtr)[-1]) == IMMIX_OBJECT_HAS_MOVED )
            {
               //GCLOG("  patch reference to  %p -> %p\n", *ioPtr,  (*(void ***)ioPtr)[0]);
               memcpy(ioPtr, *ioPtr, sizeof(*ioPtr));
               //GCLOG("    %08x %08x ...\n", ((int *)(*ioPtr))[0], ((int *)(*ioPtr))[1] );
            }
         }
      };


      AdjustPointer adjust(this);
      VisitAll(&adjust,true, inRemembered);
   }



   int releaseEmptyGroups(BlockDataStats &outStats, size_t releaseSize)
   {
      int released=0;
      int groups=0;
      for(int i=0; i<mAllBlocks.size(); i++ )
      {
         if (!mAllBlocks[i]->isEmpty())
             gAllocGroups[mAllBlocks[i]->mGroupId].isEmpty=false;
      }

      #ifdef HXCPP_GC_VERIFY
      typedef std::pair< void *, void * > ReleasedRange;
      std::vector<ReleasedRange> releasedRange;
      std::vector<int> releasedGids;
      #endif

      for(int i=0;i<gAllocGroups.size();i++)
      {
         GroupInfo &g = gAllocGroups[i];
         if (g.alloc && g.isEmpty && !g.pinned)
         {
            size_t groupBytes = g.blocks << (IMMIX_BLOCK_BITS);

            #ifdef SHOW_MEM_EVENTS_VERBOSE
            GCLOG("Release group %d: %p -> %p\n", i, g.alloc, g.alloc+groupBytes);
            #endif
            #ifdef HXCPP_GC_VERIFY
            releasedRange.push_back( ReleasedRange(g.alloc, g.alloc+groupBytes) );
            releasedGids.push_back(i);
            #endif

            HxFreeGCBlock(g.alloc);
            g.alloc = 0;

            groups++;

            if (groupBytes > releaseSize)
               break;

            releaseSize -= groupBytes;
         }
      }

      // Release blocks, keeping the survivors in address order in one pass:
      // erasing each one moved the rest of the list every time
      int kept = 0;
      for(int i=0; i<mAllBlocks.size(); i++)
      {
         BlockDataInfo *info = mAllBlocks[i];
         if (!gAllocGroups[info->mGroupId].alloc)
         {
            outStats.emptyBlocks--;
            released++;
            info->destroy();
         }
         else
         {
            #ifdef HXCPP_GC_VERIFY
            for(int g=0;g<releasedGids.size();g++)
               if (info->mGroupId == releasedGids[g])
               {
                  printf("Group %d should be released.\n", info->mGroupId);
                  DebuggerTrap();
               }
            #endif
            mAllBlocks[kept++] = info;
         }
      }
      mAllBlocks.setSize(kept);

      #ifdef HXCPP_GC_VERIFY
      for(int i=0;i<mAllBlocks.size();i++)
      {
         BlockDataInfo *info = mAllBlocks[i];
         void *ptr = info->mPtr;
         for(int r=0;r<releasedRange.size();r++)
         if ( ptr>=releasedRange[r].first && ptr<releasedRange[r].second )
         {
            printf("Released block %p(%d:%p) still in list\n", info, info->mId, info->mPtr );
            DebuggerTrap();
         }
      }

      VerifyBlockOrder();
      #endif


      #if defined(SHOW_MEM_EVENTS) || defined(SHOW_FRAGMENTATION)
      GCLOG("Release %d blocks, %d groups,  %s\n", released, groups, formatBytes((size_t)released<<(IMMIX_BLOCK_BITS)).c_str());
      #endif
      return released;
   }


   void calcMoveOrder()
   {
      for(int i=0;i<gAllocGroups.size();i++)
         gAllocGroups[i].clear();

      for(int i=0; i<mAllBlocks.size(); i++ )
      {
         BlockDataInfo &block = *mAllBlocks[i];
         GroupInfo &g = gAllocGroups[block.mGroupId];
         if (block.mPinned)
            g.pinned = true;
         g.usedBytes += block.mUsedBytes;
         g.usedSpace += block.mUsedRows<<IMMIX_LINE_BITS;
      }

      for(int i=0; i<mAllBlocks.size(); i++ )
      {
         BlockDataInfo &block = *mAllBlocks[i];
         GroupInfo &g = gAllocGroups[block.mGroupId];
         // Base on group wasted space (when not pinned)
         block.mMoveScore = g.getMoveScore();
      }
   }

   #endif // } defined(HXCPP_VISIT_ALLOCS)
 
   void *GetIDObject(int inIndex)
   {
      std::lock_guard<hx::FastMutex> lock(*gSpecialObjectLock);
      if (inIndex<0 || (size_t)inIndex>=hx::sIdObjectMap.size())
         return 0;
      return hx::sIdObjectMap[inIndex];
   }

   int GetObjectID(void * inPtr)
   {
      std::lock_guard<hx::FastMutex> lock(*gSpecialObjectLock);
      hx::ObjectIdMap::iterator i = hx::sObjectIdMap.find( (hx::Object *)inPtr );
      if (i!=hx::sObjectIdMap.end())
         return i->second;

      int id = 0;
      if (hx::sFreeObjectIds.size()>0)
      {
         id = hx::sFreeObjectIds.pop();
      }
      else
      {
         id = hx::sObjectIdMap.size();
         hx::sIdObjectMap.push(0);
      }
      hx::sObjectIdMap[(hx::Object *)inPtr] = id;
      hx::sIdObjectMap[id] = (hx::Object *)inPtr;
      hx::sYoungObjectIds.push((hx::Object *)inPtr);
      return id;
   }


   void ClearRowMarks()
   {
      for(int i=0;i<mAllBlocks.size();i++)
         mAllBlocks[i]->clearRowMarks();
   }


   void ClearBlockMarks()
   {
      for(int i=0;i<mAllBlocks.size();i++)
         mAllBlocks[i]->clearBlockMarks();
   }

   #ifdef HXCPP_VISIT_ALLOCS
   void VisitAll(hx::VisitContext *inCtx,bool inMultithread = false,hx::QuickVec<hx::Object *> *inRemembered = 0)
   {
      if (inRemembered)
      {
         int n = inRemembered->size();
         for(int i=0;i<n;i++)
            (*inRemembered)[i]->__Visit(inCtx);
      }
      else if (MAX_GC_THREADS>1 && inMultithread && mAllBlocks.size())
      {
         sThreadVisitContext = inCtx;
         StartThreadJobs(tpjVisitBlocks, mAllBlocks.size(), true);
         sThreadVisitContext = 0;
      }
      else
         for(int i=0;i<mAllBlocks.size();i++)
            mAllBlocks[i]->VisitBlock(inCtx);

      // Large objects are not stored in blocks and never move, but they can hold
      // references into the moved heap, so their members must be visited too -
      // the mAllBlocks pass above only covers block-sized allocations.  The
      // remembered-set path covers these via the write barrier.
      if (!inRemembered)
         for(int i=0;i<mLargeList.size();i++)
         {
            unsigned int *blob = mLargeList[i];
            if ( (blob[1] & IMMIX_ALLOC_MARK_ID) == hx::gMarkID &&
                 (blob[1] & IMMIX_ALLOC_IS_CONTAINER) )
            {
               hx::Object *obj = (hx::Object *)(blob + 2);
               if (*(void **)obj)
                  obj->__Visit(inCtx);
            }
         }

      for(int i=0;i<mLocalAllocs.size();i++)
         VisitLocalAlloc(mLocalAllocs[i], inCtx);

      hx::VisitClassStatics(inCtx);
      #if defined(HXCPP_SCRIPTABLE) && defined(HXCPP_GC_GENERATIONAL_RUNTIME)
      hx::scriptVisitModules(inCtx);
      #endif

      {
      // For the walks only: see sGCRootLock. The visit just patches moved
      // pointers, and waits for nothing.
      std::lock_guard<std::mutex> rootLock(*hx::sGCRootLock);

      for(hx::RootSet::iterator i = hx::sgRootSet->begin(); i!=hx::sgRootSet->end(); ++i)
      {
         hx::Object **obj = *i;
         if (*obj)
            inCtx->visitObject(obj);
      }


      if (hx::sgOffsetRootSet)
         for(hx::OffsetRootSet::iterator i = hx::sgOffsetRootSet->begin(); i!=hx::sgOffsetRootSet->end(); ++i)
         {
            char *ptr = *(char **)(i->first);
            int offset = i->second;
            hx::Object *obj = (hx::Object *)(ptr - offset);

            if (obj)
            {
               hx::Object *obj0 = obj;
               inCtx->visitObject(&obj);
               if (obj!=obj0)
                  *(char **)(i->first) = (char *)(obj) + offset;
            }
         }
      }

      hx::FinalizerList *finalizerLists[] = { hx::sgFinalizers, hx::sgOldFinalizers };
      for(hx::FinalizerList *list : finalizerLists)
         if (list)
            for(int i=0;i<list->size();i++)
               (*list)[i]->Visit(inCtx);

      hx::FinalizableList *finalizables[] = { &hx::sFinalizableList, &hx::sOldFinalizableList };
      for(hx::FinalizableList *list : finalizables)
         for(int i=0;i<list->size();i++)
            inCtx->visitAlloc( &(*list)[i].base );

      for(int i=0;i<hx::sZombieList.size();i++)
         inCtx->visitObject( &hx::sZombieList[i] );

      hx::WeakRefs *weakRefs[] = { &hx::sWeakRefs, &hx::sOldWeakRefs };
      for(hx::WeakRefs *list : weakRefs)
         for(int i=0;i<list->size(); i++)
            inCtx->visitObject( (hx::Object **) &(*list)[i] );

      for(int i=0;i<hx::sWeakHashList.size();i++)
         inCtx->visitObject( (hx::Object **) &hx::sWeakHashList[i] );

   }

   #endif

   void ReclaimAsync(BlockDataStats &outStats)
   {
      while(!sgThreadPoolAbort)
      {
         size_t blockId{ mThreadJobId.fetch_add(1) };
         if (blockId>=mAllBlocks.size())
            break;

         if ( sgThreadPoolJob==tpjReclaimFull)
            mAllBlocks[blockId]->reclaim<true>(&outStats);
         else
            mAllBlocks[blockId]->reclaim<false>(&outStats);
      }
   }

   void CountAsync(BlockDataStats &outStats)
   {
      while(!sgThreadPoolAbort)
      {
         size_t blockId{ mThreadJobId.fetch_add(1) };
         if (blockId>=mAllBlocks.size())
            break;

         mAllBlocks[blockId]->countRows(outStats);
      }
   }


   void GetStatsAsync(BlockDataStats &outStats)
   {
      while(!sgThreadPoolAbort)
      {
         size_t blockId{ mThreadJobId.fetch_add(1) };
         if (blockId>=mAllBlocks.size())
            break;

         mAllBlocks[blockId]->getStats( outStats );
      }
   }

  

   #ifdef HXCPP_VISIT_ALLOCS
   void VisitBlockAsync(hx::VisitContext *inCtx)
   {
      while(!sgThreadPoolAbort)
      {
         size_t blockId{ mThreadJobId.fetch_add(1) };
         if (blockId>=mAllBlocks.size())
            break;

         mAllBlocks[blockId]->VisitBlock(inCtx);
      }
   }
   #endif


   void ZeroAsync()
   {
      while(!sgThreadPoolAbort)
      {
         size_t zeroListId{ mThreadJobId.fetch_add(1) };
         if (zeroListId>=mZeroList.size())
            break;

         BlockDataInfo *info = mZeroList[zeroListId];
         info->tryZero();
      }
   }

   bool ZeroAsyncJit()
   {
      int spinCount = 0;
      while(!sgThreadPoolAbort)
      {
         if (mZeroListQueue>=sMaxZeroQueueSize)
         {
            #ifdef HXCPP_GC_INCREMENTAL
            // A finished cycle left its sweep for later. This thread has the time
            //  now, which the next cycle's first stop would not.
            bool reclaimed = false;
            while(!reclaimed && mReclaimAhead<mZeroList.size())
               reclaimed = mZeroList[mReclaimAhead++]->tryReclaim();
            if (reclaimed)
               continue;
            #endif
            spinCount++;
            if (spinCount<10000)
               // Spin
               continue;
            // Full for now, so sleep...
            return true;
         }
         spinCount = 0;

         // Look at next block...
         size_t zeroListId{ mThreadJobId.fetch_add(1) };
         if (zeroListId>=mZeroList.size())
         {
            // Done, so sleep...
            return true;
         }

         BlockDataInfo *info = mZeroList[zeroListId];
         if (info->tryZero())
         {
            // We zeroed it, so increase queue count
            mZeroListQueue++;
            #ifdef PROFILE_THREAD_USAGE
            sThreadBlockZeroCount++;
            #endif
         }
      }

      return true;
   }

   // Try to maintain between sMinZeroQueueSize and sMaxZeroQueueSize pre-zeroed blocks
   void onZeroedBlockDequeued()
   {
      // Wake the thread?
      if (mZeroListQueue.fetch_sub(1) < sMinZeroQueueSize && !sRunningThreads)
      {
         if (mZeroListQueue + mThreadJobId < mZeroList.size())
         {
            // Wake zeroing thread
            std::lock_guard<std::mutex> l(sThreadPoolLock);
            if (!(sRunningThreads & 0x01))
            {
               #ifdef PROFILE_THREAD_USAGE
               sThreadZeroPokes++;
               #endif
               wakeThreadLocked(0);
            }
         }
      }
   }


   void finishThreadJob(int inId)
   {
      std::lock_guard<std::mutex> l(sThreadPoolLock);
      if (sRunningThreads & (1<<inId))
      {
         sRunningThreads &= ~(1<<inId);
         sLazyThreads = sRunningThreads != sAllThreads;

         if (!sRunningThreads)
            SignalThreadPool(sThreadJobDone,sThreadJobDoneSleeping);
      }
      else
      {
         printf("Finishe non-runnning thread?\n");
         DebuggerTrap();
      }
   }

   void waitForThreadWake(int inId)
   {
        std::lock_guard<std::mutex> l(sThreadPoolLock);
        int count = 0;

        // May be woken multiple times if sRunningThreads is set to 0 then 1 before we sleep
        sThreadSleeping[inId] = true;
        // Spurious wake?
        sThreadWake[inId]->wait(sThreadPoolLock, [&inId]() { return sRunningThreads & (1 << inId); });
        
        sThreadSleeping[inId] = false;
        sWorkerStarted[inId] = true;
   }


   void ThreadLoop(int inId)
   {
      hx::MarkContext context(inId);

      while(true)
      {
         waitForThreadWake(inId);

         #ifdef HXCPP_GC_VERIFY
         if (! (sRunningThreads & (1<<inId)) )
            printf("Bad running threads!\n");
         #endif

         if (sgThreadPoolJob==tpjMark)
         {
            context.processMarkStack();
         }
         else if (sgThreadPoolJob==tpjAsyncZeroJit)
         {
            #ifdef PROFILE_THREAD_USAGE
            sThreadZeroWaits++;
            #endif
            if (ZeroAsyncJit())
               finishThreadJob(inId);
         }
         else
         {
            if (sgThreadPoolJob==tpjReclaimFull || sgThreadPoolJob==tpjReclaim)
               ReclaimAsync(sThreadBlockDataStats[inId]);

            else if (sgThreadPoolJob==tpjCountRows)
               CountAsync(sThreadBlockDataStats[inId]);

            else if (sgThreadPoolJob==tpjGetStats)
               GetStatsAsync(sThreadBlockDataStats[inId]);

            #ifdef HXCPP_VISIT_ALLOCS
            else if (sgThreadPoolJob==tpjVisitBlocks)
               VisitBlockAsync(sThreadVisitContext);
            #endif

            else if (sgThreadPoolJob==tpjAsyncZero)
               ZeroAsync();

            finishThreadJob(inId);
         }
      }
   }

   static void SThreadLoop( void *inInfo )
   {
      sGlobalAlloc->ThreadLoop((int)(size_t)inInfo);
   }

   void CreateWorker(int inId)
   {
      void *info = (void *)(size_t)inId;

      #if defined(__EMSCRIPTEN__)
         // Only one thread
      #else
         sThreadWake[inId] = new std::condition_variable_any();
         if (0 == inId)
         {
             sThreadJobDone = new std::condition_variable_any();
         }

         sThreadSleeping[inId] = false;

         std::thread thread(SThreadLoop, info);

         thread.detach();
      #endif
   }

   #ifdef HXCPP_GC_INCREMENTAL
   // Ends a background job only if no worker is running it
   bool TryStopThreadJobs()
   {
      if (!sAllThreads)
         return true;
      std::lock_guard<std::mutex> l(sThreadPoolLock);
      if (sRunningThreads)
         return false;
      sgThreadPoolAbort = false;
      sAllThreads = 0;
      sgThreadPoolJob = tpjNone;
      sLazyThreads = 0;
      return true;
   }

   // The pool and this thread mark together
   void MarkSliceRound(hx::MarkContext *inCtx)
   {
      inCtx->releaseJobs();
      StartThreadJobs(tpjMark, MAX_GC_THREADS, false);
      inCtx->processMarkStack();
      JoinStartedWorkers();
   }
   #endif

   // Ends a job the collecting thread has been working on beside the pool. A
   //  worker the scheduler has not run yet has taken no work, so it is not waited for.
   void JoinStartedWorkers()
   {
      std::lock_guard<std::mutex> l(sThreadPoolLock);
      // No more lazy wakes for this job
      sgThreadCount = 0;
      for(int i=0;i<MAX_GC_THREADS;i++)
         if (!sWorkerStarted[i])
            sRunningThreads &= ~(1<<i);
      if (sRunningThreads)
      {
         sThreadJobDoneSleeping = true;
         sThreadJobDone->wait(sThreadPoolLock, []() { return sRunningThreads == false; });
         sThreadJobDoneSleeping = false;
      }
      sAllThreads = 0;
      sgThreadPoolJob = tpjNone;
      sLazyThreads = 0;
   }

   void StopThreadJobs(bool inKill)
   {
      bool maybeExtraWake = false;

      if (sAllThreads)
      {
         if (inKill)
         {
            sgThreadPoolAbort = true;
            if (sgThreadPoolJob==tpjAsyncZeroJit)
            {
               std::lock_guard<std::mutex> l(sThreadPoolLock);
               // Thread will be waiting, but not finished
               if (sRunningThreads & 0x1)
               {
                  sgThreadPoolJob = tpjNone;
                  maybeExtraWake = true;
                  wakeThreadLocked(0);
               }
            }
         }


         std::lock_guard<std::mutex> l(sThreadPoolLock);
         sThreadJobDoneSleeping = true;
         sThreadJobDone->wait(sThreadPoolLock, []() { return sRunningThreads == false; });
         sThreadJobDoneSleeping = false;
         sgThreadPoolAbort = false;
         sAllThreads = 0;
         sgThreadPoolJob = tpjNone;
         sLazyThreads = 0;
      }
   }


   void StartThreadJobs(ThreadPoolJob inJob, int inWorkers, bool inWait, int inThreadLimit = -1)
   {
      mThreadJobId = 0;

      if (!inWorkers)
         return;

      if (!sThreadPoolInit)
      {
         sThreadPoolInit = true;
         for(int i=0;i<MAX_GC_THREADS;i++)
            CreateWorker(i);
      }

      std::lock_guard<std::mutex> l(sThreadPoolLock);

      sgThreadPoolJob = inJob;

      sgThreadCount = inThreadLimit<0 ? MAX_GC_THREADS : std::min((int)MAX_GC_THREADS, inThreadLimit) ;

      int start = std::min(inWorkers, sgThreadCount );

      sAllThreads = (1<<sgThreadCount) - 1;

      sRunningThreads = (1<<start) - 1;

      sLazyThreads = sRunningThreads != sAllThreads;

      for(int i=0;i<start;i++)
      {
         sWorkerStarted[i] = false;
         SignalThreadPool(sThreadWake[i],sThreadSleeping[i]);
      }

      if (inWait)
      {
         // Join the workers...
         sThreadJobDoneSleeping = true;
         sThreadJobDone->wait(sThreadPoolLock, []() { return sRunningThreads == false; });
         sThreadJobDoneSleeping = false;

         sAllThreads = 0;
         sgThreadPoolJob = tpjNone;
         sLazyThreads = 0;

         if (sRunningThreads)
         {
            printf("Bad thread stop %d\n", sRunningThreads.load());
            DebuggerTrap();
         }
      }
   }


   #if defined(SHOW_FRAGMENTATION) || defined(SHOW_MEM_EVENTS)
   std::string formatBytes(size_t bytes)
   {
      size_t k = 1<<10;
      size_t meg = 1<<20;

      char strBuf[100];
      if (bytes<k)
         snprintf(strBuf,sizeof(strBuf),"%d", (int)bytes);
      else if (bytes<meg)
         snprintf(strBuf,sizeof(strBuf),"%.2fk", (double)bytes/k);
      else
         snprintf(strBuf,sizeof(strBuf),"%.2fmb", (double)bytes/meg);
      return strBuf;
   }
   #endif


   double tMarkInit;
   double tMarkLocal;
   double tMarkLocalEnd;
   double tMarked;
   #ifdef HXCPP_GC_INCREMENTAL
   static void IncrementalMarkWorkers(hx::MarkContext *inCtx, void *inData)
   {
      if (!inCtx->hasWork())
         return;
      GlobalAllocator *owner = static_cast<GlobalAllocator *>(inData);
#ifdef HX_MULTI_THREAD_MARKING
      // A zeroing job left running has the pool
      if (MAX_GC_THREADS > 1 && !owner->mZeroingLeft)
         owner->MarkSliceRound(inCtx);
      else
#endif
         inCtx->processMarkStack();
   }
#endif
   bool MarkAll(bool inGenerational, bool inResume=false)
   {
      // The old allocations' index stays: only a change to them drops it
      mLargeLookups = 0;
      mYoungLargeIndexReady = false;
      mYoungLargeLookups = 0;

      if (!inResume) {

      if (!inGenerational)
      {
         hx::gPrevByteMarkID = hx::gByteMarkID;

         // The most-significant header byte looks like:
         // C nH Odd Even  c c c c
         //  C = "is const alloc bit" - in this case Odd and Even will be false
         // nH = non-hashed const string bit
         // Odd = true if cycle is odd
         // Even = true if cycle is even
         // c c c c = 4 bit cycle code
         //
         hx::gPrevMarkIdMask = ((~hx::gMarkID) & 0x30000000) | HX_GC_CONST_ALLOC_BIT;

         // 4 bits of cycle
         gByteMarkID = (gByteMarkID + 1) & 0x0f;
         if (gByteMarkID & 0x1)
            gByteMarkID |= 0x20;
         else
            gByteMarkID |= 0x10;

         hx::gMarkID = gByteMarkID << 24;
         hx::gMarkIDWithContainer = (gByteMarkID << 24) | IMMIX_ALLOC_IS_CONTAINER;
         gRememberedByteMarkID = gByteMarkID | HX_GC_REMEMBERED;

         #ifdef HX_WATCH
         GCLOG(" non-gen mark byte -> %02x\n", hx::gByteMarkID);
         #endif
         gBlockStack = 0;

         ClearRowMarks();
      }
      else
      {
         #ifdef HX_WATCH
         GCLOG(" generational mark byte -> %02x\n", hx::gByteMarkID);
         #endif
         ClearBlockMarks();
      }

      #ifdef HXCPP_GC_MOVING
      hx::FinalizableList *finalizables[] = { &hx::sFinalizableList, &hx::sOldFinalizableList };
      for(hx::FinalizableList *list : finalizables)
         for(int i=0;i<list->size();i++)
         {
            hx::Finalizable &f = (*list)[i];
            uintptr_t base = reinterpret_cast<uintptr_t>(f.base);
            if (f.pin && GetMemType(base)==memBlock)
               (*gBlockInfo)[reinterpret_cast<BlockData*>(base & IMMIX_BLOCK_BASE_MASK)->mId]->pin();
         }
      #endif

      MEM_STAMP(tMarkInit);
      }

      #ifdef PROFILE_THREAD_USAGE
      for(int i=-1;i<MAX_GC_THREADS;i++)
         sThreadChunkPushCount = sThreadChunkWakes = sThreadMarkCount[i] = sThreadArrayMarkCount[i] = 0;
      #endif



      mMarker.init();
      // Only a full mark reaches every live object
      CensusRequest *census = inGenerational ? nullptr : sgCensusRequest.load();
      sgCensus = census && !census->done;
      if (sgCensus)
      {
         sgCensusCounts = &census->counts;
         sgCensusDeferred = &census->deferred;
         sgCensusCounts->clear();
      }
      #ifdef HXCPP_DEBUG
      // Gc.trace counts as a census does, so it defers the same marks
      static std::vector< std::pair<void *,bool> > *sTraceDeferred = new std::vector< std::pair<void *,bool> >();
      if (gCollectTrace && !sgCensus)
         sgCensusDeferred = sTraceDeferred;
      #endif

      hx::MarkClassStatics(&mMarker);
      #if defined(HXCPP_SCRIPTABLE) && defined(HXCPP_GC_GENERATIONAL_RUNTIME)
      hx::scriptMarkModules(&mMarker, inGenerational);
      #endif

      {
      hx::AutoMarkPush info(&mMarker,"Roots","root");
      // For the walks only: see sGCRootLock. Marking here waits for nothing
      // and touches no root; the marking threads start, and the finalizers
      // run, after it is released.
      std::lock_guard<std::mutex> rootLock(*hx::sGCRootLock);

      for(hx::RootSet::iterator i = hx::sgRootSet->begin(); i!=hx::sgRootSet->end(); ++i)
      {
         hx::Object *&obj = **i;
         if (obj)
            hx::MarkObjectAlloc(obj , &mMarker );
      }

      if (hx::sgOffsetRootSet)
         for(hx::OffsetRootSet::iterator i = hx::sgOffsetRootSet->begin(); i!=hx::sgOffsetRootSet->end(); ++i)
         {
            char *ptr = *(char **)(i->first);
            int offset = i->second;
            hx::Object *obj = (hx::Object *)(ptr - offset);

            if (obj)
               hx::MarkObjectAlloc(obj , &mMarker );
         }
      } // automark

      #ifdef PROFILE_COLLECT
      hx::rootObjects = sObjectMarks;
      hx::rootAllocs = sAllocMarks;
      #endif


      #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
      {
      hx::AutoMarkPush info(&mMarker,"Young array values","young");
      // The first mark of a collection takes the log: a full one has no use
      //  for it, and one that follows a minor mark has marked these already
      hx::MarkYoungValues(inGenerational, &mMarker);
      }
      #endif

      {
      hx::AutoMarkPush info(&mMarker,"Zombies","zombie");
      // Mark zombies too....
      for(int i=0;i<hx::sZombieList.size();i++)
         hx::MarkObjectAlloc(hx::sZombieList[i] , &mMarker );
      } // automark

      MEM_STAMP(tMarkLocal);
      hx::localCount = 0;

      mMarker.isGenerational = inGenerational;

      // Mark local stacks
      for(int i=0;i<mLocalAllocs.size();i++)
         MarkLocalAlloc(mLocalAllocs[i] , &mMarker);

      #ifdef PROFILE_COLLECT
      hx::localObjects = sObjectMarks;
      hx::localAllocs = sAllocMarks;
      #endif

      MEM_STAMP(tMarkLocalEnd);

      #ifdef HXCPP_GC_INCREMENTAL
      if (hx::IncrementalData().tracing) {
         if (!hx::IncrementalDrain(&mMarker,IncrementalMarkWorkers,this)) return false;
         // Avoid adding weak processing and reclamation to a full marking
         // slice. Roots and mutation logs are checked again at the next stop
         // before the cycle is allowed to finish.
         if (hx::IncrementalData().deadline > 0 && !hx::IncrementalData().finishPending)
         {
            hx::IncrementalData().finishPending = true;
            return false;
         }
         mMarker.init();
         for (int i=0;i<mLocalAllocs.size();i++)
            if (!hx::IncrementalRevisitScopes((hx::StackContext *)mLocalAllocs[i],&mMarker))
               return false;
         if (!hx::IncrementalDrain(&mMarker,IncrementalMarkWorkers,this)) return false;
         // Weak processing and reclamation use the pool
         StopZeroingLeft();
         hx::IncrementalData().tracing = false;
         hx::gIncrementalAllocationsWhite = false;
      }
      else
      #endif
      {
      #ifdef HX_MULTI_THREAD_MARKING
      // Gc.trace counts objects as it marks them, so like a census it
      //  marks on one thread, where nothing is marked twice
      #ifdef HXCPP_DEBUG
      bool tracing = gCollectTrace;
      #else
      bool tracing = false;
      #endif
      if (!sgCensus && !tracing)
      {
         mMarker.releaseJobs();

         // Unleash the workers, if the roots and stacks left them anything:
         //  a minor collection that marked nothing new paid for waking and
         //  joining every one of them, most of its time (libhxcpp #377)
         if (hx::sGlobalChunks.processList.load())
            StartThreadJobs(tpjMark, MAX_GC_THREADS, true);
      }
      else
      #endif
         mMarker.processMarkStack();
      }

      if (sgCensusDeferred)
         MarkCensusDeferred();
      #ifdef HXCPP_DEBUG
      if (!sgCensus)
         sgCensusDeferred = 0;
      #endif



      MEM_STAMP(tMarked);

      #ifdef HXCPP_GC_INCREMENTAL_VERIFY
      #ifdef HXCPP_GC_INCREMENTAL_TEST
      if (hx::IncrementalData().omit) {
         reinterpret_cast<unsigned char *>(hx::IncrementalData().omit)[HX_ENDIAN_MARK_ID_BYTE] = hx::gPrevByteMarkID;
         hx::IncrementalData().omit = nullptr;
      }
      #endif
      if (hx::gIncrementalActive.load(std::memory_order_relaxed)) VerifyIncremental();
      #endif

      // An object that only a weak hash value keeps is not a zombie while its key lives,
      // and a zombie can keep more keys alive
      MarkWeakHashValues();
      hx::FindZombies(mMarker, inGenerational);
      MarkWeakHashValues();

      if (sgCensus)
      {
         census->done = true;
         sgCensus = false;
         sgCensusCounts = 0;
         sgCensusDeferred = 0;
      }

      hx::RunFinalizers(inGenerational);

      // Finalizers and weak reference checks may still look at dead script objects
      #ifdef HXCPP_SCRIPTABLE
      hx::scriptFreeModules();
      #endif

      #ifdef HXCPP_GC_VERIFY
      for(int i=0;i<mAllBlocks.size();i++)
         mAllBlocks[i]->verify("After mark");
      #endif

      #ifdef HX_WATCH
      for (uintptr_t *w = hxWatchList; *w; w++)
      {
         uintptr_t watch = *w;
         GCLOG("********* Watch mark : %p %08x\n", reinterpret_cast<void*>(watch), (reinterpret_cast<unsigned int*>(watch))[-1]);
         GCLOG(" ******** is marked  : %d\n", (reinterpret_cast<unsigned char*>(watch)[HX_ENDIAN_MARK_ID_BYTE] == gByteMarkID));
      }
      #endif
      return true;
   }


   
   #ifdef HXCPP_GC_INCREMENTAL_VERIFY
   void VerifyIncremental()
   {
      hx::MarkContext oracle;
      oracle.incrementalOracle = true;
      hx::MarkClassStatics(&oracle);
      {
         std::lock_guard<std::mutex> lock(*hx::sGCRootLock);
         for (hx::Object **slot : *hx::sgRootSet)
            if (*slot)
               hx::MarkObjectAlloc(*slot, &oracle);
         if (hx::sgOffsetRootSet)
            for (const auto &entry : *hx::sgOffsetRootSet)
            {
               char *ptr = *static_cast<char **>(entry.first);
               if (ptr)
                  hx::MarkObjectAlloc(reinterpret_cast<hx::Object *>(ptr - entry.second), &oracle);
            }
      }
      for (int i = 0; i < hx::sZombieList.size(); i++)
         hx::MarkObjectAlloc(hx::sZombieList[i], &oracle);
      for (int i = 0; i < mLocalAllocs.size(); i++)
         MarkLocalAlloc(mLocalAllocs[i], &oracle);
      while (!oracle.oracleQueue.empty())
      {
         hx::Object *object = oracle.oracleQueue.back();
         oracle.oracleQueue.pop_back();
         object->__Mark(&oracle);
      }
      for (const auto &entry : oracle.oracleVisited)
      {
         uintptr_t header = reinterpret_cast<uintptr_t>(entry.first) - sizeof(unsigned int);
         unsigned int flags = *reinterpret_cast<unsigned int *>(header);
         bool valid = (flags & IMMIX_ALLOC_MARK_ID) == hx::gMarkID;
         unsigned int rows = flags & IMMIX_ALLOC_ROW_COUNT;
         unsigned char *row = reinterpret_cast<unsigned char *>(header & IMMIX_BLOCK_BASE_MASK) +
                              ((header & IMMIX_BLOCK_OFFSET_MASK) >> IMMIX_LINE_BITS);
         for (unsigned int r = 0; r < rows; r++)
            valid = valid && row[r];
         if (!valid)
         {
            fprintf(stderr, "Incremental GC omitted reachable %s %p flags=%08x epoch=%08x rows=%u first=%u vtable=%p\n", entry.second ? "object" : "allocation", entry.first, flags, hx::gMarkID, rows, rows ? *row : 0, entry.second ? *static_cast<void **>(entry.first) : nullptr);
            fflush(stderr);
            abort();
         }
      }
   }
#endif

   // The stack values a census put aside: what a reference reached is marked, and
   // counted, by now, and the rest is marked as the stack scan would have
   void MarkCensusDeferred()
   {
      hx::MarkContext *__inCtx = &mMarker;
      for(size_t i=0;i<sgCensusDeferred->size();i++)
      {
         void *ptr = (*sgCensusDeferred)[i].first;
         if ((*sgCensusDeferred)[i].second)
         {
            unsigned char &mark = ((unsigned char *)ptr)[HX_ENDIAN_MARK_ID_BYTE];
            if (mark != gByteMarkID)
               mark = static_cast<unsigned char>(gByteMarkID);
         }
         else
            HX_MARK_STRING(ptr);
      }
      sgCensusDeferred->clear();
   }

   // Weak-key hashes are ephemerons: a value is reachable only while the hash and its
   // key are. A value can make keys in other weak hashes reachable, so this repeats
   // until nothing more is marked. What a hash's values reach is marked before the
   // next hash is looked at, so a chain of weak maps whose keys lead from one to the
   // next resolves in one pass when the scan goes along it, and the scan alternates
   // direction, so one registered the other way resolves in the next (libhxcpp #203).
   // Before, what a pass found was marked only at the end of the pass, by restarting
   // the marking threads, so such a chain gave up one link per pass.
   //
   // Within one hash that did not help: a key that only another entry's value
   // leads to was found marked in the next pass over the whole table, so a
   // chain of N such entries took N passes (libhxcpp #378). An entry whose
   // object key is not marked is now remembered by its key while the values
   // are marked, and once the marking reaches the key its value is marked
   // too, in the same pass. String keys still wait for the next pass.
   void MarkWeakHashValues()
   {
      EphemeronPending pending;
      std::vector<EphemeronEntry> ready;
      sgEphemeronReady = &ready;
      bool marked;
      bool forward = true;
      do
      {
         marked = false;
         pending.clear();
         sgEphemeronPending = &pending;
         int count = hx::sWeakHashList.size();
         for(int i=0;i<count;i++)
         {
            hx::HashRoot *hash = hx::sWeakHashList[forward ? i : count-1-i];
            if (hx::IsWeakRefValid(hash))
            {
               mMarker.init();
               if (hash->markWeakValues(&mMarker))
               {
                  marked = true;
                  mMarker.processMarkStack();
               }
               while(!ready.empty())
               {
                  marked = true;
                  mMarker.init();
                  for(size_t r=0;r<ready.size();r++)
                     ready[r].hash->markWeakElement(ready[r].element, &mMarker);
                  ready.clear();
                  mMarker.processMarkStack();
               }
            }
         }
         forward = !forward;
      }
      while(marked);
      sgEphemeronPending = 0;
      sgEphemeronReady = 0;
   }

   #ifdef HX_GC_VERIFY_ALLOC_START
   void verifyAllocStart()
   {
      for(int i=1;i<mAllBlocks.size();i++)
         mAllBlocks[i]->verifyAllocStart();
   }
   #endif

   #ifdef HXCPP_GC_VERIFY
   void VerifyBlockOrder()
   {
      for(int i=1;i<mAllBlocks.size();i++)
      {
         if ( mAllBlocks[i-1]->mPtr >= mAllBlocks[i]->mPtr)
         {
            printf("Bad block order block[%d]=%p >= block[%d]=%p / %d\n", i-1, mAllBlocks[i-1]->mPtr,
                    i, mAllBlocks[i]->mPtr, mAllBlocks.size() );
            DebuggerTrap();
         }
      }
   }
   #endif

   #ifndef HXCPP_SINGLE_THREADED_APP
   // Called once as the process exits, from the atexit handler Boot registers.
   //  Library finalizers run after atexit handlers, and a thread still running
   //  Haxe code could reach one that has been torn down: with musl, libgcc frees
   //  the table its unwinder looks frames up in, and a thread that throws after
   //  that crashes (#423). So every other thread that runs Haxe code is parked
   //  for good, at its next safe point or as it leaves a GC-free zone. The state
   //  lock stays held and collections stop, since a parked thread never answers
   //  one, and the exiting thread allocates as before.
   void ParkThreadsForExit()
   {
      LocalAllocator *self = (LocalAllocator *)(hx::ImmixAllocator *)hx::tlsStackContext;
      // Waits out a collection in progress from a GC-free zone, as Collect does
      bool entered = self && hx::TryGCFreeZone();
      gThreadStateChangeLock->lock();
      if (entered)
         hx::ExitGCFreeZoneLocked();
      sgInternalEnable = false;

      // A thread waiting on the lock to collect may have set it already
      unsigned int expected{ 0 };
      hx::gPauseForCollect.compare_exchange_strong(expected, std::numeric_limits<unsigned int>::max());
      std::atomic_thread_fence(std::memory_order_seq_cst);

      // A thread in a loop that never allocates reaches no safe point, so the
      //  wait has a limit and such a thread is left running
      auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
      for(int i=0;i<mLocalAllocs.size();i++)
         if (mLocalAllocs[i]!=self)
         {
            std::chrono::duration<double> left = deadline - std::chrono::steady_clock::now();
            WaitForSafeUntil(mLocalAllocs[i], left.count());
         }

      sgExitAlloc = self;
      sgParkedForExit.store(true);
      hx::gPauseForCollect = 0;
   }
   #endif

   void Collect(bool inMajor, bool inForceCompact, bool inLocked,bool inFreeIsFragged)
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      // A thread parked for exit never reaches a safe point again
      if (sgParkedForExit.load(std::memory_order_relaxed))
         return;
      #endif

      #ifdef HXCPP_GC_INCREMENTAL
      double incrementalStarted = hx::IncrementalNow();
      #endif
      PROFILE_COLLECT_SUMMARY_START;

      #ifndef HXCPP_SINGLE_THREADED_APP
      // If we set the flag from 0 -> 0xffffffff then we are the collector
      //  otherwise, someone else is collecting at the moment - so wait...
      unsigned int expected{ 0 };
      if (false == hx::gPauseForCollect.compare_exchange_strong(expected, std::numeric_limits<unsigned int>::max()))
      {
         if (inLocked)
         {
            gThreadStateChangeLock->unlock();

            hx::PauseForCollect();

            hx::EnterGCFreeZone();
            gThreadStateChangeLock->lock();
            hx::ExitGCFreeZoneLocked();
         }
         else
         {
            hx::PauseForCollect();
         }
         return;
      }
      // Pairs with the fences in EnterGCFreeZone and ExitGCFreeZone: the CAS
      //  alone does not order the mGCFreeZone reads after it on ARM64
      std::atomic_thread_fence(std::memory_order_seq_cst);
      #endif

      STAMP(t0)

      // We are the collector - all must wait for us
      LocalAllocator *this_local = 0;
      #ifndef HXCPP_SINGLE_THREADED_APP
      this_local = (LocalAllocator *)(hx::ImmixAllocator *)hx::tlsStackContext;

      if (!inLocked)
         gThreadStateChangeLock->lock();

      for(int i=0;i<mLocalAllocs.size();i++)
         if (mLocalAllocs[i]!=this_local)
            WaitForSafe(mLocalAllocs[i]);
      #endif

      sgIsCollecting = true;

      #ifdef HXCPP_GC_INCREMENTAL
      struct ExitCollection
      {
         GlobalAllocator *owner;
         LocalAllocator *current;
         bool locked;
         double start;
         bool completed = false;
         ExitCollection(GlobalAllocator *inOwner, LocalAllocator *inCurrent, bool inLocked, double inStart)
             : owner(inOwner), current(inCurrent), locked(inLocked), start(inStart) {}
         ~ExitCollection()
         {
            if (!completed)
            {
               if (hx::gIncrementalActive.load())
               {
                  std::lock_guard<std::mutex> lock(hx::IncrementalData().statsLock);
                  hx::IncrementalData().fallbacks++;
               }
               owner->StopZeroingLeft();
               owner->CancelIncremental();
               for (int i = 0; i < owner->mLocalAllocs.size(); i++)
               {
                  ((hx::StackContext *)owner->mLocalAllocs[i])->mIncrementalEntries.clear();
                  ((hx::StackContext *)owner->mLocalAllocs[i])->mIncrementalRead = 0;
               }
            }
            owner->ResumeIncremental(current, locked);
            hx::IncrementalData().pause(start, hx::sIncrementalRequestMs > 0);
         }
      } incrementalExit{this, this_local, inLocked, incrementalStarted};
#endif

      #ifdef HXCPP_GC_INCREMENTAL
      // A slice only marks, which does not disturb the zeroing thread, so it
      //  does not wait for one that the scheduler has preempted
      mZeroingLeft = hx::sIncrementalRequestMs > 0 && !sgCensus && !TryStopThreadJobs();
      if (!mZeroingLeft)
      #endif
         StopThreadJobs(true);
      #ifdef HXCPP_DEBUG
      sgAllocsSinceLastSpam = 0;
      #endif

      HX_STACK_FRAME("GC", "collect", 0, "GC::collect", __FILE__, __LINE__,0)
      #ifdef SHOW_MEM_EVENTS
      int here = 0;
      GCLOG("=== Collect === %p\n",&here);
      #endif


      #ifdef PROFILE_THREAD_USAGE
      GCLOG("Thread zero waits %d/%d/%d, misses=%d, hits=%d\n", sThreadZeroPokes, sThreadZeroWaits, (int)mFreeBlocks.size(), sThreadZeroMisses.load(), sThreadBlockZeroCount);
      sThreadZeroWaits = 0;
      sThreadZeroPokes = 0;
      sThreadZeroMisses = 0;
      sThreadBlockZeroCount = 0;
      #endif



      #ifdef HXCPP_TELEMETRY
      __hxt_gc_start();
      #endif

      size_t freeFraggedRows = 0; 
      if (inFreeIsFragged)
      {
         for(int i=mNextFreeBlockOfSize[0]; i<mFreeBlocks.size(); i++)
            freeFraggedRows += mFreeBlocks[i]->GetFreeRows();
      }

      // Now all threads have mTopOfStack & mBottomOfStack set.
      bool generational = false; 

      #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
      if (sGcMode==gcmGenerational)
      {
         for(int i=0;i<mLocalAllocs.size();i++)
         {
            hx::StackContext *ctx = (hx::StackContext *)mLocalAllocs[i];
            if( ctx->mOldReferrers->count )
                hx::sGlobalChunks.addLocked( ctx->mOldReferrers );
            else
                hx::sGlobalChunks.free( ctx->mOldReferrers );
            ctx->mOldReferrers = 0;
            hx::TakeYoungValues( ctx->mYoungValues );
         }
      }
      hx::sCollectCycle++;

      generational = !inMajor && !inForceCompact && sGcMode == gcmGenerational;
      if (sGcMode==gcmGenerational)
      {
         int remembered = hx::sGlobalChunks.markPointers(!generational);
         #ifdef SHOW_MEM_EVENTS
         GCLOG("Patch remembered set marks %d\n", remembered);
         #else
         (void)remembered;
         #endif
      }
      #endif

      STAMP(t1)

      #ifdef HXCPP_GC_INCREMENTAL
      bool cycleFinished = false;
      {
         hx::IncrementalState &state = hx::IncrementalData();
         bool active = hx::gIncrementalActive.load(std::memory_order_relaxed);
         bool requested = hx::sIncrementalRequestMs > 0 && !sgCensus;
         if (active && !requested)
         {
            std::lock_guard<std::mutex> lock(state.statsLock);
            state.forcedFinishes++;
         }
         if (active && sgCensus)
            state.failed.store(true, std::memory_order_relaxed);
         if (active && hx::sIncrementalRequestMs == 0)
         {
            for (int i = 0; i < mLocalAllocs.size(); i++)
               if (((hx::StackContext *)mLocalAllocs[i])->mIncrementalMutations > hx::IncrementalScopeDepth)
                  state.failed.store(true, std::memory_order_relaxed);
         }
         if (active && state.failed.exchange(false, std::memory_order_relaxed))
         {
            StopZeroingLeft();
            CancelIncremental();
            {
               std::lock_guard<std::mutex> lock(state.statsLock);
               state.fallbacks++;
            }
            active = requested = false;
         }
         if (active || requested)
         {
            if (!active)
            {
               state.clearWork();
               state.slices = 0;
               // Materialize the previous completed collection's free ranges
               // before replacing its row marks with a partial new mark. Only
               // a block in the free list can be allocated from in this cycle.
               for (int i = 0; i < mFreeBlocks.size(); i++)
               {
                  BlockDataInfo *block = mFreeBlocks[i];
                  if (mZeroingLeft)
                  {
                     int expected = 0;
                     while (!block->mZeroLock.compare_exchange_weak(expected, 1))
                        expected = 0;
                  }
                  if (!block->mReclaimed)
                     block->reclaim<false>(nullptr);
                  if (mZeroingLeft)
                     block->mZeroLock = 0;
               }
               state.cycleStarted(mIncrementalAllocated.load(std::memory_order_relaxed));
               state.failed.store(false, std::memory_order_relaxed);
               hx::gIncrementalActive.store(true, std::memory_order_relaxed);
            }
            state.tracing = true;
            state.work = 0;
            hx::gIncrementalAllocationsWhite = true;
            state.deadline = requested ? incrementalStarted + hx::sIncrementalRequestMs / 1000.0 : 0;
            state.dirty.clear();
            state.slices++;
            mMarker.init();
            state.logsPending = !hx::IncrementalEntries(state.detached, state.detachedRead, &mMarker);
            for (int i = 0; i < mLocalAllocs.size(); i++)
            {
               hx::StackContext *ctx = (hx::StackContext *)mLocalAllocs[i];
               if (!hx::IncrementalEntries(ctx->mIncrementalEntries, ctx->mIncrementalRead, &mMarker))
                  state.logsPending = true;
            }
            if (!MarkAll(false, active))
            {
               incrementalExit.completed = true;
               PROFILE_COLLECT_SUMMARY_END;
               return;
            }
            hx::gIncrementalActive.store(false, std::memory_order_relaxed);
            for (int i = 0; i < mLocalAllocs.size(); i++)
            {
               ClearPooledAlloc(mLocalAllocs[i]);
               ((hx::StackContext *)mLocalAllocs[i])->mIncrementalEntries.clear();
               ((hx::StackContext *)mLocalAllocs[i])->mIncrementalRead = 0;
            }
            {
               std::lock_guard<std::mutex> lock(state.statsLock);
               state.cycles++;
               state.lastSlices = state.slices;
            }
            // A cycle the allocator cut short started too late, and needed more
            //  slices than it got. An explicit collection says nothing about pacing.
            size_t cycleAllocated = mIncrementalAllocated.load(std::memory_order_relaxed);
            if (requested)
               state.cycleFinished(cycleAllocated, state.slices);
            else if (!inMajor)
               state.cycleFinished(cycleAllocated, state.slices * 1.5);
            state.clearWork();
            cycleFinished = true;
         }
         else
         {
            for (int i = 0; i < mLocalAllocs.size(); i++)
            {
               ((hx::StackContext *)mLocalAllocs[i])->mIncrementalEntries.clear();
               ((hx::StackContext *)mLocalAllocs[i])->mIncrementalRead = 0;
            }
            MarkAll(false);
         }
      }
#else
      MarkAll(generational);
      #endif

      #ifdef HX_GC_VERIFY_GENERATIONAL
      {
         #ifdef SHOW_MEM_EVENTS
         GCLOG("verify generational [\n");
         #endif
         sGcVerifyGenerational = true;
         sgTimeToNextTableUpdate--;
         MarkAll(false);
         sGcVerifyGenerational = false;
         #ifdef SHOW_MEM_EVENTS
         GCLOG("] verify generational\n");
         #endif
      }
      #endif

      STAMP(t2)


      // Sweep blocks

      // Update table entries?  This needs to be done before the gMarkID count clocks
      //  back to the same number
      if (!generational)
         sgTimeToNextTableUpdate--;

      bool full = inMajor || (sgTimeToNextTableUpdate<=0) || inForceCompact;

      // Setup memory target ...
      // Count free rows, and prep blocks for sorting
      BlockDataStats stats;

      /*
       This reduces the stall time, but adds a bit of background cpu usage
       Might be good to just countRows for non-generational too
      */
      if (!full && generational)
      {
         countRows(stats);
         size_t currentRows = stats.rowsInUse + stats.fraggedRows + freeFraggedRows;
         double filled = (double)(currentRows) / (double)(mAllBlocks.size()*IMMIX_USEFUL_LINES);
         if (filled>0.85)
         {
            // Failure of generational estimation
            int retained = currentRows - mRowsInUse;
            int space = mAllBlocks.size()*IMMIX_USEFUL_LINES - mRowsInUse;
            if (space<retained)
               space = retained;
            if (space<1)
               space = 1;
            mGenerationalRetainEstimate = (double)retained/(double)space;
            #ifdef SHOW_MEM_EVENTS
            GCLOG("Generational retention/fragmentation too high %f, do normal collect\n", mGenerationalRetainEstimate);
            #endif

            #ifdef HX_GC_VERIFY_ALLOC_START
            verifyAllocStart();
            #endif

            generational = false;
            MarkAll(generational);

            sgTimeToNextTableUpdate--;
            full = sgTimeToNextTableUpdate<=0;

            stats.clear();
            reclaimBlocks(full,stats);
         }
      }
      #ifdef HXCPP_GC_INCREMENTAL
      // The sweep is most of the stop that finishes a cycle. The zeroing thread
      //  or the allocator does it instead, as each block comes up for use
      else if (!full && cycleFinished)
      {
         countRows(stats);
      }
      #endif
      else
      {
         reclaimBlocks(full,stats);
      }


      #ifdef HXCPP_GC_MOVING
      if (!full)
      {
         double useRatio = (double)(mRowsInUse<<IMMIX_LINE_BITS) / (sWorkingMemorySize);
         #if defined(SHOW_FRAGMENTATION) || defined(SHOW_MEM_EVENTS)
            GCLOG("Row use ratio:%f\n", useRatio);
         #endif
         // Could be either expanding, or fragmented...
         if (useRatio>0.75)
         {
            #if defined(SHOW_FRAGMENTATION) || defined(SHOW_MEM_EVENTS)
               GCLOG("Do full stats\n", useRatio);
            #endif
            full = true;
            stats.clear();
            reclaimBlocks(full,stats);
         }
      }
      #endif

      if (full)
      {
         #ifdef HXCPP_GC_MOVING
         sgTimeToNextTableUpdate = 7;
         #elif defined(HX_GC_VERIFY_GENERATIONAL)
         // The verification mark bumps the mark id a second time in a collection,
         // so one fewer keeps the table update ahead of the 16 mark ids
         sgTimeToNextTableUpdate = 14;
         #else
         sgTimeToNextTableUpdate = 15;
         #endif
      }

      size_t oldRowsInUse = mRowsInUse;
      mRowsInUse = stats.rowsInUse + stats.fraggedRows + freeFraggedRows;

      bool moved = false;

      #if defined(SHOW_FRAGMENTATION) || defined(SHOW_MEM_EVENTS)
      GCLOG("Total memory : %s\n", formatBytes(GetWorkingMemory()).c_str());
      GCLOG("  reserved bytes : %s\n", formatBytes(mRowsInUse*IMMIX_LINE_LEN).c_str());
      if (full)
      {
         GCLOG("  active bytes   : %s\n", formatBytes(stats.bytesInUse).c_str());
         GCLOG("  active ratio   : %f\n", (double)stats.bytesInUse/( mRowsInUse*IMMIX_LINE_LEN));
         GCLOG("  fragged blocks : %d (%.1f%%)\n", (int)stats.fraggedBlocks, stats.fraggedBlocks*100.0/mAllBlocks.size() );
         GCLOG("  fragged score  : %f\n", (double)stats.fragScore/mAllBlocks.size() );
      }
      GCLOG("  large size   : %s\n", formatBytes(mLargeAllocated).c_str());
      GCLOG("  empty blocks : %d (%.1f%%)\n", (int)stats.emptyBlocks, stats.emptyBlocks*100.0/mAllBlocks.size());
      #endif

      size_t bytesInUse = mRowsInUse<<IMMIX_LINE_BITS;

      STAMP(t3)


      #ifdef HXCPP_TELEMETRY
      // Detect deallocations
      __hxt_gc_after_mark(gByteMarkID, HX_ENDIAN_MARK_ID_BYTE);
      #endif

      // Sweep large

      // Manage recycle size ?
      //  clear old frames recycle objects
      int l2 = mRecycleSize.load(std::memory_order_relaxed);
      RecycleLists dropped;
      dropped.swap(mRecycle);
      mDroppedBytes.fetch_add(mRecycleBytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
      mRecycleBytes.store(0, std::memory_order_relaxed);
      mRecycleSize.store(0, std::memory_order_relaxed);

      size_t recycleRemaining = 0;
      #ifdef RECYCLE_LARGE
      if (!inForceCompact)
         recycleRemaining = mLargeAllocForceRefresh;
      #endif

      // The allocations that survived a collection come first in the list. A
      // minor collection cannot free one, so it sweeps only those made since,
      // unless an old one was released for the sweep to free. It used to
      // sweep them all, touching every long-lived buffer (libhxcpp #318).
      bool sweepOld = !generational || mLargeOldReleased;
      mLargeOldReleased = false;
      int firstNew = mLargeOld;
      int idx = sweepOld ? 0 : mLargeOld;
      if (sweepOld)
      {
         mLargeOld = 0;
         // The kept index loses the dead and takes the new survivors, so a
         //  full collection does not sort a stable pool again (libhxcpp #365).
         //  The sweep reorders the list, so the new survivors are taken first
         if (mLargeIndexReady)
         {
            auto dead = [](uintptr_t inBase) { return (((unsigned int *)inBase)[-1] & IMMIX_ALLOC_MARK_ID) != hx::gMarkID; };
            mLargeIndex.erase(std::remove_if(mLargeIndex.begin(), mLargeIndex.end(), dead), mLargeIndex.end());
            mLargeIndexNew.clear();
            for(int i=firstNew;i<mLargeList.size();i++)
               if (!dead(reinterpret_cast<uintptr_t>(mLargeList[i] + 2)))
                  mLargeIndexNew.push_back(reinterpret_cast<uintptr_t>(mLargeList[i] + 2));
         }
      }
      int l0 = mLargeList.size();
      // Bounds of the survivors, so a past burst does not keep them wide
      size_t largeMin = sweepOld ? (size_t)-1 : mLargeOldMin;
      size_t largeMax = sweepOld ? 0 : mLargeOldMax;
      while(idx<mLargeList.size())
      {
         unsigned int *blob = mLargeList[idx];
         if ( (blob[1] & IMMIX_ALLOC_MARK_ID) != hx::gMarkID )
         {
            unsigned int size = *blob;
            takeLargeAllocated(size);
            if (size < recycleRemaining)
            {
               recycleRemaining -= size;
               pushRecycle(blob);
            }
            else
            {
               HxFree(blob);
            }

            qeraseLarge(idx);
         }
         else
         {
            size_t objPtr = (size_t)(blob+2);
            if (objPtr < largeMin)
               largeMin = objPtr;
            if (objPtr > largeMax)
               largeMax = objPtr;
            idx++;
         }
      }
      // Every survivor is old now. A kept index takes the new survivors' bases
      if (mLargeIndexReady)
      {
         size_t had = mLargeIndex.size();
         if (sweepOld)
            mLargeIndex.insert(mLargeIndex.end(), mLargeIndexNew.begin(), mLargeIndexNew.end());
         else
            for(int i=firstNew;i<mLargeList.size();i++)
               mLargeIndex.push_back( reinterpret_cast<uintptr_t>(mLargeList[i] + 2) );
         std::sort(mLargeIndex.begin()+had, mLargeIndex.end());
         std::inplace_merge(mLargeIndex.begin(), mLargeIndex.begin()+had, mLargeIndex.end());
      }
      mLargeOld = mLargeList.size();
      mLargeOldMin = mLargeMin = largeMin;
      mLargeOldMax = mLargeMax = largeMax;

      int l1 = mLargeList.size();


      STAMP(t4)

      bool defragged = false;

      // Compact/Defrag?
      #if defined(HXCPP_GC_MOVING) && defined(HXCPP_VISIT_ALLOCS)
      if (full)
      {
         bool doRelease = false;
         // Only Gc.compact() gives everything back, as without the moving
         //  collector below: Gc.run(true) and the allocator's collection when
         //  its free list is full also compact, and releasing the whole heap
         //  then shrank it to what was live, so it never grew (#230, #231)
         bool releaseAll = inForceCompact && inMajor && sReleaseAllRequests.load() > 0;

         if (releaseAll)
            doRelease = true;
         else
         {
            size_t mem = mRowsInUse<<IMMIX_LINE_BITS;
            size_t targetFree = std::max(hx::sgMinimumFreeSpace, mem / 100 * hx::sgTargetFreeSpacePercentage );
            targetFree = std::min(targetFree, sgMaximumFreeSpace );
            sWorkingMemorySize = std::max( mem + targetFree, hx::sgMinimumWorkingMemory);

            size_t allMem = GetWorkingMemory();
            // 8 Meg too much?
            size_t allowExtra = std::max(size_t{ 8 } * 1024 * 1024, sWorkingMemorySize * 5 / 4);

            if ( allMem > sWorkingMemorySize + allowExtra )
            {
               #if defined(SHOW_FRAGMENTATION) || defined(SHOW_MEM_EVENTS)
               int releaseGroups = (int)((allMem - sWorkingMemorySize) / (IMMIX_BLOCK_SIZE<<IMMIX_BLOCK_GROUP_BITS));
               if (releaseGroups)
                  GCLOG("Try to release %d groups\n", releaseGroups );
               #endif
               doRelease = true;
            }
         }


         bool isFragged = stats.fragScore > mAllBlocks.size()*FRAG_THRESH;
         if (doRelease || isFragged || hx::gAlwaysMove || inForceCompact)
         {
            if (isFragged && sgTimeToNextTableUpdate>3)
               sgTimeToNextTableUpdate = 3;
            calcMoveOrder( );

            // Borrow some blocks to ensuure space to defrag into
            int workingBlocks = mAllBlocks.size()*3/2 - stats.emptyBlocks;
            int borrowed = 0;
            while( mAllBlocks.size()<workingBlocks )
            {
               bool dummy = false;
               if (!AllocMoreBlocks(dummy, true))
                  break;
               doRelease = true;
               borrowed++;
            }
            stats.emptyBlocks += borrowed;
            #if defined(SHOW_FRAGMENTATION)
               GCLOG("Borrowed %d groups for %d target blocks\n", borrowed, workingBlocks);
            #endif

            MoveBlockJob job(mAllBlocks);

            if (MoveBlocks(job,stats) || doRelease)
            {
               if (doRelease)
               {
                  size_t mem = mRowsInUse<<IMMIX_LINE_BITS;
                  size_t targetFree = std::max(hx::sgMinimumFreeSpace, bytesInUse/100 *hx::sgTargetFreeSpacePercentage );
                  targetFree = std::min(targetFree, sgMaximumFreeSpace );
                  size_t targetMem = std::max( mem + targetFree, hx::sgMinimumWorkingMemory) +
                                        (2<<(IMMIX_BLOCK_GROUP_BITS+IMMIX_BLOCK_BITS));

                  if (releaseAll)
                     targetMem = 0;
                  size_t have = GetWorkingMemory();
                  if (targetMem<have)
                  {
                     size_t releaseSize = have - targetMem;
                     #ifdef SHOW_FRAGMENTATION
                     GCLOG(" Release %s bytes to leave %s\n", formatBytes(releaseSize).c_str(), formatBytes(targetMem).c_str() );
                     #endif

                     int releasedBlocks = releaseEmptyGroups(stats, releaseSize);
                     #ifdef SHOW_FRAGMENTATION
                     int releasedGroups = releasedBlocks >> IMMIX_BLOCK_GROUP_BITS;
                     GCLOG(" Released %s, %d groups\n", formatBytes((size_t)releasedBlocks<<IMMIX_BLOCK_BITS).c_str(), releasedGroups );
                     #endif
                  }
               }

               // Reduce sWorkingMemorySize now we have defragged
               #if defined(SHOW_FRAGMENTATION) || defined(SHOW_MEM_EVENTS)
               GCLOG("After compacting---\n");
               GCLOG("  total memory : %s\n", formatBytes(GetWorkingMemory()).c_str() );
               GCLOG("  total needed : %s\n", formatBytes((size_t)mRowsInUse*IMMIX_LINE_LEN).c_str() );
               GCLOG("  for bytes    : %s\n", formatBytes(bytesInUse).c_str() );
               GCLOG("  empty blocks : %d (%.1f%%)\n", (int)stats.emptyBlocks, stats.emptyBlocks*100.0/mAllBlocks.size());
               GCLOG("  fragged blocks : %d (%.1f%%)\n", (int)stats.fraggedBlocks, stats.fraggedBlocks*100.0/mAllBlocks.size() );
               #endif
            }

            std::stable_sort(&mAllBlocks[0], &mAllBlocks[0] + mAllBlocks.size(), SortByBlockPtr );

            #ifdef HXCPP_GC_VERIFY
            VerifyBlockOrder();
            #endif
         }
      }
      #endif

      // Without the moving collector, only groups whose blocks are all empty can
      // go back to the system; every allocator dropped its block when it was marked.
      // Not under Emscripten: wasm memory never shrinks, so a released group only
      // fragments malloc's heap, and the next groups grow the memory instead of
      // reusing it (an instance compiling repeatedly ran out at 2 GB).
      #if !defined(HXCPP_GC_MOVING) && defined(HXCPP_VISIT_ALLOCS) && !defined(__EMSCRIPTEN__)
      if (full)
      {
         size_t mem = mRowsInUse<<IMMIX_LINE_BITS;
         size_t targetFree = std::max(hx::sgMinimumFreeSpace, mem / 100 * hx::sgTargetFreeSpacePercentage );
         targetFree = std::min(targetFree, sgMaximumFreeSpace );
         size_t targetMem = std::max( mem + targetFree, hx::sgMinimumWorkingMemory) +
                               (2<<(IMMIX_BLOCK_GROUP_BITS+IMMIX_BLOCK_BITS));
         size_t allowExtra = std::max(size_t{ 8 } * 1024 * 1024, targetMem / 4);
         // Only an explicit Gc.compact() gives everything back. Gc.run(true) passes the
         // same major+compact flags, but keeps the normal target so the heap it just
         // grew stays committed (#231, #233). The allocator also forces a compacting
         // collection when mFreeBlocks has no room for another group (it can't realloc
         // while other threads read it); releasing then threw away the groups it was
         // about to reuse, so the heap never grew past one free-list reserve and
         // collected ~4x as often (#230).
         if (inForceCompact && inMajor && sReleaseAllRequests.load() > 0)
         {
            targetMem = 0;
            allowExtra = 0;
         }
         size_t have = GetWorkingMemory();
         if (have > targetMem + allowExtra)
         {
            calcMoveOrder();
            releaseEmptyGroups(stats, have - targetMem);
         }
      }
      #endif


      STAMP(t5)

      size_t mem = mRowsInUse<<IMMIX_LINE_BITS;
      if (full)
         mLiveBytes = stats.bytesInUse;
      size_t baseMem = full ? bytesInUse : mem;
      #ifdef HXCPP_GC_DYNAMIC_SIZE
      size_t targetFree = std::max(hx::sgMinimumFreeSpace, (size_t)(baseMem * profileCollectSummary.spaceFactor) );
      #else
      size_t targetFree = std::max(hx::sgMinimumFreeSpace, baseMem / 100 *hx::sgTargetFreeSpacePercentage );
      #endif
      targetFree = std::min(targetFree, sgMaximumFreeSpace );
      // Only adjust if non-generational
      if (!generational)
         sWorkingMemorySize = std::max( mem + targetFree, hx::sgMinimumWorkingMemory);

      #if defined(SHOW_FRAGMENTATION) || defined(SHOW_MEM_EVENTS)
      GCLOG("Target memory %s, using %s\n",  formatBytes(sWorkingMemorySize).c_str(), formatBytes(mem).c_str() );
      #endif

      // Large alloc target
      // A burst's allowance halves at each collection, down to what the heap
      // needs now, so garbage after it does not pile up to the old peak
      size_t blockSize =  GetWorkingMemory();
      size_t space = std::max(blockSize, mLargeAllocSpace.load(std::memory_order_relaxed)/2);
      mLargeAllocSpace.store(std::max(space, sLargeAllocSpaceFloor), std::memory_order_relaxed);
      mLargeAllocForceRefresh = mLargeAllocated + mLargeAllocSpace;

      mTotalAfterLastCollect = MemUsage();

      #ifdef HXCPP_GC_INCREMENTAL
      // The stop-the-world collector spends every free block, and grows the heap
      //  up to its target, before it collects. Pacing against less than that
      //  runs a cycle more often than it would collect.
      {
         size_t heap = std::max(sWorkingMemorySize, GetWorkingMemory());
         size_t reserve = std::max(targetFree, heap>mem ? heap-mem : 0);
         size_t startAt, pressureAt;
         hx::IncrementalData().plan(reserve, startAt, pressureAt);
         mIncrementalStartAt.store(startAt,std::memory_order_relaxed);
         mIncrementalPressureAt.store(pressureAt,std::memory_order_relaxed);
      }
      mIncrementalAllocated.store(0,std::memory_order_relaxed);
      #endif

      #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
      if (generational)
      {
         // TODO - include large too?
         int retained = mRowsInUse - oldRowsInUse;
         int space = mAllBlocks.size()*IMMIX_USEFUL_LINES - oldRowsInUse;
         if (space<retained)
            space = retained;

         mGenerationalRetainEstimate = (double)retained/(double)space;
      }
      else
      {
         // move towards 0.2
         mGenerationalRetainEstimate += (0.2-mGenerationalRetainEstimate)*0.25;
      }

      double filled_ratio = (double)mRowsInUse/(double)(mAllBlocksCount*IMMIX_USEFUL_LINES);
      double after_gen = filled_ratio + (1.0-filled_ratio)*mGenerationalRetainEstimate;

      if (after_gen<0.75)
      {
         sGcMode = gcmGenerational;
      }
      else
      {
         sGcMode = gcmFull;
         // What was I thinking here?  This breaks #851
         //gByteMarkID |= 0x30;
      }

      // Only a full mark frees an old large buffer, or the native memory of
      //  an old object, so a small heap kept every collection that large
      //  allocations started a minor one while released old buffers piled
      //  up (libhxcpp #360). Once the large and native allocations since the
      //  last full mark add up to the heap it left, the next one is full,
      //  which costs no more than marking the heap that often would
      if (!generational)
      {
         mLargeSinceFull = 0;
         mLargeFullBudget = std::max((mRowsInUse<<IMMIX_LINE_BITS) + mLargeAllocated, sLargeAllocSpaceFloor);
      }
      else if (mLargeSinceFull > mLargeFullBudget)
         sGcMode = gcmFull;

      #ifdef SHOW_MEM_EVENTS
      GCLOG("filled=%.2f%% + estimate = %.2f%% = %.2f%% -> %s\n",
            filled_ratio*100, mGenerationalRetainEstimate*100, after_gen*100, sGcMode==gcmFull?"Full":"Generational");
      #endif

      #endif

      createFreeList();

      // This saves some running/stall time, but increases the total CPU usage
      // Delaying it until just before the block is used to improve the cache locality
      backgroundProcessFreeList(true);

      mAllBlocksCount   = mAllBlocks.size();
      mCurrentRowsInUse = mRowsInUse;

      #ifdef SHOW_MEM_EVENTS
      GCLOG("Collect Done\n");
      #endif

      #ifdef PROFILE_COLLECT
      STAMP(t6)
      double period = t6-sLastCollect;
      sLastCollect=t6;
      GCLOG("Collect time %s total=%.2fms =%.1f%%\n  setup=%.2f\n  %s=%.2f(init=%.2f/roots=%.2f %d+%d/loc=%.2f*%d %d+%d/mark=%.2f %d+%d/fin=%.2f*%d/ids=%.2f)\n  reclaim=%.2f\n  large(%d->%d, recyc %d)=%.2f\n  defrag=%.2f\n",
               generational ? "gen" : "std",
              (t6-t0)*1000, (t6-t0)*100.0/period, // total %
              (t1-t0)*1000, // sync/setup
              generational ? "mark gen" : "mark", (t2-t1)*1000,
                      (tMarkInit-t1)*1000,
                      (tMarkLocal-tMarkInit)*1000,  hx::rootObjects, hx::rootAllocs,
                      (tMarkLocalEnd-tMarkLocal)*1000, hx::localCount, hx::localObjects-hx::rootObjects, hx::localAllocs-hx::rootAllocs,
                      (tMarked-tMarkLocalEnd)*1000, sObjectMarks-hx::localObjects, sAllocMarks-hx::localAllocs,
                      (hx::tFinalizers-tMarked)*1000, hx::finalizerCount,
                      (t2-hx::tFinalizers)*1000,
              (t3-t2)*1000, // reclaim
              l0, l1, l2, (t4-t3)*1000, // large
              (t5-t4)*1000 // defrag
              );
      sObjectMarks = sAllocMarks = 0;

      #endif

      #ifdef PROFILE_THREAD_USAGE
      GCLOG("Thread chunks:%d, wakes=%d\n", sThreadChunkPushCount.load(), sThreadChunkWakes);
      for(int i=-1;i<MAX_GC_THREADS;i++)
        GCLOG(" thread %d] %d + %d\n", i, sThreadMarkCount[i], sThreadArrayMarkCount[i]);
      GCLOG("Locking spins  : %d\n", sSpinCount.load());
      sSpinCount = 0;
      #endif

      #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
      if (sGcMode==gcmGenerational)
         for(int i=0;i<mLocalAllocs.size();i++)
         {
            hx::StackContext *ctx = (hx::StackContext *)mLocalAllocs[i];
            ctx->mOldReferrers = hx::sGlobalChunks.alloc();
            ctx->mYoungValues = hx::sGlobalChunks.alloc();
         }
      #endif


      #ifdef HXCPP_GC_VERIFY
      VerifyBlockOrder();
      #endif

      // Minor collections leave the spare chunks to the next full one, so a
      //  burst repeated between full collections does not make them again
      if (!generational)
      {
         #ifdef HXCPP_GC_INCREMENTAL
         hx::sGlobalChunks.trimFree(256, cycleFinished ? 128 : 1024);
         #else
         hx::sGlobalChunks.trimFree(256, 1024);
         #endif
      }

      for(int i=0;i<LOCAL_POOL_SIZE;i++)
      {
         LocalAllocator *l = mLocalPool[i];
         if (l)
            ClearPooledAlloc(l);
      }

      #if defined(HXCPP_TELEMETRY) && !defined(HXCPP_GC_INCREMENTAL)
      __hxt_gc_end();
      #endif

      #ifdef HXCPP_GC_INCREMENTAL
      // incrementalExit resumes the other threads as Collect returns
      if (hx::sFinalizerException)
         hx::sFinalizerExceptionOwner.store((void *)hx::tlsStackContext, std::memory_order_release);
      incrementalExit.completed = true;
      #else
      sgIsCollecting = false;

      if (hx::sFinalizerException)
         hx::sFinalizerExceptionOwner.store((void *)hx::tlsStackContext, std::memory_order_release);

      hx::gPauseForCollect = 0x00000000;
      #ifndef HXCPP_SINGLE_THREADED_APP
         for(int i=0;i<mLocalAllocs.size();i++)
         {
            #ifdef HXCPP_SCRIPTABLE
            ((hx::StackContext *)mLocalAllocs[i])->byteMarkId = hx::gByteMarkID;
            #endif
            if (mLocalAllocs[i]!=this_local)
               ReleaseFromSafe(mLocalAllocs[i]);
         }

         if (!inLocked)
            gThreadStateChangeLock->unlock();
      #else
        #ifdef HXCPP_SCRIPTABLE
        hx::gMainThreadContext->byteMarkId = hx::gByteMarkID;
        #endif
      #endif
      #endif // HXCPP_GC_INCREMENTAL

      releaseDropped(dropped, inForceCompact);

      PROFILE_COLLECT_SUMMARY_END;

      if (!inLocked)
         hx::ThrowFinalizerException();
   }

   #ifdef HXCPP_GC_INCREMENTAL
   void StopZeroingLeft()
   {
      if (mZeroingLeft)
      {
         StopThreadJobs(true);
         mZeroingLeft = false;
      }
   }

   void CancelIncremental()
   {
      if (hx::gIncrementalActive.load(std::memory_order_relaxed))
      {
         // A partial mark contains both epochs. Advancing again would make
         // its unvisited objects look black to the alternating-bit fast
         // path. Undo this epoch before starting an independent full mark.
         for (int b = 0; b < mAllBlocks.size(); b++)
         {
            BlockDataInfo *block = mAllBlocks[b];
            for (int r = IMMIX_HEADER_LINES; r < IMMIX_LINES; r++)
            {
               unsigned int starts = block->allocStart[r];
               unsigned int *headers = reinterpret_cast<unsigned int *>(block->mPtr->row(r));
               for (int bit = 0; starts; bit++, starts >>= 1)
                  if ((starts & 1) && (headers[bit] & IMMIX_ALLOC_MARK_ID) == hx::gMarkID)
                     reinterpret_cast<unsigned char *>(headers + bit + 1)[HX_ENDIAN_MARK_ID_BYTE] = hx::gPrevByteMarkID;
            }
         }
         for (int i = 0; i < mLargeList.size(); i++)
            if ((mLargeList[i][1] & IMMIX_ALLOC_MARK_ID) == hx::gMarkID)
               reinterpret_cast<unsigned char *>(mLargeList[i] + 2)[HX_ENDIAN_MARK_ID_BYTE] = hx::gPrevByteMarkID;
         hx::gByteMarkID = hx::gPrevByteMarkID;
         hx::gMarkID = hx::gByteMarkID << 24;
         hx::gMarkIDWithContainer = hx::gMarkID | IMMIX_ALLOC_IS_CONTAINER;
         hx::gPrevMarkIdMask = (hx::gMarkID & 0x30000000) | HX_GC_CONST_ALLOC_BIT;
         hx::gPrevByteMarkID = ((hx::gByteMarkID - 1) & 0x0f) | ((hx::gByteMarkID & 1) ? 0x10 : 0x20);
         hx::gRememberedByteMarkID = hx::gByteMarkID | HX_GC_REMEMBERED;
      }
      hx::IncrementalDiscard(&mMarker);
   }

   void ResumeIncremental(LocalAllocator *inCurrent, bool inLocked)
   {
      if (hx::gIncrementalActive.load(std::memory_order_relaxed))
         for (int i = 0; i < mLocalAllocs.size(); i++)
            RestoreIncrementalAlloc(mLocalAllocs[i]);
#ifdef HXCPP_TELEMETRY
      __hxt_gc_end();
#endif
      sgIsCollecting = false;
      hx::gPauseForCollect = 0;
#ifndef HXCPP_SINGLE_THREADED_APP
      for (int i = 0; i < mLocalAllocs.size(); i++)
         if (mLocalAllocs[i] != inCurrent)
            ReleaseFromSafe(mLocalAllocs[i]);
      if (!inLocked)
         gThreadStateChangeLock->unlock();
#endif
   }
#endif

   void reclaimBlocks(bool full, BlockDataStats &outStats)
   {
      if (MAX_GC_THREADS>1)
      {
         for(int i=0;i<MAX_GC_THREADS;i++)
            sThreadBlockDataStats[i].clear();
         StartThreadJobs(full ? tpjReclaimFull : tpjReclaim, mAllBlocks.size(), false);
         outStats.clear();
         ReclaimAsync(outStats);
         JoinStartedWorkers();
         for(int i=0;i<MAX_GC_THREADS;i++)
            outStats.add(sThreadBlockDataStats[i]);
      }
      else
      {
         outStats.clear();
         for(int i=0;i<mAllBlocks.size();i++)
         {
            if (full)
               mAllBlocks[i]->reclaim<true>(&outStats);
            else
               mAllBlocks[i]->reclaim<false>(&outStats);
         }
      }
   }


   void countRows(BlockDataStats &outStats)
   {
      // A block a minor collection marked nothing new in needs no recount, so
      // with few that do the work is less than waking the marking threads for
      // it: that took most of an otherwise idle minor collection (#329)
      bool threaded = MAX_GC_THREADS>1;
      #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
      if (threaded)
      {
         int changed = 0;
         for(int i=0;i<mAllBlocks.size() && changed<=256;i++)
            if (mAllBlocks[i]->mHasSurvivor)
               changed++;
         threaded = changed>256;
      }
      #endif
      if (threaded)
      {
         for(int i=0;i<MAX_GC_THREADS;i++)
            sThreadBlockDataStats[i].clear();
         StartThreadJobs(tpjCountRows, mAllBlocks.size(), false);
         outStats.clear();
         CountAsync(outStats);
         JoinStartedWorkers();
         for(int i=0;i<MAX_GC_THREADS;i++)
            outStats.add(sThreadBlockDataStats[i]);
      }
      else
      {
         outStats.clear();
         for(int i=0;i<mAllBlocks.size();i++)
            mAllBlocks[i]->countRows(outStats);
      }
   }


   // buils mFreeBlocks and maybe starts the async-zero process on mZeroList
   void createFreeList()
   {
      // Smallest hole first. Holes are whole rows, so counting them into place
      //  costs less than sorting, and this is on the collector's stop
      int place[IMMIX_LINES+1] = {};
      for(int i=0;i<mAllBlocks.size();i++)
      {
         BlockDataInfo *info = mAllBlocks[i];
         if (info->GetFreeRows() > 0 && info->mMaxHoleSize>256)
            place[(info->mMaxHoleSize>>IMMIX_LINE_BITS)+1]++;
      }
      for(int i=1;i<=IMMIX_LINES;i++)
         place[i] += place[i-1];

      mFreeBlocks.setSize(place[IMMIX_LINES]);
      for(int i=0;i<mAllBlocks.size();i++)
      {
         BlockDataInfo *info = mAllBlocks[i];
         if (info->GetFreeRows() > 0 && info->mMaxHoleSize>256)
         {
            info->mOwned = false;
            mFreeBlocks[place[info->mMaxHoleSize>>IMMIX_LINE_BITS]++] = info;
         }
      }

      size_t extra{ std::max(mAllBlocks.size(), static_cast<size_t>(8 << IMMIX_BLOCK_GROUP_BITS)) };
      mFreeBlocks.safeReserveExtra(extra);

      for(int i=0;i<BLOCK_OFSIZE_COUNT;i++)
         mNextFreeBlockOfSize[i] = mFreeBlocks.size();

      for(int i=mFreeBlocks.size()-1;i>=0;i--)
      {
         int slot = mFreeBlocks[i]->mMaxHoleSize >> IMMIX_LINE_BITS;
         if (slot>=BLOCK_OFSIZE_COUNT)
            slot = BLOCK_OFSIZE_COUNT-1;
         mNextFreeBlockOfSize[slot] = i;
      }

      for(int i=BLOCK_OFSIZE_COUNT-2;i>=0;i--)
         if (mNextFreeBlockOfSize[i]>mNextFreeBlockOfSize[i+1])
            mNextFreeBlockOfSize[i] = mNextFreeBlockOfSize[i+1];

      mZeroList.clear();
   }

   void backgroundProcessFreeList(bool inJit)
   {
      mZeroListQueue = 0;
      #ifdef HXCPP_GC_INCREMENTAL
      mReclaimAhead = 0;
      #endif
      #ifdef HX_GC_ZERO_EARLY
      mZeroList.setSize(mFreeBlocks.size());
      memcpy( &mZeroList[0], &mFreeBlocks[0], mFreeBlocks.size()*sizeof(void *));

      StartThreadJobs(tpjAsyncZero, mZeroList.size(),true);
      #else
      if ( MAX_GC_THREADS>1 && mFreeBlocks.size()>4)
      {
         mZeroList.setSize(mFreeBlocks.size());
         memcpy( &mZeroList[0], &mFreeBlocks[0], mFreeBlocks.size()*sizeof(void *));

         // Only use one thread for parallel zeroing.  Try to get though the work wihout
         //  slowing down the main thread
         StartThreadJobs(inJit ? tpjAsyncZeroJit : tpjAsyncZero, mZeroList.size(), false, 1);
      }
      #endif
   }




   size_t MemLive()
   {
      return mLiveBytes + mLargeAllocated;
   }

   size_t MemLarge()
   {
      return mLargeAllocated;
   }

   // With the dead large buffers kept for reuse, which are still the
   //  process's memory (libhxcpp #313)
   size_t MemReserved()
   {
      return mLargeAllocated + mRecycleBytes.load(std::memory_order_relaxed) +
             mDroppedBytes.load(std::memory_order_relaxed) +
             (mAllBlocksCount*IMMIX_USEFUL_LINES<<IMMIX_LINE_BITS);
   }

   size_t MemCurrent()
   {
      return mLargeAllocated + (mCurrentRowsInUse<<IMMIX_LINE_BITS);
   }

   size_t MemUsage()
   {
      return mLargeAllocated + (mRowsInUse<<IMMIX_LINE_BITS);
   }

   bool IsAllBlock(BlockData *block)
   {
      if (mAllBlocks.size())
      {
         int min = 0;
         int max = mAllBlocks.size()-1;
         if (block==mAllBlocks[0]->mPtr)
            return true;
         if (block==mAllBlocks[max]->mPtr)
            return true;
         if (block>mAllBlocks[0]->mPtr && block<mAllBlocks[max]->mPtr)
         {
            while(min<max-1)
            {
               int mid = (max+min)>>1;
               if (mAllBlocks[mid]->mPtr==block)
                  return true;

               if (mAllBlocks[mid]->mPtr<block)
                  min = mid;
               else
                  max = mid;
            }
         }
      }
      return false;
   }

   // The large allocation in sorted inIndex that starts inReach bytes or less
   //  below inPtr, or 0
   static uintptr_t FindLarge(const std::vector<uintptr_t> &inIndex, uintptr_t inPtr, size_t inReach)
   {
      std::vector<uintptr_t>::const_iterator above = std::upper_bound(inIndex.begin(), inIndex.end(), inPtr);
      if (above==inIndex.begin())
         return 0;
      uintptr_t base = *(above-1);
      return IsLargeAt(base, inPtr, inReach) ? base : 0;
   }

   // Whether inPtr is inBase or at most inReach bytes into its allocation
   static bool IsLargeAt(uintptr_t inBase, uintptr_t inPtr, size_t inReach)
   {
      uintptr_t offset = inPtr - inBase;
      return offset==0 || (offset<=inReach && offset<reinterpret_cast<unsigned int *>(inBase)[-2]);
   }

   // With outBase, a pointer up to sgCheckInternalOffset bytes into a large
   //  allocation finds it too, as GetEnclosingAllocType does in a block, and
   //  outBase is set to where it starts. Native code may hold only a field's
   //  address, which kept a small object alive but not a large one (libhxcpp #373)
   MemType GetMemType(uintptr_t inPtr, uintptr_t *outBase = nullptr)
   {
      BlockData* block{ reinterpret_cast<BlockData*>(inPtr & IMMIX_BLOCK_BASE_MASK) };

      bool isBlock{ IsAllBlock(block) };

      if (isBlock)
         return memBlock;

      // Most stack words are nowhere near the large allocations - reject on
      // the cached bounds before paying for the full list scan. The bounds
      // are of the starts, so a pointer into the highest reaches past them
      size_t reach = outBase ? sgCheckInternalOffset : 0;
      size_t p = (size_t)inPtr;
      if (p < mLargeMin || p > mLargeMax + reach)
         return memUnmanaged;
      uintptr_t base = 0;

      // Only the collector looks up while marking, with the list fixed, so once
      // a collection has seen several candidates in range it sorts the bases
      // and searches them instead of scanning the list for each. The old
      // allocations' sorted bases are kept while they do not change, which a
      // minor collection cannot do: sorting them again in each collection
      // took longer than sweeping them did (libhxcpp #318).
      if (!mLargeIndexReady && mLargeOld>64 && ++mLargeLookups>8)
      {
         mLargeIndex.resize(mLargeOld);
         for(int i=0;i<mLargeOld;i++)
            mLargeIndex[i] = reinterpret_cast<uintptr_t>(mLargeList[i] + 2);
         std::sort(mLargeIndex.begin(), mLargeIndex.end());
         mLargeIndexReady = true;
      }
      if (mLargeIndexReady)
         base = FindLarge(mLargeIndex, inPtr, reach);
      else
         for(int i=0;i<mLargeOld && !base;i++)
            if (IsLargeAt(reinterpret_cast<uintptr_t>(mLargeList[i] + 2), inPtr, reach))
               base = reinterpret_cast<uintptr_t>(mLargeList[i] + 2);
      if (base)
      {
         if (outBase)
            *outBase = base;
         return memLarge;
      }

      // Those made since the last collection, which change in each
      int young = mLargeList.size() - mLargeOld;
      if (!mYoungLargeIndexReady && young>64 && ++mYoungLargeLookups>8)
      {
         mYoungLargeIndex.resize(young);
         for(int i=0;i<young;i++)
            mYoungLargeIndex[i] = reinterpret_cast<uintptr_t>(mLargeList[mLargeOld+i] + 2);
         std::sort(mYoungLargeIndex.begin(), mYoungLargeIndex.end());
         mYoungLargeIndexReady = true;
      }
      if (mYoungLargeIndexReady)
         base = FindLarge(mYoungLargeIndex, inPtr, reach);
      else
         for(int i=mLargeOld;i<mLargeList.size() && !base;i++)
         {
            uintptr_t blob{ reinterpret_cast<uintptr_t>(mLargeList[i] + 2) };
            if (IsLargeAt(blob, inPtr, reach))
               base = blob;
         }

      if (!base)
         return memUnmanaged;
      if (outBase)
         *outBase = base;
      return memLarge;
   }


   size_t mRowsInUse;
   size_t mCurrentRowsInUse;
   std::atomic_size_t mLargeAllocSpace;
   size_t mLargeAllocForceRefresh;
   // Read unlocked to decide on a collection, changed under mLargeListLock
   std::atomic_size_t mLargeAllocated;
   // Large and native allocations since the last full mark, under
   //  mLargeListLock, and how much they may add up to before the next
   size_t mLargeSinceFull = 0;
   size_t mLargeFullBudget = sLargeAllocSpaceFloor;
   size_t mLargeMin;
   size_t mLargeMax;
   // mLargeList[0..mLargeOld) survived a collection, and the rest are newer.
   //  The bounds are of the old ones, at the last sweep
   int mLargeOld;
   size_t mLargeOldMin;
   size_t mLargeOldMax;
   // An old allocation was released for the sweep to free
   bool mLargeOldReleased;
   // The sorted bases of mLargeList[0..mLargeOld), and of the rest
   std::vector<uintptr_t> mLargeIndex;
   bool mLargeIndexReady = false;
   int mLargeLookups = 0;
   // The new survivors' bases, taken for the index before a full sweep
   std::vector<uintptr_t> mLargeIndexNew;
   std::vector<uintptr_t> mYoungLargeIndex;
   bool mYoungLargeIndexReady = false;
   int mYoungLargeLookups = 0;
   size_t mTotalAfterLastCollect;
   size_t mAllBlocksCount;
   size_t mLiveBytes;
   double mGenerationalRetainEstimate;

   hx::MarkContext mMarker;

   #ifdef HXCPP_GC_INCREMENTAL
   std::atomic_size_t mIncrementalStartAt{hx::sgMinimumWorkingMemory/2};
   std::atomic_size_t mIncrementalPressureAt{hx::sgMinimumWorkingMemory*3/4};
   bool mZeroingLeft = false;
   size_t mReclaimAhead = 0;
   std::atomic_size_t mIncrementalAllocated{0};
   #endif

   volatile int mNextFreeBlockOfSize[BLOCK_OFSIZE_COUNT];
   std::atomic_size_t mThreadJobId;

   BlockList mAllBlocks;
   BlockList mFreeBlocks;
   BlockList mZeroList;
   std::atomic_int mZeroListQueue;

   LargeList mLargeList;
   hx::FastMutex mLargeListLock;
   hx::QuickVec<LocalAllocator *> mLocalAllocs;
   LocalAllocator *mLocalPool[LOCAL_POOL_SIZE];
   // Dead large buffers kept for reuse, by size, under mLargeListLock
   RecycleLists mRecycle;
   // Their data bytes, read unlocked for the reserved memory figure
   std::atomic_size_t mRecycleBytes;
   // The data bytes of dropped caches not freed yet
   std::atomic_size_t mDroppedBytes;
   #if !defined(HXCPP_SINGLE_THREADED_APP) && !defined(__EMSCRIPTEN__)
   std::mutex mReleaseLock;
   std::condition_variable mReleaseWake;
   std::condition_variable mReleaseIdle;
   std::vector<RecycleLists> mReleaseQueue;
   bool mReleaseBusy = false;
   bool mReleaseStarted = false;
   #endif

   void ForEachStackContext(void (*inFunc)(hx::StackContext *, void *), void *inData);
   // Lock-free probe for AllocLarge - only read unlocked, maintained under
   // mLargeListLock (or stop-the-world in the sweep)
   std::atomic_int mRecycleSize;
};



namespace hx
{

MarkChunk *MarkChunk::swapForNew()
{
   return sGlobalChunks.pushJobNoWake(this);
}



HX_NO_SANITIZE_ADDRESS
void MarkConservative(int *inBottom, int *inTop,hx::MarkContext *__inCtx)
{
   #ifdef VERIFY_STACK_READ
   VerifyStackRead(inBottom, inTop);
   #endif

   #ifdef SHOW_MEM_EVENTS
   GCLOG("Mark conservative %p...%p (%d) [...", inBottom, inTop, (int)(inTop-inBottom) );
     #ifdef HX_WATCH
     GCLOG("\n");
     #endif
   #endif

   #ifdef HXCPP_STACK_UP
   int *start = inTop-1;
   inTop = inBottom+1;
   inBottom = start;
   #endif

   if (sizeof(int)==4 && sizeof(void *)==8)
   {
      // Can't start pointer on last integer boundary...
      inTop--;
   }

   uintptr_t prev{};
   uintptr_t lastPin{};
   #ifdef HX_WATCH
   uintptr_t lastWatch{};
   bool isWatch{};
   #endif

   #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
   // If this is a generational mark, then the byte marker has not been increased.
   // Previous mark Ids are therfore from more than 1 collection ago
   bool allowPrevious{ !__inCtx->isGenerational };
   #else
   const bool allowPrevious{ true };
   #endif


   for(int *ptr = inBottom ; ptr<inTop; ptr++)
   {
      void *word;
      memcpy(&word, ptr, sizeof(word));
      uintptr_t potentialObject{ reinterpret_cast<uintptr_t>(word) };

      // Objects start 8-byte aligned with HXCPP_ALIGN_ALLOC, but a pointer to an
      //  Int field inside one need only be 4-byte aligned
      const size_t validObjectMask{ 0x03 };

      if (potentialObject && !(potentialObject & validObjectMask) && potentialObject != prev && potentialObject != lastPin)
      {
         // Last-value cache - adjacent stack slots often hold the same
         // pointer (spills, argument copies), and without this assignment
         // the prev test above never fires
         prev = potentialObject;

         #ifdef PROFILE_COLLECT
         hx::localCount++;
         #endif
         uintptr_t largeBase{};
         MemType mem{ sGlobalAlloc->GetMemType(potentialObject, &largeBase) };

         #ifdef HX_WATCH
         isWatch = false;
         if (hxInWatchList(potentialObject) && potentialObject !=lastWatch)
         {
            isWatch = true;
            lastWatch = potentialObject;
            GCLOG("********* Watch location conservative mark %p:%d\n", reinterpret_cast<void*>(potentialObject), mem);
         }
         #endif

         if (mem!=memUnmanaged)
         {
            if (mem==memLarge)
            {
               // A pointer into it, as into an object in a block, marks it
               potentialObject = largeBase;
               #ifdef HXCPP_GC_INCREMENTAL
               unsigned int flags = reinterpret_cast<unsigned int *>(potentialObject)[-1];
               if (flags & IMMIX_ALLOC_IS_CONTAINER)
                  hx::MarkObjectAlloc(reinterpret_cast<hx::Object *>(potentialObject),__inCtx);
               else hx::MarkAlloc(reinterpret_cast<void *>(potentialObject),__inCtx);
               #else
               unsigned char& mark{ reinterpret_cast<unsigned char*>(potentialObject)[HX_ENDIAN_MARK_ID_BYTE] };
               // A large object's members are marked as from any other root:
               //  setting its mark kept the object but none of what it held
               if (reinterpret_cast<unsigned int*>(potentialObject)[-1] & IMMIX_ALLOC_IS_CONTAINER)
                  hx::MarkObjectAlloc(reinterpret_cast<hx::Object*>(potentialObject), __inCtx);
               else if (mark != gByteMarkID)
               {
                  if (sgCensusDeferred)
                     sgCensusDeferred->push_back(std::make_pair(reinterpret_cast<void*>(potentialObject), true));
                  else
                     mark = static_cast<unsigned char>(gByteMarkID);
               }
               #endif
            }
            else
            {
               BlockData* block{ reinterpret_cast<BlockData*>(potentialObject & IMMIX_BLOCK_BASE_MASK) };
               BlockDataInfo* info{ (*gBlockInfo)[block->mId] };

               size_t pos{ potentialObject & IMMIX_BLOCK_OFFSET_MASK };
               AllocType t{
                  sgCheckInternalOffset ?
                     info->GetEnclosingAllocType(pos - sizeof(int), &potentialObject, allowPrevious) :
                     info->GetAllocType(pos - sizeof(int), allowPrevious) };

               #ifdef HX_WATCH
               if (!isWatch && hxInWatchList(potentialObject))
               {
                  isWatch = true;
                  GCLOG("********* Watch location conservative mark offset %p:%d\n", reinterpret_cast<void*>(potentialObject), mem);
               }
               #endif


               if ( t==allocObject )
               {
                  #ifdef HX_WATCH
                  if (isWatch)
                  {
                     GCLOG(" Mark object %p (%p)\n", reinterpret_cast<void*>(potentialObject), ptr);
                  }
                  #endif
                  hx::MarkObjectAlloc( reinterpret_cast<hx::Object*>(potentialObject), __inCtx );
                  lastPin = potentialObject;
                  info->pin();
               }
               else if (t==allocString)
               {
                  #ifdef HX_WATCH
                  if (isWatch)
                     GCLOG(" Mark string %p (%p)\n", reinterpret_cast<void*>(potentialObject), ptr);
                  #endif
                  // An object with no pointer fields looks like this too, so a
                  //  census or Gc.trace marks it last, after a reference may
                  //  have counted it
                  if (sgCensusDeferred)
                     sgCensusDeferred->push_back(std::make_pair(reinterpret_cast<void*>(potentialObject), false));
                  else
                     HX_MARK_STRING(reinterpret_cast<void*>(potentialObject));
                  lastPin = potentialObject;
                  info->pin();
               }
               else if (t==allocMarked)
               {
                  #ifdef HXCPP_GC_INCREMENTAL_VERIFY
                  if (__inCtx->incrementalOracle) {
                     unsigned int flags = reinterpret_cast<unsigned int *>(potentialObject)[-1];
                     hx::IncrementalOracleMark(reinterpret_cast<void *>(potentialObject),(flags & IMMIX_ALLOC_IS_CONTAINER)!=0,__inCtx);
                  }
                  #endif
                  #ifdef HX_WATCH
                  if (isWatch)
                     GCLOG(" pin alloced %p (%p)\n", reinterpret_cast<void*>(potentialObject), ptr);
                  #endif
                  lastPin = potentialObject;
                  info->pin();
               }
               #ifdef HX_WATCH
               else // memBlock
               {
                  if (isWatch)
                  {
                     GCLOG(" missed watch %p:%d\n", reinterpret_cast<void*>(potentialObject), t);
                     int x = info->GetAllocType(pos-sizeof(int),allowPrevious);
                     int y = info->GetEnclosingAllocType(pos-sizeof(int),&potentialObject,allowPrevious);
                     #ifdef HXCPP_GC_NURSERY
                     int z = info->GetEnclosingNurseryType(pos-sizeof(int),nullptr);
                     #else
                     int z = 0;
                     #endif
                     printf("but got alloc type=%d, enclosing=%d nurs=%d o=%" PRIdPTR "\n", x, y, z, sgCheckInternalOffset);
                  }
               }
               #endif
            }
         }
         // GCLOG(" rejected %p %p %d %p %d=%d\n", ptr, vptr, !((size_t)vptr & 0x03), prev,
         // sGlobalAlloc->GetMemType(vptr) , memUnmanaged );
      }
   }
   #ifdef SHOW_MEM_EVENTS
   GCLOG("...]\n");
   #endif
}

} // namespace hx


// --- LocalAllocator -------------------------------------------------------
//
// One per thread ...

static int sFragIgnore=0;

class LocalAllocator : public hx::StackContext
{
   uint8_t        mCurrentHole;
   uint8_t        mCurrentHoles;
   HoleRange     *mCurrentRange;
   uint16_t      *mFraggedRows;

   bool           mMoreHoles;

   #ifndef HXCPP_EXPLICIT_STACK_EXTENT
   int *mTopOfStack;
   int *mBottomOfStack;
   #endif

   hx::RegisterCaptureBuffer mRegisterBuf;
   int                   mRegisterBufSize;

   #ifndef HXCPP_SINGLE_THREADED_APP
   // Atomic so that a collector which reads true also sees what this thread
   //  wrote before entering the zone: its registers, its stack and anything
   //  it changed under another lock, such as the finalizer map
   std::atomic<bool> mGCFreeZone;
   // True while our mReadyForCollect event may be set - lets the zone exit
   // clear it without taking the global state lock
   bool            mReadySignalled;
   HxSemaphore     mReadyForCollect;
   HxSemaphore     mCollectDone;
   // True from just before PauseForCollect signals mReadyForCollect until its
   // wait for mCollectDone ends
   volatile bool   mPausedForCollect;
   // Set by the collector when it found us paused in PauseForCollect, so that
   // ReleaseFromSafe signals mCollectDone, which we are waiting for
   bool            mReleaseWhenDone;
   #endif

   int             mID;


   // Must be called locked
   ~LocalAllocator()
   {
   }

public:
   bool            mGlobalStackLock;
   int             mStackLocks;

public:
   LocalAllocator(int *inTopOfStack=0)
   {
      Reset();

      // The fields remain in the barrier-compatible layout for incremental
      // builds, even though that mode never creates generational logs.
      #ifdef HXCPP_GC_GENERATIONAL
      mOldReferrers = 0;
      mYoungValues = 0;
      #endif

      AttachThread(inTopOfStack);
   }


   void AttachThread(int *inTopOfStack)
   {
      #ifndef HXCPP_EXPLICIT_STACK_EXTENT
      mTopOfStack = mBottomOfStack = inTopOfStack;
      #endif

      mRegisterBufSize = 0;
      mStackLocks = 0;
      mGlobalStackLock = false;
      #ifdef HX_WINDOWS
      mID = GetCurrentThreadId();
      #endif

      #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
      if (mOldReferrers)
      {
         GCLOG("Uncleaned referrers\n");
      }
      #endif


      // It is in the free zone - wait for 'SetTopOfStack' to activate
      #ifndef HXCPP_SINGLE_THREADED_APP
      mPausedForCollect = false;
      mReleaseWhenDone = false;
      mGCFreeZone = true;
      mReadyForCollect.Set();
      mReadySignalled = true;
      #endif
      sGlobalAlloc->AddLocal(this);
   }

   void Release()
   {
      mStackLocks = 0;

      onThreadDetach();

      #ifndef HXCPP_SINGLE_THREADED_APP
      if (!mGCFreeZone)
         EnterGCFreeZone();
      #endif

      std::lock_guard<std::recursive_mutex> lock(*gThreadStateChangeLock);

      #ifdef HXCPP_GC_INCREMENTAL
      hx::IncrementalTransfer(this);
      #endif

      #ifdef HX_WINDOWS
      mID = 0;
      #endif

      #ifdef HXCPP_GC_GENERATIONAL_RUNTIME
      if (mOldReferrers)
      {
         if ( mOldReferrers->count )
            hx::sGlobalChunks.pushJob( mOldReferrers, false );
         else
            hx::sGlobalChunks.free( mOldReferrers );
         mOldReferrers = 0;
      }
      {
         std::lock_guard<hx::FastMutex> lock(hx::sYoungValueLock);
         hx::TakeYoungValues( mYoungValues );
      }
      #endif

      #ifndef HXCPP_EXPLICIT_STACK_EXTENT
      mTopOfStack = mBottomOfStack = 0;
      #endif

      sGlobalAlloc->RemoveLocalLocked(this);

      hx::tlsStackContext = 0;

      if (!sGlobalAlloc->ReturnToPoolLocked(this))
         delete this;
   }

   void Reset()
   {
      allocBase = 0;
      mCurrentHole = 0;
      mCurrentHoles = 0;
      mFraggedRows = 0;
      #ifdef HXCPP_GC_NURSERY
      spaceFirst = 0;
      spaceOversize = 0;
      #else
      spaceEnd = 0;
      spaceStart = 0;
      #endif
      mMoreHoles = false;
   }

   #ifdef HXCPP_GC_INCREMENTAL
   void RestoreIncrementalRange()
   {
      // WaitForSafe invalidates these bounds to park a running allocator.
      // A partial mark did not reclaim anything, so its owned hole is still
      // usable. Dropping it on every slice wastes reserve and forces more GC.
      if (allocBase && mCurrentHole && mCurrentHole <= mCurrentHoles)
      {
         const HoleRange &range = mCurrentRange[mCurrentHole - 1];
         spaceEnd = range.start + range.length;
         mMoreHoles = mCurrentHole < mCurrentHoles;
      }
   }
   #endif

   // The main of haxe calls SetTopOfStack(top,false)
   //   via hxcpp_set_top_of_stack or HX_TOP_OF_STACK in HxcppMain.cpp/Macros.h
   // The means "register current thread indefinitely"
   // Other places may call this to ensure the the current thread is registered
   //  indefinitely (until forcefully revoked)
   //
   // Normally libraries/mains will then let this dangle.
   //
   // However after the main, on android it calls SetTopOfStack(0,true), to unregister the thread,
   // because it is likely to be the ui thread, and the remaining call will be from
   // callbacks from the render thread.
   // 
   // When threads want to attach temporarily, they will call
   // gc_set_top_of_stack(top,true)
   //  -> SetTopOfStack(top,true)
   //    do stuff...
   // gc_set_top_of_stack(0,true)
   //  -> SetTopOfStack(0,true)
   //
   //  OR
   //
   //  PushTopOfStack(top)
   //   ...
   //  PopTopOfStack
   //
   //  However, we really want the gc_set_top_of_stack(top,true) to allow recursive locks so:
   //
   //  SetTopOfStack(top,false) -> ensure global stack lock exists
   //  SetTopOfStack(top,true) -> add stack lock
   //  SetTopOfStack(0,_) -> pop stack lock. If all gone, clear global stack lock
   //

   #ifdef HXCPP_EXPLICIT_STACK_EXTENT // {

   void SetTopOfStack(int *inTop,bool inPush) { }
   void PushTopOfStack(void *inTop) { }
   void PopTopOfStack() { }
   void SetBottomOfStack(int *inBottom) { }
   void PauseForCollect() { }
   void EnterGCFreeZone() { }
   bool TryGCFreeZone() { return true; }
   bool TryExitGCFreeZone() { return false; }
   void ExitGCFreeZoneLocked() { }


   #else // } !HXCPP_EXPLICIT_STACK_EXTENT {
   void SetTopOfStack(int *inTop,bool inPush)
   {
      if (inTop)
      {
         if (!mTopOfStack)
            mTopOfStack = inTop;
         // EMSCRIPTEN the stack grows upwards - not wasm.
         // It could be that the main routine was called from deep with in the stack,
         //  then some callback was called from a higher location on the stack
         #ifdef HXCPP_STACK_UP
         else if (inTop < mTopOfStack)
            mTopOfStack = inTop;
         #else
         else if (inTop > mTopOfStack)
            mTopOfStack = inTop;
         #endif

         if (inPush)
            mStackLocks++;
         else
            mGlobalStackLock = true;

         #ifndef HXCPP_SINGLE_THREADED_APP
         if (mGCFreeZone)
            ExitGCFreeZone();
         #endif
      }
      else
      {
         if (mStackLocks>0)
            mStackLocks--;
         else
            mGlobalStackLock = false;

         if (!mStackLocks && !mGlobalStackLock)
         {
            Release();
         }
      }

      #ifdef VerifyStackRead
      VerifyStackRead(mBottomOfStack, mTopOfStack)
      #endif
   }


   void PushTopOfStack(void *inTop)
   {
      SetTopOfStack((int *)inTop,true);
   }


   void PopTopOfStack()
   {
      mStackLocks--;
      if (mStackLocks<=0 && !mGlobalStackLock)
      {
         Release();
      }
   }



   void SetBottomOfStack(int *inBottom)
   {
      mBottomOfStack = inBottom;
      #ifdef VerifyStackRead
      VerifyStackRead(mBottomOfStack, mTopOfStack)
      #endif
   }


   void PauseForCollect()
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      volatile int dummy = 1;
      mBottomOfStack = (int *)&dummy;
      CAPTURE_REGS;
      #ifdef VerifyStackRead
      VerifyStackRead(mBottomOfStack, mTopOfStack)
      #endif

      if (sgIsCollecting)
         CriticalGCError("Bad Allocation while collecting - from finalizer?");

      mPausedForCollect = true;
      mReadyForCollect.Set();
      mCollectDone.Wait();
      mPausedForCollect = false;
      #endif
   }

   void EnterGCFreeZone()
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      volatile int dummy = 1;
      mBottomOfStack = (int *)&dummy;
      if (mTopOfStack)
      {
         CAPTURE_REGS;
      }
      #ifdef VerifyStackRead
      VerifyStackRead(mBottomOfStack, mTopOfStack)
      #endif

      mGCFreeZone = true;
      // The collector announces itself by setting gPauseForCollect (a full
      // atomic op) before it reads mGCFreeZone.  With the store-load fence
      // here, at least one side observes the other: either the collector
      // sees us safely in the zone, or we see the pending collection and
      // signal.  When no collection is pending this skips the kernel-event
      // call entirely.
      std::atomic_thread_fence(std::memory_order_seq_cst);
      if (hx::gPauseForCollect.load(std::memory_order_relaxed))
      {
         mReadyForCollect.Set();
         mReadySignalled = true;
      }
      #endif
   }

   bool TryGCFreeZone()
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      if (mGCFreeZone)
         return false;
      EnterGCFreeZone();
      #endif
      return true;
   }

   bool TryExitGCFreeZone()
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      if (!mGCFreeZone)
         return false;
      ExitGCFreeZone();
      return true;
      #endif
      return false;
   }


   void ExitGCFreeZone()
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      if (!mGCFreeZone)
         CriticalGCError("GCFree Zone mismatch");

      // Clear any signal we own before leaving the zone, so a collector
      // that observes us outside it cannot consume a stale event and scan
      // while we run
      if (mReadySignalled)
      {
         mReadyForCollect.Reset();
         mReadySignalled = false;
      }

      mGCFreeZone = false;
      std::atomic_thread_fence(std::memory_order_seq_cst);
      // Acquire: a collection that ran while the thread was in the zone ended
      //  by clearing gPauseForCollect, and what it did to the thread's objects
      //  must be seen before the thread uses them. The fence above orders the
      //  store before this load, but does not make a relaxed load acquire
      if (hx::gPauseForCollect.load(std::memory_order_acquire))
      {
         // A collection is pending or running.  The collector either saw us
         // in the zone (and expects us to stay parked) or is waiting on our
         // event - re-enter the zone, signal, and queue on the state lock,
         // which the collector holds until the collect completes.
         mGCFreeZone = true;
         mReadyForCollect.Set();
         mReadySignalled = true;
         std::lock_guard<std::recursive_mutex> lock(*gThreadStateChangeLock);
         mReadyForCollect.Reset();
         mReadySignalled = false;
         mGCFreeZone = false;
      }
      // The process is exiting (ParkThreadsForExit) and only the exiting thread
      //  runs on: this one stays in the zone, on the state lock that is held for good
      if (sgParkedForExit.load(std::memory_order_acquire) && this!=sgExitAlloc)
      {
         mGCFreeZone = true;
         gThreadStateChangeLock->lock();
      }
      #endif
   }
        // For when we already hold the lock
   void ExitGCFreeZoneLocked()
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      mReadyForCollect.Reset();
      mReadySignalled = false;
      mGCFreeZone = false;
      #endif
   }



   // Called by the collecting thread to make sure this allocator is paused.
   // The collecting thread has the lock, and will not be releasing it until
   //  it has finished the collect.
   //
   //  The mGCFreeZone is set without a lock in the EnterGCFreeZone code, and then
   //   mReadyForCollect is set.  So it is possible the mGCFreeZone check may or may not
   //   trigger.  If this call happens first, mGCFreeZone will be zero, and mReadyForCollect
   //   will wait.  By this time mGCFreeZone will be set and the next call not check
   //   mReadyForCollect again.  If this one happens later, it is possible mReadyForCollect
   //   will not be waited on. mReadyForCollect will be cleared when the zone is left.
   //
   //  The mMoreHoles/spaceOversize/spaceEnd get zeroed without a lock.  The timing should
   //   not be critical since the allocation code shold expect that these are volatile.
   //   If the allocation works, all is good.  If it fails then the collection will happed soon.
   #if defined(__has_feature)
     #if __has_feature(thread_sanitizer)
      __attribute__((no_sanitize("thread")))
     #endif
   #endif
   void WaitForSafe()
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      mReleaseWhenDone = false;
      if (!mGCFreeZone)
      {
         // Cause allocation routines to fail ...
         mMoreHoles = false;
         #ifdef HXCPP_GC_NURSERY
         spaceOversize = 0;
         #else
         spaceEnd = 0;
         #endif
         mReadyForCollect.Wait();
         // Signalled by PauseForCollect, which now waits for mCollectDone, or by
         //  the thread entering a GC-free zone, which does not
         mReleaseWhenDone = mPausedForCollect;
      }
      #endif
   }

   // WaitForSafe with a limit, for ParkThreadsForExit: a thread in a loop that
   //  never allocates reaches no safe point. A thread that does stays waiting in
   //  PauseForCollect, as nothing releases it.
   #if defined(__has_feature)
     #if __has_feature(thread_sanitizer)
      __attribute__((no_sanitize("thread")))
     #endif
   #endif
   void WaitForSafeUntil(double inSeconds)
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      if (!mGCFreeZone)
      {
         mMoreHoles = false;
         #ifdef HXCPP_GC_NURSERY
         spaceOversize = 0;
         #else
         spaceEnd = 0;
         #endif
         mReadyForCollect.WaitSeconds(inSeconds > 0 ? inSeconds : 0);
      }
      #endif
   }

   // Signals only a thread this collection found paused in PauseForCollect.
   //  mGCFreeZone is no guide here: a thread that was in a zone throughout may
   //  have left it since gPauseForCollect was cleared, or be briefly out of it
   //  on its way to block on the state lock. An mCollectDone it is not waiting
   //  for stays set, so its next PauseForCollect would return at once and run
   //  through that collection.
   void ReleaseFromSafe()
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      if (mReleaseWhenDone)
      {
         mReleaseWhenDone = false;
         mCollectDone.Set();
      }
      #endif
   }

   #endif // }  HXCPP_EXPLICIT_STACK_EXTENT


   void SetupStackAndCollect(bool inMajor, bool inForceCompact, bool inLocked=false,bool inFreeIsFragged=false) HXCPP_OVERRIDE
   {
      SetupStackAndCollectFlags( (inMajor ? collectMajor : 0) | (inForceCompact ? collectCompact : 0) |
                                 (inLocked ? collectLocked : 0) | (inFreeIsFragged ? collectFreeIsFragged : 0) );
   }

   // The flags come in one register. SetupStackAndCollect's fifth argument went
   //  on the stack, in the range this thread's own scan covers, and MSVC stores
   //  a bool there as one byte over what the slot held: a stale heap address
   //  with its low byte cleared can point at an object, which then stayed
   //  alive through every collection this thread made from that depth
   //  (libhxcpp #367). The runtime calls this, so no argument it passes is on
   //  the stack above the scan's bottom
   void SetupStackAndCollectFlags(int inFlags)
   {
      bool inMajor = inFlags & collectMajor;
      bool inForceCompact = inFlags & collectCompact;
      bool inLocked = inFlags & collectLocked;
      bool inFreeIsFragged = inFlags & collectFreeIsFragged;

      #ifndef HXCPP_SINGLE_THREADED_APP
        #if HXCPP_DEBUG
        if (mGCFreeZone)
           CriticalGCError("Collecting from a GC-free thread");
        #endif
      #endif

      #ifndef HXCPP_EXPLICIT_STACK_EXTENT
      volatile int dummy = 1;
      mBottomOfStack = (int *)&dummy;

      CAPTURE_REGS;

      if (!mTopOfStack)
         mTopOfStack = mBottomOfStack;

      // EMSCRIPTEN the stack grows upwards
      #ifdef HXCPP_STACK_UP
      if (mBottomOfStack < mTopOfStack)
         mTopOfStack = mBottomOfStack;
      #else
      if (mBottomOfStack > mTopOfStack)
         mTopOfStack = mBottomOfStack;
      #endif

      #ifdef VerifyStackRead
      VerifyStackRead(mBottomOfStack, mTopOfStack)
      #endif

      #endif


      sGlobalAlloc->Collect(inMajor, inForceCompact, inLocked, inFreeIsFragged);
   }



   void ExpandAlloc(size_t& ioSize)
   {
      #ifdef HXCPP_ALIGN_ALLOC
      // Do nothing here - aligning to the end of the row will bump the
      // next allocation, so it's not clear if its a good idea.
      #else
         #ifdef HXCPP_GC_NURSERY
         int spaceStart = spaceFirst - allocBase - 4;
         int spaceEnd = spaceOversize - allocBase - 4;
         #endif

         size_t size{ ioSize + sizeof(int) };
         size_t end{ spaceStart + size };
         if (end <= spaceEnd)
         {
            size_t linePad{ IMMIX_LINE_LEN - (end & (IMMIX_LINE_LEN - 1)) };
            if (linePad>0 && linePad<=64)
               ioSize += linePad;
         }
      #endif
   }


   void *CallAlloc(size_t inSize, unsigned int inObjectFlags) HXCPP_OVERRIDE
   {
      return DoAlloc<true>(inSize, inObjectFlags);
   }

   // String data holds no atomics or doubles, so it need not start 8-byte
   //  aligned: a short string takes 4 bytes less on average (libhxcpp #370)
   void *CallAllocUnaligned(size_t inSize)
   {
      return DoAlloc<false>(inSize, 0);
   }

   template<bool ALIGN>
   void *DoAlloc(size_t inSize, unsigned int inObjectFlags)
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      #if HXCPP_DEBUG
      if (mGCFreeZone)
         CriticalGCError("Allocating from a GC-free thread");
      #endif
      if (hx::gPauseForCollect)
         PauseForCollect();
      #endif

      if (inSize==0)
         return hx::emptyAlloc;

      #if defined(HXCPP_VISIT_ALLOCS) && (defined(HXCPP_M64)||defined(HXCPP_ARM64))
      // Make sure we can fit a relocation pointer
      // I think we could use something like sizeof(void*) instead of 8.
      size_t allocSize{ sizeof(int) + std::max(size_t{ 8 }, inSize) };
      #else
      size_t allocSize{ sizeof(int) + inSize };
      #endif

      #if HXCPP_GC_DEBUG_LEVEL>0
      if (inSize & 3) DebuggerTrap();
      #endif

      while(1)
      {
         #ifdef HXCPP_GC_NURSERY
            unsigned char* buffer{ spaceFirst };
            #ifdef HXCPP_ALIGN_ALLOC
            if (ALIGN && (reinterpret_cast<uintptr_t>(buffer) & 0x4))
               buffer += 4;
            #endif
            unsigned char *end = buffer + allocSize;

            if ( end <= spaceOversize )
            {
               spaceFirst = end;

               reinterpret_cast<unsigned int*>(buffer)[-1] = static_cast<unsigned int>(allocSize) - 4 | inObjectFlags;

               #if defined(HXCPP_GC_CHECK_POINTER) && defined(HXCPP_GC_DEBUG_ALWAYS_MOVE)
               hx::GCOnNewPointer(buffer, ALIGN);
               #endif

               return buffer;
            }
            // spaceOversize might have been set to zero for quick-termination of alloc.
            unsigned char* s{ *static_cast<unsigned char * volatile *>(&spaceOversize) };
            if (s>spaceFirst && mFraggedRows)
               *mFraggedRows += static_cast<uint16_t>((s - spaceFirst) >> IMMIX_LINE_BITS);
         #else
            #ifdef HXCPP_ALIGN_ALLOC
            if (ALIGN && !(size_t{ spaceStart } & 0x4))
               spaceStart += 4;
            #endif

            size_t end{ allocSize + spaceStart };
            if (end <= spaceEnd)
            {
               unsigned int* buffer{ reinterpret_cast<unsigned int*>(allocBase + spaceStart) };

               size_t startRow{ spaceStart >> IMMIX_LINE_BITS };
               size_t endRow{ (end + (IMMIX_LINE_LEN - 1)) >> IMMIX_LINE_BITS };

               allocStartFlags[startRow] |= hx::gImmixStartFlag[spaceStart & 127];

               *buffer++ =
                   inObjectFlags |
                   hx::gMarkID |
                   static_cast<unsigned int>(inSize << IMMIX_ALLOC_SIZE_SHIFT) |
                   static_cast<unsigned int>(endRow - startRow);

               spaceStart = end;

               #if defined(HXCPP_GC_CHECK_POINTER) && defined(HXCPP_GC_DEBUG_ALWAYS_MOVE)
               hx::GCOnNewPointer(buffer, ALIGN);
               #endif

               #ifdef HXCPP_TELEMETRY
               __hxt_gc_alloc(buffer, inSize);
               #endif

               #ifdef HXCPP_GC_INCREMENTAL
               if (hx::gIncrementalActive.load(std::memory_order_relaxed))
                  hx::IncrementalAllocated(buffer,(inObjectFlags & IMMIX_ALLOC_IS_CONTAINER)!=0,this);
               #endif

               return buffer;
            }
            // Read once: the collecting thread zeroes spaceEnd in WaitForSafe
            size_t holeEnd{ *static_cast<volatile size_t *>(&spaceEnd) };
            if (mFraggedRows && holeEnd > spaceStart)
            {
               *mFraggedRows += static_cast<uint16_t>((holeEnd - spaceStart) >> IMMIX_LINE_BITS);
            }
         #endif


         if (mMoreHoles)
         {
            #ifdef HXCPP_GC_NURSERY
            spaceFirst = allocBase + mCurrentRange[mCurrentHole].start + sizeof(int);
            spaceOversize = spaceFirst + mCurrentRange[mCurrentHole].length;
            #else
            spaceStart = mCurrentRange[mCurrentHole].start;
            spaceEnd = spaceStart + mCurrentRange[mCurrentHole].length;
            #endif
            mCurrentHole = saturating_add<uint8_t>(mCurrentHole, 1);
            mMoreHoles = mCurrentHole<mCurrentHoles;

         }
         else
         {
            // For opmtimized windows 64 builds, this dummy var technique does not
            //  quite work, since the compiler might recycle one of the earlier stack
            //  slots, and place dummy behind the stack values we are actually trying to
            //  capture.  Moving the dummy into the GetFreeBlock seems to have fixed this.
            //  Not 100% sure this is the best answer, but it is working.
            //volatile int dummy = 1;
            //mBottomOfStack = (int *)&dummy;
            //CAPTURE_REGS;

            BlockDataInfo *info = sGlobalAlloc->GetFreeBlock(allocSize,this);

            allocBase = reinterpret_cast<unsigned char*>(info->mPtr);
            mCurrentRange = info->mRanges;
            allocStartFlags = info->allocStart;
            mCurrentHoles = info->mHoles;
            mFraggedRows = &info->mFraggedRows;
            #ifdef HXCPP_GC_NURSERY
            spaceFirst = allocBase + mCurrentRange->start + sizeof(int);
            spaceOversize = spaceFirst + mCurrentRange->length;
            #else
            spaceStart = mCurrentRange->start;
            spaceEnd = spaceStart + mCurrentRange->length;
            #endif
            mCurrentHole = 1;
            mMoreHoles = mCurrentHole<mCurrentHoles;

            // From a collection GetFreeBlock made: the block is ours now
            hx::ThrowFinalizerException();
         }

         // Other thread may have started collect, in which case we may just
         //  overwritted the 'mMoreHoles' and 'spaceEnd' termination attempt
         if (hx::gPauseForCollect)
         {
            mMoreHoles = 0;
            #ifdef HXCPP_GC_NURSERY
            spaceOversize = 0;
            #else
            spaceEnd = 0;
            #endif
         }
      }
      return 0;
   }

   #ifdef HXCPP_VISIT_ALLOCS
   void Visit(hx::VisitContext *__inCtx)
   {
      #ifdef HXCPP_COMBINE_STRINGS
      if (stringSet)
      {
         __inCtx->visitObject( (hx::Object **)&stringSet);
      }
      #endif
   }
   #endif


   void Mark(hx::MarkContext *__inCtx)
   {
      #ifndef HXCPP_SINGLE_THREADED_APP
      if (!mTopOfStack)
      {
         #ifdef HXCPP_GC_INCREMENTAL
         if (hx::gIncrementalActive.load(std::memory_order_relaxed)) return;
         #endif
         Reset();
         return;
      }
      #endif

      #ifdef SHOW_MEM_EVENTS
      //int here = 0;
      //GCLOG("=========== Mark Stack ==================== %p ... %p (%p)\n",mBottomOfStack,mTopOfStack,&here);
      #endif

      #ifdef HXCPP_DEBUG
         MarkPushClass("Stack",__inCtx);
         MarkSetMember("Stack",__inCtx);

         #ifdef HXCPP_EXPLICIT_STACK_EXTENT
           hx::MarkConservative( (int *)emscripten_stack_get_current(),(int *)emscripten_stack_get_base(), __inCtx);
         #else
         if (mTopOfStack && mBottomOfStack)
            hx::MarkConservative(mBottomOfStack, mTopOfStack , __inCtx);
         #endif

         #ifdef HXCPP_SCRIPTABLE
            MarkSetMember("ScriptStack",__inCtx);
            hx::MarkConservative((int *)(stack), (int *)(pointer),__inCtx);
         #endif
         MarkSetMember("Registers",__inCtx);
         hx::MarkConservative(CAPTURE_REG_START, CAPTURE_REG_END, __inCtx);


         MarkPopClass(__inCtx);
      #else

         #ifdef HXCPP_EXPLICIT_STACK_EXTENT
           hx::MarkConservative( (int *)emscripten_stack_get_current(), (int *) emscripten_stack_get_base(), __inCtx);
         #else
            if (mTopOfStack && mBottomOfStack)
               hx::MarkConservative(mBottomOfStack, mTopOfStack , __inCtx);
            hx::MarkConservative(CAPTURE_REG_START, CAPTURE_REG_END, __inCtx);
         #endif

         #ifdef HXCPP_SCRIPTABLE
            hx::MarkConservative((int *)(stack), (int *)(pointer),__inCtx);
         #endif
      #endif

      #ifdef HXCPP_COMBINE_STRINGS
         if (stringSet)
            MarkMember( *(hx::Object **)&stringSet, __inCtx);
      #endif

      #ifdef HXCPP_GC_INCREMENTAL
      if (!hx::gIncrementalActive.load(std::memory_order_relaxed))
      #endif
      Reset();

   }

};




inline LocalAllocator *GetLocalAlloc(bool inAllowEmpty=false)
{
   #ifndef HXCPP_SINGLE_THREADED_APP
      LocalAllocator *result = (LocalAllocator *)(hx::ImmixAllocator *)hx::tlsStackContext;
      if (!result && !inAllowEmpty)
         hx::BadImmixAlloc();

      return result;
   #else
      return (LocalAllocator *)hx::gMainThreadContext;
   #endif
}

#include "IncrementalApi.h"

#ifndef HXCPP_SINGLE_THREADED_APP
void WaitForSafe(LocalAllocator *inAlloc)
{
   inAlloc->WaitForSafe();
}

void WaitForSafeUntil(LocalAllocator *inAlloc, double inSeconds)
{
   inAlloc->WaitForSafeUntil(inSeconds);
}

void ReleaseFromSafe(LocalAllocator *inAlloc)
{
   inAlloc->ReleaseFromSafe();
}
#endif

void MarkLocalAlloc(LocalAllocator *inAlloc,hx::MarkContext *__inCtx)
{
   inAlloc->Mark(__inCtx);
}

void ClearPooledAlloc(LocalAllocator *inAlloc)
{
   inAlloc->Reset();
}

#ifdef HXCPP_GC_INCREMENTAL
void RestoreIncrementalAlloc(LocalAllocator *inAlloc)
{
   inAlloc->RestoreIncrementalRange();
}
#endif

void GlobalAllocator::ForEachStackContext(void (*inFunc)(hx::StackContext *, void *), void *inData)
{
   for(int i=0;i<mLocalAllocs.size();i++)
      inFunc(mLocalAllocs[i], inData);
   for(int i=0;i<LOCAL_POOL_SIZE;i++)
      if (mLocalPool[i])
         inFunc(mLocalPool[i], inData);
}

namespace hx
{
// Only while collecting, with every thread stopped
void GCForEachStackContext(void (*inFunc)(StackContext *, void *), void *inData)
{
   sGlobalAlloc->ForEachStackContext(inFunc, inData);
}

// Boot registers this with atexit (see GlobalAllocator::ParkThreadsForExit)
void ParkThreadsForExit()
{
   #ifndef HXCPP_SINGLE_THREADED_APP
   if (sGlobalAlloc)
      sGlobalAlloc->ParkThreadsForExit();
   #endif
}
}

#ifdef HXCPP_VISIT_ALLOCS
void VisitLocalAlloc(LocalAllocator *inAlloc,hx::VisitContext *__inCtx)
{
   inAlloc->Visit(__inCtx);
}
#endif



void CollectFromThisThread(bool inMajor,bool inForceCompact)
{
   LocalAllocator *la = GetLocalAlloc();
   la->SetupStackAndCollectFlags( (inMajor ? collectMajor : 0) | (inForceCompact ? collectCompact : 0) );
}

void CollectFromAlloc(hx::ImmixAllocator *inAlloc, int inFlags)
{
   ((LocalAllocator *)inAlloc)->SetupStackAndCollectFlags(inFlags);
}

namespace hx
{

void *ImmixAllocator::CallAlloc(size_t inSize,unsigned int inObjectFlags)
{
   return reinterpret_cast<LocalAllocator *>(this)->CallAlloc(inSize, inObjectFlags);
}



void PauseForCollect()
{
   GetLocalAlloc()->PauseForCollect();
}



void EnterGCFreeZone()
{
   #ifndef HXCPP_SINGLE_THREADED_APP
      LocalAllocator *tla = GetLocalAlloc();
      tla->EnterGCFreeZone();
   #endif
}


bool TryGCFreeZone()
{
   #ifndef HXCPP_SINGLE_THREADED_APP
      LocalAllocator *tla = GetLocalAlloc();
      return tla->TryGCFreeZone();
   #else
      return false;
   #endif
}

bool TryExitGCFreeZone()
{
   #ifndef HXCPP_SINGLE_THREADED_APP
      LocalAllocator *tla = GetLocalAlloc(true);
      if (!tla)
         return 0;
      bool left = tla->TryExitGCFreeZone();
      return left;
   #else
      return false;
   #endif
}


void ExitGCFreeZone()
{
   #ifndef HXCPP_SINGLE_THREADED_APP
      LocalAllocator *tla = GetLocalAlloc();
      tla->ExitGCFreeZone();
   #endif
}

void ExitGCFreeZoneLocked()
{
   #ifndef HXCPP_SINGLE_THREADED_APP
      LocalAllocator *tla = GetLocalAlloc();
      tla->ExitGCFreeZoneLocked();
   #endif
}

void InitAlloc()
{
   for(int i=0;i<IMMIX_LINE_LEN;i++)
      gImmixStartFlag[i] = 1<<( i>>2 ) ;

   hx::CommonInitAlloc();
   sgAllocInit = true;
   sGlobalAlloc = new GlobalAllocator();
   sgFinalizers = new FinalizerList();
   sgOldFinalizers = new FinalizerList();
   sFinalizerLock = new std::mutex();
   sGCRootLock = new std::mutex();
   sgRootSet = new RootSet();

   gMainThreadContext =  new LocalAllocator();

   tlsStackContext = gMainThreadContext;

   ExitGCFreeZone();

   // Setup main thread ...
   hx::thread::Thread_obj::current();

   gMainThreadContext->onThreadAttach();
}


void GCPrepareMultiThreaded()
{
   #ifdef HXCPP_SINGLE_THREADED_APP
   CriticalGCError("GCPrepareMultiThreaded called with HXCPP_SINGLE_THREADED_APP");
   #endif
}



void SetTopOfStack(int *inTop,bool inForce)
{
   bool threadAttached = false;
   if (inTop)
   {
      if (!sgAllocInit)
         InitAlloc();
      else
      {
         if (tlsStackContext==0)
         {
            GCPrepareMultiThreaded();
            RegisterCurrentThread(inTop);
            threadAttached = true;
         }
      }
   }

   LocalAllocator *tla = (LocalAllocator *)(hx::ImmixAllocator *)tlsStackContext;

   if (tla)
   {
      tla->SetTopOfStack(inTop,inForce);
      if (threadAttached)
         tla->onThreadAttach();
   }
}


void *InternalNew(size_t inSize,bool inIsObject)
{
   // HX_STACK_FRAME("GC", "new", 0, "GC::new", __FILE__, __LINE__, 0)

   #ifdef HXCPP_DEBUG
   if (sgSpamCollects && sgAllocsSinceLastSpam>=sgSpamCollects)
   {
      //GCLOG("InternalNew spam\n");
      CollectFromThisThread(false,false);
   }
   sgAllocsSinceLastSpam++;
   #endif

   if (inSize>=IMMIX_LARGE_OBJ_SIZE)
   {
      void* result{ sGlobalAlloc->AllocLarge(inSize, true, inIsObject) };
      return result;
   }
   else
   {
      LocalAllocator* tla{ GetLocalAlloc() };

      if (inIsObject)
      {
         void* result{ tla->CallAlloc(inSize,IMMIX_ALLOC_IS_CONTAINER) };
         return result;
      }
      else
      {
         #if defined(HXCPP_GC_MOVING) && defined(HXCPP_M64)
         if (inSize<8)
            return tla->CallAlloc(8,0);
         #endif

         void* result{ tla->CallAlloc((inSize + 3) & ~size_t{3},0) };
         return result;
      }
   }
}


// Small allocations come from blocks cleared ahead of use, so only a large
//  one skips a clear that the copy into it would overwrite (libhxcpp #315)
void *InternalNewUncleared(size_t inSize)
{
   if (inSize>=IMMIX_LARGE_OBJ_SIZE)
   {
      #ifdef HXCPP_DEBUG
      if (sgSpamCollects && sgAllocsSinceLastSpam>=sgSpamCollects)
         CollectFromThisThread(false,false);
      sgAllocsSinceLastSpam++;
      #endif
      return sGlobalAlloc->AllocLarge(inSize, false);
   }
   return InternalNew(inSize, false);
}


void *InternalNewString(size_t inSize, bool inClear)
{
   if (inSize>=IMMIX_LARGE_OBJ_SIZE)
      return inClear ? InternalNew(inSize, false) : InternalNewUncleared(inSize);

   #ifdef HXCPP_DEBUG
   if (sgSpamCollects && sgAllocsSinceLastSpam>=sgSpamCollects)
      CollectFromThisThread(false,false);
   sgAllocsSinceLastSpam++;
   #endif

   #if defined(HXCPP_GC_MOVING) && defined(HXCPP_M64)
   if (inSize<8)
      return GetLocalAlloc()->CallAllocUnaligned(8);
   #endif
   return GetLocalAlloc()->CallAllocUnaligned((inSize + 3) & ~size_t{3});
}


// Force global collection - should only be called from 1 thread.
size_t InternalCollectBytes(bool inMajor,bool inCompact)
{
   if (!sgAllocInit)
       return 0;

   do {
      GetLocalAlloc()->SetupStackAndCollectFlags( (inMajor ? collectMajor : 0) | (inCompact ? collectCompact : 0) );
      #ifdef HXCPP_GC_INCREMENTAL
      if (gIncrementalActive.load()) continue;
      #endif
      break;
   } while (true);

   return sGlobalAlloc->MemUsage();
}

int InternalCollect(bool inMajor,bool inCompact)
{
   return (int)std::min(InternalCollectBytes(inMajor, inCompact), (size_t)0x7fffffff);
}

// Gc.compact(): a major compacting collection that also returns every empty group.
size_t InternalCompactBytes()
{
   sReleaseAllRequests++;
   size_t result = InternalCollectBytes(true, true);
   sReleaseAllRequests--;
   return result;
}

inline unsigned int ObjectSize(void *inData)
{
   unsigned int header = ((unsigned int *)(inData))[-1];

   return (header & IMMIX_ALLOC_ROW_COUNT) ?
            ( (header & IMMIX_ALLOC_SIZE_MASK) >> IMMIX_ALLOC_SIZE_SHIFT) :
             ((unsigned int *)(inData))[-2];
}


unsigned int ObjectSizeSafe(void *inData)
{
   unsigned int header = ((unsigned int *)(inData))[-1];
   if (header & HX_GC_CONST_ALLOC_BIT)
      return 0;

   #ifdef HXCPP_GC_NURSERY
   if (!(header & 0xff000000))
   {
      // Small object
      if (header & 0x00ffffff)
         return header & 0x0000ffff;
      // Large object
   }
   #endif


   return (header & IMMIX_ALLOC_ROW_COUNT) ?
            ( (header & IMMIX_ALLOC_SIZE_MASK) >> IMMIX_ALLOC_SIZE_SHIFT) :
             ((unsigned int *)(inData))[-2];
}

void GCReleaseManagedMemory(size_t inBytes)
{
   sGlobalAlloc->onMemoryRelease(inBytes);
}

void GCChangeManagedMemory(int inDelta, const char *inWhy)
{
   sGlobalAlloc->onMemoryChange(inDelta, inWhy);
}

void InternalReleaseMem(void *inMem)
{
   if (inMem)
   {
      unsigned int s = ObjectSizeSafe(inMem);
      if (s>=IMMIX_LARGE_OBJ_SIZE)
      {
         //Can release asap
         sGlobalAlloc->FreeLarge(inMem);
      }
   }
}



void *InternalRealloc(size_t inFromSize, void *inData, size_t inSize, bool inExpand)
{
   if (inData==0 || inFromSize==0)
   {
      if (inData)
         InternalReleaseMem(inData);
      return hx::InternalNew(inSize,false);
   }

   // HX_STACK_FRAME("GC", "realloc", 0, "GC::relloc", __FILE__ , __LINE__, 0)

   #ifdef HXCPP_DEBUG
   if (sgSpamCollects && sgAllocsSinceLastSpam>=sgSpamCollects)
   {
      //GCLOG("InternalNew spam\n");
      CollectFromThisThread(false,false);
   }
   sgAllocsSinceLastSpam++;
   #endif

   void* new_data{};
   if (inSize==0)
   {
      new_data = hx::emptyAlloc;
   }
   else if (inSize>=IMMIX_LARGE_OBJ_SIZE)
   {
      new_data = sGlobalAlloc->AllocLarge(inSize, false);
      if (inSize > inFromSize)
         ZERO_MEM(static_cast<char*>(new_data) + inFromSize, inSize - inFromSize);
   }
   else
   {
      LocalAllocator *tla = GetLocalAlloc();

      #if defined(HXCPP_GC_MOVING) && defined(HXCPP_M64)
      if (inSize<8)
          new_data =  tla->CallAlloc(8,0);
      else
      #endif
      {
         inSize = (inSize + 3) & ~size_t{ 3 };
         if (inExpand)
            tla->ExpandAlloc(inSize);

         new_data = tla->CallAlloc(inSize,0);
      }
   }


#ifdef HXCPP_TELEMETRY
   //printf(" -- reallocating %018x to %018x, size from %d to %d\n", inData, new_data, s, inSize);
   __hxt_gc_realloc(inData, new_data, inSize);
#endif

   size_t min_size{ inFromSize < inSize ? inFromSize : inSize };

   if (min_size)
      memcpy(new_data, inData, min_size );

   InternalReleaseMem(inData);

   return new_data;
}

#ifdef HXCPP_GC_GENERATIONAL_RUNTIME
void NewMarkedObject(hx::Object *inPtr)
{
   HX_OBJ_WB_PESSIMISTIC_GET(inPtr);
}
#endif

void RegisterCurrentThread(void *inTopOfStack)
{
   // Create a local-alloc
   LocalAllocator *local = sGlobalAlloc->GetPooledAllocator();
   if (!local)
   {
      local = new LocalAllocator((int *)inTopOfStack);
   }
   else
   {
      local->AttachThread((int *)inTopOfStack);
   }

   tlsStackContext = local;
   #ifdef HXCPP_SCRIPTABLE
   local->byteMarkId = hx::gByteMarkID;
   #endif
}

void UnregisterCurrentThread()
{
   LocalAllocator *local = (LocalAllocator *)(hx::ImmixAllocator *)tlsStackContext;
   local->Release();
}

void RegisterVTableOffset(uintptr_t inOffset)
{
   if (inOffset>sgCheckInternalOffset)
   {
      sgCheckInternalOffset = inOffset;
   }
}

void PushTopOfStack(void *inTop)
{
   bool threadAttached = false;
   if (!sgAllocInit)
      InitAlloc();
   else
   {
      if (tlsStackContext==0)
      {
         GCPrepareMultiThreaded();
         RegisterCurrentThread(inTop);
         threadAttached = true;
      }
   }
 
   LocalAllocator *tla = GetLocalAlloc();
   tla->PushTopOfStack(inTop);
   if (threadAttached)
      tla->onThreadAttach();
}

void PopTopOfStack()
{
   LocalAllocator *tla = GetLocalAlloc();
   tla->PopTopOfStack();
}

int GcGetThreadAttachedCount()
{
   LocalAllocator *tla = GetLocalAlloc(true);
   if (!tla)
      return 0;
   return tla->mStackLocks + (tla->mGlobalStackLock ? 1 : 0);
}


#ifdef HXCPP_VISIT_ALLOCS
class GcFreezer : public hx::VisitContext
{
   std::unordered_map<void *, void *> copies;

public:
   void visitObject(hx::Object **ioPtr) HXCPP_OVERRIDE
   {
      hx::Object *obj = *ioPtr;
      if (!obj || IsConstAlloc(obj))
         return;

      auto found = copies.find(obj);
      if (found!=copies.end())
      {
         *ioPtr = (hx::Object *)found->second;
         return;
      }

      unsigned int s = ObjectSizeSafe(obj);
      void *result = InternalCreateConstBuffer(obj,s,false);
      copies[obj] = result;
      *ioPtr = (hx::Object *)result;
      (*ioPtr)->__Visit(this);
      // Its keys are all copies now, and an object key's hash is its identity
      if ((*ioPtr)->_hx_isInstanceOf(hx::clsIdHash))
         static_cast<HashRoot *>(*ioPtr)->rehashKeys();
   }

   void visitAlloc(void **ioPtr) HXCPP_OVERRIDE
   {
      void *data = *ioPtr;
      if (!data || IsConstAlloc(data))
         return;

      auto found = copies.find(data);
      if (found!=copies.end())
      {
         *ioPtr = found->second;
         return;
      }

      unsigned int s = ObjectSizeSafe(data);
      void *result = InternalCreateConstBuffer(data,s,false);
      // A UTF-16 string's encoding is in its header, not its data
      ((unsigned int *)result)[-1] |= ((unsigned int *)data)[-1] & HX_GC_STRING_CHAR16_T;
      copies[data] = result;
      *ioPtr = result;
   }
};
#endif


} // end namespace hx


Dynamic _hx_gc_freeze(Dynamic inObject)
{
#ifdef HXCPP_VISIT_ALLOCS
   hx::GcFreezer freezer;
   hx::Object *base = inObject.mPtr;
   freezer.visitObject(&base);
   return base;
#else
   return inObject;
#endif
}



void __hxcpp_spam_collects(int inEveryNCalls)
{
   #ifdef HXCPP_DEBUG
   sgSpamCollects = inEveryNCalls;
   #else
   GCLOG("Spam collects only available on debug versions\n");
   #endif
}

int __hxcpp_gc_trace(hx::Class inClass,bool inPrint)
{
    #if  !defined(HXCPP_DEBUG)
       #ifdef ANDROID
       __android_log_print(ANDROID_LOG_ERROR, "hxcpp", "GC trace not enabled in release build.");
       #elif defined(HX_WINRT)
       WINRT_LOG("GC trace not enabled in release build.");
       #else
       printf("WARNING : GC trace not enabled in release build.\n");
       #endif
       return 0;
    #else
       gCollectTrace = inClass.GetPtr();
       gCollectTraceCount = 0;
       gCollectTraceDoPrint = inPrint;
       // Major, so that every live instance is marked and counted: a minor
       //  collection skips the old ones, which are marked already
       hx::InternalCollect(true,false);
       gCollectTrace = 0;
       return gCollectTraceCount;
    #endif
}

Dynamic __hxcpp_gc_census()
{
   // Simultaneous calls replaced each other's counts, and one that found another
   //  collection running returned with none (libhxcpp #363)
   CensusRequest request;
   {
      {
         hx::AutoGCFreeZone wait;
         sgCensusLock->lock();
      }
      struct Turn
      {
         ~Turn() { sgCensusRequest = nullptr; sgCensusLock->unlock(); }
      } turn;
      sgCensusRequest = &request;
      while(!request.done)
         hx::InternalCollect(true,false);
   }
   std::unordered_map<hx::Class_obj *,CensusEntry> &counts = request.counts;

   std::vector< std::pair<hx::Class_obj *,CensusEntry> > sorted(counts.begin(), counts.end());
   std::sort(sorted.begin(), sorted.end(), [](const std::pair<hx::Class_obj *,CensusEntry> &a, const std::pair<hx::Class_obj *,CensusEntry> &b)
      { return a.second.bytes > b.second.bytes; });

   Array<Dynamic> result = Array_obj<Dynamic>::__new(0, (int)sorted.size());
   for(size_t i=0;i<sorted.size();i++)
   {
      hx::Anon row = hx::Anon_obj::Create();
      row->__SetField(HX_CSTRING("name"), sorted[i].first ? sorted[i].first->mName : HX_CSTRING("(no class)"), hx::paccDynamic);
      row->__SetField(HX_CSTRING("count"), (int)std::min(sorted[i].second.count, (size_t)std::numeric_limits<int>::max()), hx::paccDynamic);
      row->__SetField(HX_CSTRING("bytes"), (Float)sorted[i].second.bytes, hx::paccDynamic);
      result->push(row);
   }
   return result;
}

static int hxSaturateInt(size_t inBytes)
{
   return (int)std::min(inBytes, (size_t)std::numeric_limits<int>::max());
}

int   __hxcpp_gc_large_bytes()
{
   return hxSaturateInt(sGlobalAlloc->MemLarge());
}

int   __hxcpp_gc_reserved_bytes()
{
   return hxSaturateInt(sGlobalAlloc->MemReserved());
}

double __hxcpp_gc_mem_info(int inWhich)
{
   switch(inWhich)
   {
      case MEM_INFO_USAGE:
         return (double)sGlobalAlloc->MemUsage();
      case MEM_INFO_RESERVED:
         return (double)sGlobalAlloc->MemReserved();
      case MEM_INFO_CURRENT:
         return (double)sGlobalAlloc->MemCurrent();
      case MEM_INFO_LARGE:
         return (double)sGlobalAlloc->MemLarge();
      case MEM_INFO_LIVE:
         return (double)sGlobalAlloc->MemLive();
   }
   return 0;
}

int   __hxcpp_gc_used_bytes()
{
   return hxSaturateInt(sGlobalAlloc->MemUsage());
}

void  __hxcpp_gc_do_not_kill(Dynamic inObj)
{
   hx::GCDoNotKill(inObj.GetPtr());
}

hx::Object *__hxcpp_get_next_zombie()
{
   return hx::GCGetNextZombie();
}


void _hx_set_finalizer(Dynamic inObj, void (*inFunc)(Dynamic) )
{
   GCSetHaxeFinalizer( inObj.mPtr, inFunc );
}

void __hxcpp_set_finalizer(Dynamic inObj, void *inFunc)
{
   GCSetHaxeFinalizer( inObj.mPtr, (hx::HaxeFinalizer) inFunc );
}

void __hxcpp_add_member_finalizer(hx::Object *inObject, _hx_member_finalizer f, bool inPin)
{
   std::lock_guard<hx::FastMutex> lock(*gSpecialObjectLock);
   hx::sFinalizableList.push( hx::Finalizable(inObject, f, inPin) );
}

void __hxcpp_add_alloc_finalizer(void *inAlloc, _hx_alloc_finalizer f, bool inPin)
{
   std::lock_guard<hx::FastMutex> lock(*gSpecialObjectLock);
   hx::sFinalizableList.push( hx::Finalizable(inAlloc, f, inPin) );
}



extern "C"
{
void hxcpp_set_top_of_stack()
{
   int i = 0;
   hx::SetTopOfStack(&i,false);
}
}

void __hxcpp_enter_gc_free_zone()
{
   hx::EnterGCFreeZone();
}


bool __hxcpp_try_gc_free_zone()
{
   return hx::TryGCFreeZone();
}



void __hxcpp_exit_gc_free_zone()
{
   hx::ExitGCFreeZone();
}


void __hxcpp_gc_safe_point()
{
    if (hx::gPauseForCollect)
      hx::PauseForCollect();
}

//#define HXCPP_FORCE_OBJ_MAP

#if defined(HXCPP_M64) || defined(HXCPP_GC_MOVING) || defined(HXCPP_FORCE_OBJ_MAP)
#define HXCPP_USE_OBJECT_MAP
#endif

int __hxcpp_obj_id(Dynamic inObj)
{
   hx::Object *obj = inObj.mPtr;
   if (!obj) return -1;
   #ifdef HXCPP_USE_OBJECT_MAP
   return sGlobalAlloc->GetObjectID(obj);
   #else
   return (int)(obj);
   #endif
}

hx::Object *__hxcpp_id_obj(int inId)
{
   #ifdef HXCPP_USE_OBJECT_MAP
   return (hx::Object *)sGlobalAlloc->GetIDObject(inId);
   #else
   return (hx::Object *)(inId);
   #endif
}

#if defined(HXCPP_GC_MOVING) || defined(HXCPP_FORCE_OBJ_MAP)
unsigned int __hxcpp_obj_hash(Dynamic inObj)
{
   return __hxcpp_obj_id(inObj);
}
#else
// Objects stay where they are, so the address is the hash. Allocations are
// 8-byte aligned, so the low 3 bits carry nothing
unsigned int __hxcpp_obj_hash(Dynamic inObj)
{
   if (!inObj.mPtr) return 0;
   size_t h = (size_t)inObj.mPtr >> 3;
   #if defined(HXCPP_M64)
   return (unsigned int)h ^ (unsigned int)(h>>32);
   #else
   return (unsigned int)h;
   #endif
}
#endif




void DummyFunction(void *inPtr) { }

