# Benchmarks

The benchmark suite in [bench](bench) compares libhxcpp 6.0.0 with stock hxcpp 4.3.53, the release Lime's CI builds desktop and mobile targets with. The [README](README.md#performance) shows a short summary of the key measures; this page has every result.

## Results

<!-- bench:start -->
**Speed** (higher is better)

| Benchmark | Unit | libhxcpp 6.0.0 | hxcpp 4.3.53 | libhxcpp vs stock |
| --- | --- | ---: | ---: | ---: |
| alloc_small | objects/s | 550M ±0.9% | 565M ±4.1% | ~0.97x |
| alloc_strings | strings/s | 48.9M ±0.3% | 23.5M ±0.6% | 2.08x |
| alloc_arrays | arrays/s | 20M ±1.2% | 20.5M ±0.8% | ~0.98x |
| gc_pauses | objects/s | 89.5M ±2.3% | 57.5M ±4.1% | 1.56x |
| map_int | map operations/s | 121M ±1% | 125M ±0.8% | ~0.97x |
| map_string | map operations/s | 31M ±3% | 23.7M ±2% | 1.31x |
| map_object | map operations/s | 74.5M ±3.6% | 10.4M ±3.4% | 7.18x |
| string_build | appends/s | 64.5M ±1.6% | 41.5M ±2.5% | 1.56x |
| string_index | indexOf calls/s | 1.9M ±1.8% | 212k ±0.3% | 8.82x |
| string_substr | substr calls/s | 79.8M ±0.7% | 81.3M ±0.6% | ~0.98x |
| string_split | split calls/s | 27.3k ±1.7% | 22.5k ±0.6% | 1.21x |
| string_case | case conversions/s | 37.4M ±0.6% | 1.5M ±0.4% | 25.01x |
| sort_int | elements/s | 2.8M ±4.4% | 2.9M ±2.6% | ~1.0x |
| sort_string | elements/s | 1.1M ±6.7% | 717k ±5.1% | 1.49x |
| sort_object | elements/s | 3.6M ±3.5% | 3.8M ±3.3% | ~0.95x |

**CPPIA speed** (higher is better)

| Workload | Unit | Interpreter, libhxcpp 6.0.0 | Interpreter, hxcpp 4.3.53 | Interpreter vs stock | JIT, libhxcpp 6.0.0 | JIT, hxcpp 4.3.53 | JIT vs stock |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| int_loop | iterations/s | 44.9M ±2.1% | 43.4M ±2.5% | ~1.03x | 739M ±2% | 739M ±1.9% | ~1.0x |
| float_array | elements/s | 23.2M ±2.2% | 24.3M ±2.9% | ~0.95x | 218M ±0.3% | 222M ±0.6% | ~0.98x |
| alloc_objects | objects/s | 18M ±0.4% | 17.9M ±1.9% | ~1.0x | 96.9M ±0.8% | 104M ±0.6% | 0.93x |
| virtual_call | calls/s | 38.1M ±0.8% | 38.1M ±1.1% | ~1.0x | 293M ±0.3% | 294M ±0.2% | ~1.0x |
| closure_call | calls/s | 19.9M ±0.8% | 19.1M ±3.3% | ~1.04x | 25.1M ±1.6% | 22.7M ±1.7% | 1.11x |
| native_call | calls/s | 72.5M ±0.7% | 72.4M ±2.3% | ~1.0x | 610M ±0.2% | 606M ±0.3% | ~1.01x |
| native_callback | callbacks/s | 27.1M ±0.3% | 26.7M ±0.7% | ~1.01x | 27.1M ±0.5% | 28.1M ±0.6% | ~0.96x |

**Collection pauses and memory** (lower is better)

