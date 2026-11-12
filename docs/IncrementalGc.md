# Incremental GC

Tracked in [#372](https://github.com/openfl/libhxcpp/issues/372). This is an experimental, opt-in collector for native C++ programs using Haxe 4.3 or later in the 4.x series. The existing collector stays the default. The concurrent prototype in [#327](https://github.com/openfl/libhxcpp/issues/327) is deferred in the separate `feature/concurrent-gc` worktree.

Incremental marking is implemented, including automatic pacing, scheduling hooks, write barriers, resumable containers, synchronous recovery and verification. It does not yet meet every performance gate in #372. A requested duration is a target, not a deadline: root capture, thread rendezvous, one object's native `__Mark`, weak closure, finalizers, reclamation and forced completion can run beyond it. Application frame timings remain the measure of interruption seen by a game.

## Enable and use

Regenerate and rebuild the program and its native code with:

```sh
haxe build.hxml -lib hxcpp -D HXCPP_GC_INCREMENTAL
```

The library macro requests the existing Haxe generator's setter barriers. It enables `HXCPP_GC_GENERATIONAL` internally for code generation, while the runtime keeps ordinary allocation headers and does not enable the nursery. No compiler or standard library patch is needed. Do not add the generational flag yourself or reuse C++ generated without the incremental flag.

Automatic collection needs no engine calls. It accounts for newly acquired allocation blocks and large buffers, and uses a 0.5 ms slice target and a preferred 1 ms interval. Each finished cycle teaches the pacer how many slices a cycle takes and how fast the app allocates during one. The next cycle then starts as late as it can still finish inside the reserve, the space the existing collector would spend before collecting: the free list, leaving out holes too small to use, and growth up to the memory target. A later start means fewer marks for the same allocation. During a cycle a slice is due at the interval, or sooner once the app has allocated its share of the reserve, with at least 0.25 ms for the app between slices. Past three quarters of what was left at the start, slices stop waiting. A cycle the allocator cuts short counts as a longer one, so the next starts earlier, and the first cycle, with nothing to learn from, starts at half the reserve. Deadlines and pause counters use a monotonic clock. Exhausting available space can still force synchronous completion. `Gc.enable(false)` disables automatic collection as before; explicit collection and scheduling calls still work.

Optional engine hooks are library-owned:

```haxe
import hxcpp.gc.Incremental;

Incremental.configure(0.5, 1.0);

// During spare frame time, advance a cycle that automatic pacing started.
if (Incremental.isActive())
    Incremental.step(0.3);
```

- `step(budgetMs = 0.5)` starts or advances a cycle and returns whether work remains. Zero only queries pending work. Repeated calls can finish a cycle with automatic collection disabled. Calling it unconditionally on an idle heap starts unnecessary major cycles.
- `configure(budgetMs = 0.5, intervalMs = 1.0)` changes pacing. The interval is preferred spacing, which allocation pressure can shorten. Budgets must be finite and positive, at most 1000 ms. Intervals can be zero and must be finite, between zero and 1000 ms.
- `isActive()` reports unfinished work.
- `getStats()` returns cumulative counters described below. Without the build flag it returns `{active: false}`; `isActive()` returns false, and scheduling calls throw an explanatory error.

`Gc.run`, `Gc.compact` and disabling collection finish an active cycle before returning. A census abandons the partial mark and performs an independent synchronous collection.

## Marking and mutation protocol

All application threads stop while their fields are traced. The collecting thread marks beside the parallel marker workers, so a slice does not depend on them starting. A worker the scheduler has not run when a round ends has taken no work, so its wake is withdrawn instead of waited for. A slice does not wait for the background zeroing thread either, which marking does not disturb; the stop that finishes a cycle does. No marker worker reads application fields after application threads resume, and no heap graph is copied into a second snapshot.

The mark epoch and pending object jobs survive between slices. Roots and thread stacks are captured again at each stop. Newly allocated objects and buffers are white during marking. Discovering one through a root or an inserted edge marks it normally. A store into a holder already discovered by the cycle logs its new target; stores into white holders need no log because reaching the holder will scan its fields. A deletion can retain old work until the next cycle. When a budgeted cycle first drains its strong mark work, it yields once before finishing. The next stop rescans roots and new mutation entries before weak processing and reclamation, so finishing does not automatically follow a full marking slice in the same interruption.

Native bulk operations use a mutation scope that records the holder before and after changing its storage or order. Slices run inside scopes as they do anywhere else, because the allocation that reaches them is a safe point. A holder in an open scope changes without barriers, so the stop that finishes a cycle scans those holders again, for up to eight nested scopes on a thread. Past that a budgeted cycle waits for the scopes to close, and a synchronous request restarts a full collection. Native constructors and stores that bypass generated setters must use the appropriate `HX_OBJ_WB_GET` or `HX_OBJ_WB_NEW_MARKED_OBJECT` hooks, as the runtime wrappers do. Arbitrary native field writes without barriers are outside this mode's contract.

Arrays and maps longer than 256 elements register jobs containing a managed owner and a cursor. Each chunk reacquires current storage from that owner. Structural mutation resets the cursor. Scalar insertion shades its target without restarting the whole container. An array's chunks go onto the shared mark queue as jobs, so every marker scans them in parallel, and a job not reached in one slice waits for the next. A map keeps one cursor and is scanned by the collecting thread. Several chunks feed one marker round to avoid waking the pool for every small chunk. Traditional raw array-buffer jobs cannot survive an incremental pause.

No heap storage is reclaimed while marking is unfinished. Completed collection free ranges are materialized before starting a new partial mark, so lazy allocator reclamation cannot interpret partial row marks as a completed collection. The stop that finishes a cycle only counts rows and builds the free list. A block is reclaimed when it is first used, or before that by the zeroing thread, which has time to spare while the allocator catches up, so a cycle's first stop rarely finds any left to materialize. A block outside the free list waits for a later collection.

A dead object keeps its start bit until something clears it, and its mark id comes round again after 16 collections. The existing collector clears them in a full reclaim every 15 collections, a stop that grows with the heap. Slices sweep a twelfth of the blocks per collection instead, as jobs on the mark queue, so steady allocation needs no full reclaim. One still runs if the sweep falls more than a collection behind, for an explicit collection, and to give memory back from a heap well over its target.

Detached threads transfer their unread mutation entries before their contexts are pooled or destroyed.

If mutation metadata cannot be retained, the partial cycle is discarded before any reclamation. Recovery rewinds headers changed by the partial epoch before restarting a full mark. Advancing directly to another epoch would make unvisited objects appear marked to the alternating-bit fast path. Tests cover that failure transition. Log storage is capped at 1,048,576 entries per thread, and range registration at 1,048,576 owners per cycle. Native allocation failure requests recovery; this is not a guarantee that a process with exhausted system memory can continue.

## Verification and tests

`HXCPP_GC_INCREMENTAL_VERIFY` runs a separate root and descendant walk before weak processing and reclamation. It has its own visited set, bypasses incremental header tests and checks reachable allocation headers and occupied rows. The define applies to generated code and runtime code, including inline marking helpers. It adds a substantial pause and memory cost and must be omitted from performance measurements.

`HXCPP_GC_INCREMENTAL_TEST` supplies deterministic phase limits and failure injection. It belongs only in test builds. `IncrementalGcVerifier` deliberately removes a descendant's mark in a subprocess and requires the independent walk to reject it.

Conventional fixtures under `test/regression` cover:

| Fixture | Coverage |
| --- | --- |
| `GcMutationRoots` | Graph rewiring, older descendants through new holders, anonymous fields and mutable closure captures |
| `GcContainerMutation` | Pointer-array growth, splice and overlapping blit, plus integer, string and object map mutation |
| `IncrementalGcSlices` | Mutation after an acknowledged holder scan, detached writers, reflective stores, explicit completion, epoch recovery and invalid scheduling inputs |
| `IncrementalGcContainers` | Resize and reorder after a partially traversed array range, with payload checks after completion |
| `IncrementalGcArrayChunks` | Array chunks still queued at a stop, through shrinking and reallocation, and dropped with a failed cycle |
| `IncrementalGcPacing` | Cycles spending the free space, and finishing when all allocation is in mutation scopes |
| `IncrementalGcStartSweep` | Dead start bits cleared before their mark id returns, and no full reclaim, on a heap fragmented into small holes |
| `IncrementalGcStats` | Every counter from `getStats` read by name after a finished cycle |
| `IncrementalGcFinishBoundary` | New roots, descendants and container stores published between strong marking and finishing |
| `IncrementalGcVerifier` | Independent detection of an omitted descendant |
| `GcWeakMapChain` | Weak-map reachability closure |
| `GenerationalFinalizerLifecycle` | Finalizer lifecycle semantics |
| `GenerationalLargeBufferLifecycle` | Large-buffer lifecycle semantics |

For example, from a fixture directory:

```sh
haxe build.hxml -lib hxcpp -D HXCPP_GC_INCREMENTAL -D HXCPP_GC_INCREMENTAL_VERIFY -D HXCPP_GC_INCREMENTAL_TEST
bin/Main
```

The CI workflow includes a native Haxe 4 incremental job for these fixtures and eight-thread stress, with both normal and big blocks. `test/gc-defines/check.sh` includes incremental, verifier/test, and single-threaded configurations at both block sizes. The ordinary default, generational and moving controls remain separate.

## Telemetry and performance limits

Counters include setup and explicit collections from process startup:

| Field | Meaning |
| --- | --- |
| `active` | Work pending when the result is allocated |
| `cycles` | Completed incremental cycles |
| `pauses` | Successful collector invocations, including synchronous collections |
| `requestedSlices` | Invocations made with a positive slice budget, including a budgeted request that required recovery |
| `fallbacks` | Partial cycles discarded for synchronous recovery or exception cleanup |
| `forcedFinishes` | Active cycles entered with an unbudgeted request, including explicit collection and allocation pressure |
| `lastCycleSlices` | Stops used by the last completed incremental cycle |
| `totalPauseMs`, `lastPauseMs`, `maxPauseMs` | Collector ownership timings, including rendezvous, tracing, finishing work and release |
| `maxSlicePauseMs` | Longest invocation requested with a slice budget |
| `maxSynchronousPauseMs` | Longest unbudgeted invocation |

These counters describe the collecting thread's successful invocations. They are not a distribution of every application thread's wait. Several assists can occur in one allocation burst, and explicit APIs can finish synchronously. Measure frame or request latency as well. The benchmark executable includes `incrementalGc` counters in its JSON when built with the flag.

Development measurements must compare the same workload, source, toolchain and hardware against the existing libhxcpp collector, with verification and test controls omitted. Record checksums, frame or request distributions, throughput, RSS and reserved memory. On the development machine `gc_frames` runs 10 to 16% slower than the existing collector and `gc_pauses` 5 to 9%, with the slowest 0.1% of work units under 1 ms against 5 to 8 ms, and up to 10% more peak memory. The harness's full collection between samples resets the existing collector's periodic full reclaim, so it hides that stop: compare with those collections removed as well. Do not treat either result as a universal speedup or a hard pause guarantee. The p99.9, maximum interruption, throughput and memory gates in #372 remain the acceptance criteria for promoting this mode beyond experimental use.

## Supported configurations

The initial implementation targets native C++ with Haxe 4.3 or later in the 4.x series. It rejects Emscripten, cppia/scriptable hosts, older Haxe versions and Haxe 5 API layouts, explicit nursery or generational mode, moving mode, always-move checks and recursive GC debug levels. Those paths need separate barrier, layout and scheduling work. `HXCPP_GC_INCREMENTAL_BARRIERS` is an internal macro marker, not a user setting. Rebuild native integrations with matching flags and audit their pointer stores before enabling the mode.

The branch began at `4cdca960`. Before runtime changes, nine Windows x64 baseline runs passed: both mutation fixtures under default, generational and moving collectors, weak-map closure under default, and the two lifecycle fixtures under generational mode. [Development results](../bench/results/incremental-gc/README.md) record later validation, the comparison protocol and raw timing data. These measurements do not change the published README benchmark results.
