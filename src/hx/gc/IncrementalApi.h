#ifndef HX_INCREMENTAL_API_H
#define HX_INCREMENTAL_API_H

#ifdef HXCPP_GC_INCREMENTAL
static void IncrementalAutomatic(hx::ImmixAllocator *inAlloc, bool inPressure, size_t inAllocated)
{
   if (!sgInternalEnable)
      return;
   // A slice can run inside a mutation scope, as one on another thread can: the
   // allocation that brought it here is a safe point, and only finishing waits
   // for the scope. An app whose allocation is mostly array storage would
   // otherwise get no slices, and every collection would be a full stop.
   hx::IncrementalState &state = hx::IncrementalData();
   bool expected = false;
   if (!state.automaticRunning.compare_exchange_strong(expected, true, std::memory_order_relaxed))
      return;
   struct Release
   {
      hx::IncrementalState &state;
      ~Release() { state.automaticRunning.store(false, std::memory_order_relaxed); }
   } release{state};
   double now = hx::IncrementalNow();
   double due = state.nextAutomatic.load(std::memory_order_relaxed);
   if (!inPressure && now < due && !state.sliceDue(inAllocated, now))
      return;
   state.nextAutomatic.store(now + state.intervalMs.load() / 1000.0, std::memory_order_relaxed);
   double previous = hx::sIncrementalRequestMs;
   hx::sIncrementalRequestMs = state.budgetMs.load();
   try
   {
      inAlloc->SetupStackAndCollect(false, false);
   }
   catch (...)
   {
      hx::sIncrementalRequestMs = previous;
      throw;
   }
   hx::sIncrementalRequestMs = previous;
   state.lastSliceAllocated.store(inAllocated, std::memory_order_relaxed);
   state.lastSliceEnd.store(hx::IncrementalNow(), std::memory_order_relaxed);
}
#endif

bool __hxcpp_gc_incremental_step(double inBudgetMs)
{
#ifdef HXCPP_GC_INCREMENTAL
   if (!(inBudgetMs >= 0 && inBudgetMs <= 1000))
      hx::Throw(HX_CSTRING("Invalid incremental GC budget"));
   if (inBudgetMs == 0)
      return hx::gIncrementalActive.load();
   double previous = hx::sIncrementalRequestMs;
   hx::sIncrementalRequestMs = inBudgetMs;
   try
   {
      GetLocalAlloc()->SetupStackAndCollect(false, false);
   }
   catch (...)
   {
      hx::sIncrementalRequestMs = previous;
      throw;
   }
   hx::sIncrementalRequestMs = previous;
   return hx::gIncrementalActive.load();
#else
   hx::Throw(HX_CSTRING("Build with -lib hxcpp -D HXCPP_GC_INCREMENTAL to use incremental GC"));
   return false;
#endif
}

void __hxcpp_gc_incremental_configure(double inBudgetMs, double inIntervalMs)
{
#ifdef HXCPP_GC_INCREMENTAL
   if (!(inBudgetMs > 0 && inBudgetMs <= 1000 && inIntervalMs >= 0 && inIntervalMs <= 1000))
      hx::Throw(HX_CSTRING("Invalid incremental GC pacing"));
   hx::IncrementalData().budgetMs.store(inBudgetMs);
   hx::IncrementalData().intervalMs.store(inIntervalMs);
#else
   hx::Throw(HX_CSTRING("Incremental GC is not enabled"));
#endif
}

bool __hxcpp_gc_incremental_active()
{
#ifdef HXCPP_GC_INCREMENTAL
   return hx::gIncrementalActive.load();
#else
   return false;
#endif
}

Dynamic __hxcpp_gc_incremental_stats()
{
#ifdef HXCPP_GC_INCREMENTAL
   hx::IncrementalState &state = hx::IncrementalData();
   double cycles, pauses, fallbacks, total, longest, last, slices, forced, requested, sliceMax, syncMax;
   {
      std::lock_guard<std::mutex> lock(state.statsLock);
      cycles = static_cast<double>(state.cycles);
      pauses = static_cast<double>(state.pauses);
      fallbacks = static_cast<double>(state.fallbacks);
      total = state.totalPauseMs;
      longest = state.maxPauseMs;
      last = state.lastPauseMs;
      slices = static_cast<double>(state.lastSlices);
      forced = static_cast<double>(state.forcedFinishes);
      requested = static_cast<double>(state.requestedSlices);
      sliceMax = state.maxSlicePauseMs;
      syncMax = state.maxSynchronousPauseMs;
   }
   return hx::Anon_obj::Create(12)
       ->setFixed(0, HX_CSTRING("cycles"), cycles)
       ->setFixed(1, HX_CSTRING("pauses"), pauses)
       ->setFixed(2, HX_CSTRING("fallbacks"), fallbacks)
       ->setFixed(3, HX_CSTRING("totalPauseMs"), total)
       ->setFixed(4, HX_CSTRING("maxPauseMs"), longest)
       ->setFixed(5, HX_CSTRING("lastPauseMs"), last)
       ->setFixed(6, HX_CSTRING("lastCycleSlices"), slices)
       ->setFixed(7, HX_CSTRING("active"), hx::gIncrementalActive.load())
       ->setFixed(8, HX_CSTRING("forcedFinishes"), forced)
       ->setFixed(9, HX_CSTRING("requestedSlices"), requested)
       ->setFixed(10, HX_CSTRING("maxSlicePauseMs"), sliceMax)
       ->setFixed(11, HX_CSTRING("maxSynchronousPauseMs"), syncMax);
#else
   return hx::Anon_obj::Create(1)->setFixed(0, HX_CSTRING("active"), false);
#endif
}

#ifdef HXCPP_GC_INCREMENTAL_TEST
int __hxcpp_gc_incremental_test(int inOperation, hx::Object *inObject)
{
   hx::IncrementalState &state = hx::IncrementalData();
   switch (inOperation)
   {
      case 0:
         state.watch = inObject;
         state.watched = false;
         state.testWorkLimit = 32;
         return 0;
      case 1:
         return state.watched;
      case 2:
         return hx::gIncrementalActive.load();
      case 3:
         state.failed.store(true);
         return 0;
      case 4:
         state.testWorkLimit = 0;
         state.watch = nullptr;
         state.watched = false;
         return 0;
      case 5:
      {
         auto found = state.rangeOwners.find(inObject);
         return found != state.rangeOwners.end() && state.ranges[found->second].queued &&
                (state.ranges[found->second].cursor || state.ranges[found->second].item);
      }
      case 6:
         state.omit = inObject;
         return 0;
      case 7:
         return state.finishPending;
      case 8:
         return GetLocalAlloc()->allocBase && GetLocalAlloc()->spaceEnd > GetLocalAlloc()->spaceStart;
      case 9:
      {
         int chunks = 0;
         for (hx::MarkChunk *chunk = hx::sGlobalChunks.processList; chunk; chunk = chunk->next)
            if (chunk->count == hx::MarkChunk::RANGE_CHUNK_JOB)
               chunks++;
         return chunks;
      }
      default:
         return -1;
   }
}
#endif
#endif
