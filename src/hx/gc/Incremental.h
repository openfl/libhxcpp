#ifndef HX_INCREMENTAL_IMPLEMENTATION_H
#define HX_INCREMENTAL_IMPLEMENTATION_H

// Included after MarkContext and allocation marking, with all mutators stopped
// whenever queues, ranges or cycle state are inspected.
namespace hx
{

std::atomic<bool> gIncrementalActive{false};
bool gIncrementalAllocationsWhite = false;
static thread_local double sIncrementalRequestMs = 0;

// A cycle's place in the reserve, the bytes the app can allocate before the
// collector would have to stop it. The later a cycle starts the more garbage
// each mark pays for, so it starts as late as it can still finish by.
// The app gets at least this long between slices, however fast it allocates
static const double sPaceMinGapMs = 0.25;
// Slack on the time a cycle is expected to take
static const double sPaceMargin = 1.3;
// The earliest and latest start, as shares of the reserve
static const double sPaceEarliest = 0.25;
static const double sPaceLatest = 0.85;
// The expected slices spend 1/sPaceSpend of the reserve left after the start
static const double sPaceSpend = 1.5;
// Past this share of what is left after the start, slices stop waiting
static const double sPacePressure = 0.75;

struct IncrementalRangeJob
{
   Object *owner;
   IncrementalRangeScan scan;
   IncrementalRangeLength length;
   size_t cursor = 0;
   size_t item = 0;
   bool queued = true;
   IncrementalRangeJob(Object *inOwner, IncrementalRangeScan inScan, IncrementalRangeLength inLength) : owner(inOwner), scan(inScan), length(inLength) {}
};

struct IncrementalState
{
   std::atomic<bool> failed{false};
   std::atomic<double> budgetMs{0.5};
   std::atomic<double> intervalMs{1.0};
   std::atomic<double> nextAutomatic{0};
   std::atomic<bool> automaticRunning{false};
   // Learned from finished cycles: their slices, and the bytes allocated per ms
   double paceSlices = 0;
   double paceRate = 0;
   double cycleStart = 0;
   size_t cycleAllocated = 0;
   // Bytes the app may allocate between slices, 0 until a cycle has been learned
   std::atomic<double> sliceCredit{0};
   std::atomic<size_t> lastSliceAllocated{0};
   std::atomic<double> lastSliceEnd{0};
   std::vector<IncrementalEntry> detached;
   size_t detachedRead = 0;
   bool logsPending = false;
   bool finishPending = false;
   std::vector<IncrementalRangeJob> ranges;
   std::unordered_map<Object *, size_t> rangeOwners;
   std::vector<size_t> rangeQueue;
   size_t rangeHead = 0;
   std::unordered_set<Object *> dirty;
   bool tracing = false;
   double deadline = 0;
   std::atomic<size_t> work{0};
   size_t slices = 0;
   std::mutex statsLock;
   uint64_t cycles = 0;
   uint64_t pauses = 0;
   uint64_t fallbacks = 0;
   uint64_t forcedFinishes = 0;
   uint64_t requestedSlices = 0;
   double maxSlicePauseMs = 0;
   double maxSynchronousPauseMs = 0;
   double totalPauseMs = 0;
   double maxPauseMs = 0;
   double lastPauseMs = 0;
   size_t lastSlices = 0;
   Object *watch = nullptr;
   Object *omit = nullptr;
   std::atomic<bool> watched{false};
   std::mutex rangeLock;
   size_t testWorkLimit = 0;

   void clearWork()
   {
      ranges.clear();
      rangeOwners.clear();
      rangeQueue.clear();
      rangeHead = 0;
      detached.clear();
      detachedRead = 0;
      logsPending = false;
      finishPending = false;
      dirty.clear();
      tracing = false;
      deadline = 0;
      work = 0;
   }

   void cycleStarted(size_t inAllocated)
   {
      cycleStart = IncrementalNow();
      cycleAllocated = inAllocated;
      lastSliceAllocated.store(inAllocated, std::memory_order_relaxed);
      lastSliceEnd.store(0, std::memory_order_relaxed);
   }