| Metric | Benchmark | libhxcpp 6.0.0 | hxcpp 4.3.53 | stock / libhxcpp |
| --- | --- | ---: | ---: | ---: |
| Startup and exit | noop | 3.3 ms | 3.6 ms | 1.08x |
| Work unit time, p99 | gc_pauses | 56.9 us | 82.5 us | 1.45x |
| Work unit time, p99.9 | gc_pauses | 4.8 ms | 12.2 ms | 2.56x |
| Work unit time, max | gc_pauses | 6 ms | 17 ms | 2.81x |
| Peak memory | alloc_small | 25.8 MB | 25.4 MB | 0.98x |
| Peak memory | alloc_strings | 25.9 MB | 25.3 MB | 0.98x |
| Peak memory | alloc_arrays | 29.1 MB | 28.8 MB | 0.99x |
| Peak memory | gc_pauses | 136 MB | 131 MB | 0.97x |
| Peak memory | map_int | 33.3 MB | 31.1 MB | 0.94x |
| Peak memory | map_string | 143 MB | 110 MB | 0.77x |
| Peak memory | map_object | 42.7 MB | 68.8 MB | 1.61x |
| Peak memory | string_build | 30.2 MB | 228 MB | 7.56x |
| Peak memory | string_index | 10.4 MB | 8.9 MB | 0.86x |
| Peak memory | string_substr | 25.9 MB | 25.4 MB | 0.98x |
| Peak memory | string_split | 69.5 MB | 72.9 MB | 1.05x |
| Peak memory | string_case | 25.9 MB | 25.4 MB | 0.98x |
| Peak memory | sort_int | 32.2 MB | 28.2 MB | 0.88x |
| Peak memory | sort_string | 34.4 MB | 30.8 MB | 0.89x |
| Peak memory | sort_object | 28.9 MB | 28.4 MB | 0.98x |
| Peak memory | cppia_interp_int_loop | 7.9 MB | 8.8 MB | 1.13x |
| Peak memory | cppia_interp_float_array | 28.5 MB | 29.5 MB | 1.03x |
| Peak memory | cppia_interp_alloc_objects | 27.4 MB | 24.3 MB | 0.89x |
| Peak memory | cppia_interp_virtual_call | 7.9 MB | 8.9 MB | 1.13x |
| Peak memory | cppia_interp_closure_call | 28.4 MB | 29.4 MB | 1.03x |
| Peak memory | cppia_interp_native_call | 7.9 MB | 8.8 MB | 1.13x |
| Peak memory | cppia_interp_native_callback | 28.4 MB | 29.4 MB | 1.03x |
| Peak memory | cppia_jit_int_loop | 8 MB | 9 MB | 1.12x |
| Peak memory | cppia_jit_float_array | 8.1 MB | 9.1 MB | 1.12x |
| Peak memory | cppia_jit_alloc_objects | 27.5 MB | 24.4 MB | 0.89x |
| Peak memory | cppia_jit_virtual_call | 8 MB | 9 MB | 1.12x |
| Peak memory | cppia_jit_closure_call | 28.5 MB | 29.5 MB | 1.03x |
| Peak memory | cppia_jit_native_call | 8 MB | 9 MB | 1.12x |
| Peak memory | cppia_jit_native_callback | 28.5 MB | 29.5 MB | 1.03x |

Windows, AMD Ryzen 9 9950X 16-Core Processor; MSVC 194435213, 64-bit; Haxe 4.3.7; libhxcpp 6.0.0 at 72eedd0b; 3 processes x 10 samples per benchmark; 2026-10-26 08:50:01.
Reproduce with `cd bench && haxe --run Run`. Ratios above 1 favour libhxcpp; "~" marks a difference within the run-to-run spread, or for speed within the 5% that code layout alone can move it.
<!-- bench:end -->

These numbers are from one Windows machine; Linux and macOS results are still to come. Where libhxcpp is slower or uses more memory than stock hxcpp, that is a bug, and the [6.0.0 checklist](https://github.com/openfl/libhxcpp/issues/192) tracks it.

## How the comparison works

- **Same program, two runtimes.** The driver builds the same Haxe sources twice, with the same Haxe, C++ compiler and flags: once against this checkout and once against the stock release, downloaded from GitHub as Lime's CI does. Each runtime gets its own local haxelib repository, and the driver checks which hxcpp each build actually used and that both used the same compiler and word size. On Windows both builds are 64-bit.
- **One benchmark per process.** Each benchmark runs in its own process, so peak memory belongs to that benchmark alone: the peak working set on Windows, the maximum resident set size elsewhere. Each process does 2 untimed warm-up samples and 10 timed samples, and there are 3 processes per benchmark and runtime. The runtimes take turns, so drift on the machine affects both alike.
- **Same work, checked.** Every benchmark returns a checksum of the work it did, and every process under both runtimes has to produce the same one. A change that skips work or computes the wrong answer fails the run instead of looking faster.
- **Spread.** "±" is the median absolute deviation of the sample times, as a percentage of the median. A ratio marked "~" is within twice the larger spread of the two runtimes, so it counts as level.
- **Code layout.** A tight benchmark's speed also moves with where the compiler happens to place code. The same sources built again, or with one function added that nothing calls, measured up to 5% apart on `map_int`, and the same commit measured 7% apart on `alloc_small` depending on which runtime's directory built it. So a speed ratio within 5% of level is marked "~" as well, and a change of that size between two runs of these tables is not a regression on its own.

## The benchmarks

| Benchmark | What it measures |
| --- | --- |
| `alloc_small` | Small objects, almost all short-lived; the last 1024 stay reachable |
| `alloc_strings` | Int-to-String conversion plus a concatenation per string |
| `alloc_arrays` | Arrays of 1 to 64 Ints grown by `push`, plus a 256 KB buffer every 4000 arrays |
| `gc_pauses` | Collection pauses as the program sees them: a large live graph stays reachable while the program allocates in fixed work units, and each unit's duration is recorded. The tail holds the units that included a collection |
| `gc_frames` | 2,000 frames over 150,000 retained entities with graph and inventory mutation, temporary particle chains and event strings, fixed-size spatial cells and periodic allocation bursts. Records frame latency and checks retained payloads. Uses automatic GC without frame hooks or sleeps |
| `map_int` | `IntMap`: on a fresh map, N sets, N hits, N misses, N/2 removes, then iterating the N/2 values left |
| `map_string` | `StringMap`, the same cycle |
| `map_object` | `ObjectMap`, the same cycle |
| `string_build` | `StringBuf` appends of words, spaces and Ints, turned into a String every 64 rounds |
| `string_index` | `indexOf` from spread-out start positions; about half the needles are found |
| `string_substr` | `substr` of short slices, read back with `charCodeAt` |
| `string_split` | `split` of a text on spaces |
| `string_case` | `toUpperCase` and `toLowerCase` of a 64-character line |
| `sort_int` | `Array<Int>` sorted with a comparator closure |
| `sort_string` | `Array<String>` sorted with `Reflect.compare` |
| `sort_object` | Objects sorted by an Int field, with repeated keys |
| `gc_weak_map_int` | Collections with an unchanged `WeakMap` of 262,144 retained keys to Int values; each collection's duration is recorded |
| `gc_object_ids` | Collections with 250,000 retained objects that each have an ID from `__hxcpp_obj_id` |
| `gc_large_stack_roots` | Collections with 65,536 retained 4 KB buffers and twelve raw pointers into them on the stack |
| `gc_regex_wait` | A collection that another thread asks for 5 ms into a match over a 32 MB subject; the latency is how long that request took |
| `gc_zombie_reservations` | Collections with 250,000 retained objects that are each registered with `Gc.doNotKill` |
| `gc_finalizer_promotion` | A full collection after 125,000 retained objects are given finalizers again, which moves the registrations to the old table; no finalizer runs |
| `gc_idle_minor` | 2,000 collections of a heap of 128 retained objects, so that a minor collection has nothing to mark |
| `gc_remembered_burst` | 250,000 retained objects each take a new child, then a collection: a burst of remembered referrers and marking in each round |
| `gc_object_id_burst` | 250,000 new objects given IDs with `__hxcpp_obj_id`, dropped, then a full collection, in each round |
| `gc_weak_map_chain` | Collections with a `WeakMap` of 4,096 entries in which each key's value leads to the next key, and only the first key is held elsewhere |
| `noop` | Starting the program and exiting at once (startup time, measured 20 times per runtime) |

### CPPIA

Each workload below has two benchmark names, `cppia_interp_<workload>` and
`cppia_jit_<workload>`. A full run includes both modes on both runtimes. The
driver also checks that the interpreter and JIT return the same checksum.

| Workload | What it measures |
| --- | --- |
| `int_loop` | Integer arithmetic, bit operations and branches in a script loop |
| `float_array` | Float arithmetic, typed array reads and writes, and Float-to-Int conversion |
| `alloc_objects` | 125,000 script objects per call, with Int and object fields and a bounded live graph |
| `virtual_call` | Virtual calls alternating between two script implementations |
| `closure_call` | Script calls to a closure that updates a captured variable |
| `native_call` | Script-to-native calls with an Int argument and result |
| `native_callback` | A native loop calling a script closure that updates a captured variable |
| `bunny_update` | The update loop of OpenFL's BunnyMark over 2,000 script objects that extend a host class, moved through its Float properties, in a listener the host calls once a frame |

The native timing harness loads one script module per process. Loading, linking,
booting and JIT compilation happen in `setup()`, outside the timed samples.
These measure execution after loading, not module startup or reload. Each sample
repeats its workload enough times to keep timer overhead small, and the operation
count includes every repetition.

The script classes live in `bench/cppia/Script.hx` and are compiled only to
CPPIA. `bench/cppia/shared` defines the interface and native bridge exported by
the host and imported by the script with `dll_export` and `dll_import`. The host
uses `scriptable` and `--dce no`, like the CPPIA tests. It is a separate executable
so those flags do not change the native benchmarks. Peak memory for these cases
includes the host, loaded module and any JIT code. The `noop` startup row still
measures the native executable.

### HashLink as a reference

`--hl <path to hl>` also runs the CPPIA workloads on HashLink's JIT. `bench/hl`
builds `Script.hx` and `bench/cppia/shared` straight to HashLink with the same
harness, workload table and timing, so there the host's side of each workload
is HashLink JIT code too, where under cppia it is native C++. Its processes take
turns with the others on the `cppia_jit_` benchmarks and must give the same
checksums, and the CPPIA table gains an HL/JIT column and the JIT's ratio to it.
A host with its framework compiled in should not run a script slower than
HashLink runs the whole application, so a ratio below 1 there is a target.


## Running it yourself

You need Haxe 4.3, the C++ compiler hxcpp uses on your platform, and curl.

```sh
cd bench
haxe --run Run                       # every benchmark, both runtimes
haxe --run Run --only map_int,sort_int
haxe --run Run --only cppia_interp_native_call,cppia_jit_native_call
haxe --run Run --only cppia_jit_closure_call --hl path/to/hl   # with HashLink beside it
haxe --run Run --no-build            # reuse the last builds
haxe --run Run --only gc_object_ids --define HXCPP_GC_GENERATIONAL
haxe --run Run --readme              # publish: this page and the README summary
```

`--readme` needs every benchmark, both runtimes and a committed tree, so the published numbers always name the commit they were measured on. The driver's [doc comment](bench/Run.hx) lists every option.

## Tracking changes in CI

The [bench workflow](.github/workflows/bench.yml) runs [bench/ci.sh](bench/ci.sh), which builds the suite against a change and against the commit it is based on, on the same runner, and runs them in turns as above. `--base <checkout>` makes Run.hx compare with another libhxcpp checkout instead of stock hxcpp, and [bench/Compare.hx](bench/Compare.hx) reports throughput, tail pauses (p99.9 work unit time) and peak memory separately. Every benchmark the suite lists is included, so new ones are tracked without changing the workflow.

A difference only counts past its noise: twice the larger run-to-run spread of the two builds, and at least 5% for speed (the code layout band above), 25% for tail pauses and 5% for memory. A regression past that is a warning and a line in the job summary; it does not fail the job. Shared runners vary from run to run, so the daily scheduled run compares main with itself, and that A/A spread is what any merge gate would be set from.

Pull requests are compared with the commit they branched from and leave out CPPIA; pushes to main are compared with the commit before. Every run keeps its raw results as an artifact for 90 days, and runs on main append a line to `history.jsonl` on the `bench-history` branch, with each benchmark's numbers and its ratios to the base.

```sh
bash bench/ci.sh HEAD~1                    # this checkout against its parent
bash bench/ci.sh HEAD --no-cppia           # an A/A run of this checkout
haxe --run Compare .work/results.json      # the report again, from bench/
```

## Adding a benchmark

1. Add the class to [bench/src](bench/src) and register it in `Main.hx`. For a CPPIA workload, add its script implementation in `bench/cppia/Script.hx` and register both modes in `CppiaBenchmarks.all()`.
2. Add a row to the table above.
3. If it covers something most programs depend on, add it to `SUMMARY` in `Run.hx` so it also shows in the README. Keep that table short; everything else belongs here.
4. Run `haxe --run Run --readme` from a committed tree and commit the result, so this page and the README stay current.