   void cycleFinished(size_t inAllocated, double inSlices)
   {
      double elapsed = (IncrementalNow() - cycleStart) * 1000.0;
      if (elapsed > 0 && inAllocated > cycleAllocated)
      {
         double rate = (inAllocated - cycleAllocated) / elapsed;
         paceSlices = paceSlices > 0 ? (paceSlices + inSlices) / 2 : inSlices;
         paceRate = paceRate > 0 ? (paceRate + rate) / 2 : rate;
      }
   }

   void plan(double inReserve, size_t &outStart, size_t &outPressure)
   {
      double start = inReserve / 2;
      double pressure = inReserve * 3 / 4;
      double credit = 0;
      if (paceSlices > 0 && paceRate > 0)
      {
         // What the app allocates while the expected slices run at the fastest pace allowed
         double runway = paceSlices * paceRate * (budgetMs.load() + sPaceMinGapMs) * sPaceMargin;
         start = std::min(inReserve * sPaceLatest, std::max(inReserve * sPaceEarliest, inReserve - runway));
         credit = (inReserve - start) / (sPaceSpend * paceSlices);
         pressure = start + (inReserve - start) * sPacePressure;
      }
      sliceCredit.store(credit, std::memory_order_relaxed);
      outStart = std::max(size_t{256} * 1024, static_cast<size_t>(start));
      outPressure = std::max(size_t{384} * 1024, static_cast<size_t>(pressure));
   }

   // The app has allocated its share of the reserve since the last slice, and
   // has had its minimum time to run
   bool sliceDue(size_t inAllocated, double inNow)
   {
      double credit = sliceCredit.load(std::memory_order_relaxed);
      return credit > 0 && gIncrementalActive.load(std::memory_order_relaxed) &&
             inAllocated >= lastSliceAllocated.load(std::memory_order_relaxed) + static_cast<size_t>(credit) &&
             inNow >= lastSliceEnd.load(std::memory_order_relaxed) + sPaceMinGapMs / 1000.0;
   }

   void pause(double inStart, bool inBudgeted)
   {
      double elapsed = (IncrementalNow() - inStart) * 1000.0;
      std::lock_guard<std::mutex> lock(statsLock);
      pauses++;
      totalPauseMs += elapsed;
      lastPauseMs = elapsed;
      maxPauseMs = std::max(maxPauseMs, elapsed);
      if (inBudgeted)
      {
         requestedSlices++;
         maxSlicePauseMs = std::max(maxSlicePauseMs, elapsed);
      }
      else
         maxSynchronousPauseMs = std::max(maxSynchronousPauseMs, elapsed);
   }
};

static IncrementalState &IncrementalData()
{
   // Like the allocator, this survives static destruction and late threads.
   static IncrementalState *state = new IncrementalState();
   return *state;
}

static bool IncrementalExpired()
{
   IncrementalState &state = IncrementalData();
   if (!state.tracing || state.deadline <= 0)
      return false;
#ifdef HXCPP_GC_INCREMENTAL_TEST
   if (state.work > 0 && ((state.testWorkLimit && state.work >= state.testWorkLimit) || state.watched))
      return true;
#endif
   // Reading the clock for every pointer costs more than marking small
   // objects. Check in bounded batches without a shared counter cache line.
   static thread_local unsigned int checks = 0;
   return (++checks & 63) == 0 && IncrementalNow() >= state.deadline;
}

static void IncrementalScanned(Object *inObject)
{
#ifdef HXCPP_GC_INCREMENTAL_TEST
   IncrementalState &state = IncrementalData();
   if (!state.tracing)
      return;
   state.work++;
   if (inObject == state.watch)
      state.watched = true;
#endif
}

void IncrementalRemember(Object *inOwner, const void *inValue, StackContext *inCtx)
{
   if (!gIncrementalActive.load(std::memory_order_relaxed) || !inCtx)
      return;
   // A white holder will be scanned if it becomes reachable. Only a holder
   // already discovered by this cycle can hide an inserted edge from it.
   if (inOwner && reinterpret_cast<unsigned char *>(inOwner)[HX_ENDIAN_MARK_ID_BYTE] != gByteMarkID)
      return;
   if (inCtx->mIncrementalEntries.size() >= (1u << 20))
   {
      IncrementalData().failed.store(true, std::memory_order_relaxed);
      return;
   }
   try
   {
      inCtx->mIncrementalEntries.push_back({inOwner, inValue});
   }
   catch (const std::bad_alloc &)
   {
      IncrementalData().failed.store(true, std::memory_order_relaxed);
   }
}

void IncrementalAllocated(void *inValue, bool inObject, ImmixAllocator *inAlloc)
{
   if (!gIncrementalActive.load(std::memory_order_relaxed))
      return;
   if (IncrementalData().tracing)
   {
      // New objects are white too. A root or a store into a discovered
      // holder shades them, so transient objects do not flood the mark log.
      // Stores after a partially constructed object was discovered use the
      // same barrier as stores into any other discovered holder.
      static_cast<unsigned char *>(inValue)[HX_ENDIAN_MARK_ID_BYTE] = static_cast<unsigned char>(gPrevByteMarkID);
      return;
   }
   // Fresh headers already have the current epoch. Their rows must also be
   // live even if no subsequent pointer scan visits a primitive allocation.
   uintptr_t header = reinterpret_cast<uintptr_t>(inValue) - sizeof(unsigned int);
   unsigned int flags = *reinterpret_cast<unsigned int *>(header);
   unsigned int rows = flags & IMMIX_ALLOC_ROW_COUNT;
   unsigned char *row = reinterpret_cast<unsigned char *>(header & IMMIX_BLOCK_BASE_MASK) +
                        ((header & IMMIX_BLOCK_OFFSET_MASK) >> IMMIX_LINE_BITS);
   for (unsigned int i = 0; i < rows; i++)
      row[i] = 1;
}

void IncrementalMutationEnter(Object *inOwner, StackContext *inCtx)
{
   if (inCtx)
      inCtx->mIncrementalMutations++;
   IncrementalRemember(inOwner, nullptr, inCtx);
}

void IncrementalMutationLeave(Object *inOwner, StackContext *inCtx)
{
   IncrementalRemember(inOwner, nullptr, inCtx);
   if (inCtx)
      inCtx->mIncrementalMutations--;
}

static void IncrementalTransfer(StackContext *inCtx)
{
   IncrementalState &state = IncrementalData();
   try
   {
      state.detached.insert(state.detached.end(), inCtx->mIncrementalEntries.begin() + inCtx->mIncrementalRead, inCtx->mIncrementalEntries.end());
   }
   catch (const std::bad_alloc &)
   {
      state.failed.store(true, std::memory_order_relaxed);
   }
   inCtx->mIncrementalEntries.clear();
   inCtx->mIncrementalRead = 0;
}

bool IncrementalRange(Object *inOwner, IncrementalRangeScan inScan, MarkContext *inCtx, IncrementalRangeLength inLength)
{
   IncrementalState &state = IncrementalData();
   if (!state.tracing)
      return false;
   std::lock_guard<std::mutex> lock(state.rangeLock);
   if (state.failed.load(std::memory_order_relaxed))
      return true;
   try
   {
      if (state.ranges.size() >= (1u << 20))
      {
         state.failed.store(true, std::memory_order_relaxed);
         return true;
      }
      auto found = state.rangeOwners.find(inOwner);
      if (found == state.rangeOwners.end())
      {
         size_t index = state.ranges.size();
         state.ranges.emplace_back(inOwner, inScan, inLength);
         state.rangeOwners.emplace(inOwner, index);
         state.rangeQueue.push_back(index);
      }
      else
      {
         IncrementalRangeJob &job = state.ranges[found->second];
         if (!job.queued)
         {
            job.cursor = job.item = 0;
            job.queued = true;
            state.rangeQueue.push_back(found->second);
         }
      }
   }
   catch (const std::bad_alloc &)
   {
      // Keep the heap intact and restart synchronously at the next stop.
      // An exception must not escape a marker worker thread.
      state.failed.store(true, std::memory_order_relaxed);
   }
   return true;
}

static bool IncrementalEntries(std::vector<IncrementalEntry> &inEntries, size_t &ioRead, MarkContext *inCtx)
{
   IncrementalState &state = IncrementalData();
   while (ioRead < inEntries.size())
   {
      if (IncrementalExpired())
         return false;
      const IncrementalEntry &entry = inEntries[ioRead++];
      state.work++;
      if (entry.value)
      {
         unsigned int flags;
         memcpy(&flags, static_cast<const char *>(entry.value) - sizeof(flags), sizeof(flags));
         if (flags & IMMIX_ALLOC_IS_CONTAINER)
            MarkObjectAlloc(static_cast<Object *>(const_cast<void *>(entry.value)), inCtx);
         else
            MarkAlloc(const_cast<void *>(entry.value), inCtx);
         // Scalar insertion barriers shade the new target. They do not
         // invalidate an entire large container on every element write.
         continue;
      }
      if (!entry.owner)
         continue;
      try
      {
         if (!state.dirty.insert(entry.owner).second)
            continue;
      }
      catch (const std::bad_alloc &)
      {
         state.failed.store(true, std::memory_order_relaxed);
         return false;
      }
      MarkObjectAlloc(entry.owner, inCtx);
      // A black holder can acquire an edge after it was scanned. Revisit it
      // even if its ordinary header already belongs to this cycle.
      inCtx->pushObj(entry.owner);
      auto range = state.rangeOwners.find(entry.owner);
      if (range != state.rangeOwners.end())
      {
         IncrementalRangeJob &job = state.ranges[range->second];
         job.cursor = job.item = 0;
      }
   }
   inEntries.clear();
   ioRead = 0;
   return true;
}

// Queues a batch of a range's chunks for the markers, and returns whether that
// was the last of them. A chunk names its owner and an index, so it finds the
// current storage whichever slice it runs in.
static bool IncrementalQueueChunks(IncrementalRangeJob &ioJob, int inBatch)
{
   IncrementalState &state = IncrementalData();
   size_t length = ioJob.length(ioJob.owner);
   for (int i = 0; i < inBatch && ioJob.cursor < length; i++)
   {
      MarkChunk *chunk = sGlobalChunks.alloc();
      chunk->count = MarkChunk::RANGE_CHUNK_JOB;
      chunk->rangeOwner = ioJob.owner;
      chunk->rangeScan = ioJob.scan;
      chunk->rangeStart = ioJob.cursor;
      sGlobalChunks.pushJob(chunk, false);
      ioJob.cursor += IncrementalRangeChunk;
      state.work++;
   }
   return ioJob.cursor >= length;
}

static bool IncrementalDrain(MarkContext *inCtx, void (*inProcess)(MarkContext *, void *), void *inData)
{
   IncrementalState &state = IncrementalData();
   try
   {
      while (true)
      {
         if (state.failed.load(std::memory_order_relaxed))
            return false;
         inProcess(inCtx, inData);
         if (state.failed.load(std::memory_order_relaxed))
            return false;
         if (inCtx->hasWork())
            return false;
         if (state.rangeHead >= state.rangeQueue.size())
            return !state.logsPending;
         if (IncrementalExpired())
            return false;
         // Feed several chunks to one worker round. Waking the marker pool for
         // each 256-element chunk costs more than scanning those elements.
         for (int batch = 0; batch < 64 && state.rangeHead < state.rangeQueue.size(); batch++)
         {
            if (IncrementalExpired())
               break;
            size_t index = state.rangeQueue[state.rangeHead++];
            // Workers are stopped here. Scans enqueue objects; only their
            // later __Mark calls can append ranges and move this descriptor.
            IncrementalRangeJob &job = state.ranges[index];
            bool complete;
            if (job.length)
            {
               complete = IncrementalQueueChunks(job, 64 - batch);
               batch = 64;
            }
            else
            {
               inCtx->init();
               complete = job.scan(job.owner, job.cursor, job.item, inCtx);
               state.work++;
            }
            if (complete)
               job.queued = false;
            else
               state.rangeQueue.push_back(index);
         }
      }
   }
   catch (const std::bad_alloc &)
   {
      state.failed.store(true, std::memory_order_relaxed);
      return false;
   }
}

static void IncrementalDiscard(MarkContext *inCtx)
{
   inCtx->discardJobs();
   IncrementalData().clearWork();
   gIncrementalAllocationsWhite = false;
   gIncrementalActive.store(false, std::memory_order_relaxed);
}

} // namespace hx
#endif
