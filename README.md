# libhxcpp

[![main](https://github.com/openfl/libhxcpp/actions/workflows/main.yml/badge.svg)](https://github.com/openfl/libhxcpp/actions/workflows/main.yml)
[![License: BSD](https://img.shields.io/badge/license-BSD-blue.svg)](LICENSE.txt)

libhxcpp is the runtime for the C++ target of the [Haxe](https://haxe.org/) compiler: the headers, garbage collector, standard library and build tool that turn the C++ Haxe generates into a native program. It continues hxcpp from its full history. It is installed as the haxelib `libhxcpp`, and one setup command makes everything that asks for hxcpp use it, so existing projects need no changes.

## Highlights

- **More than 170 fixes and improvements on top of hxcpp 4.3.** Garbage collector correctness (moving GC heap corruption, `HXCPP_GC_BIG_BLOCKS`, threads that start or end during a collection), memory-safety bugs reachable from plain Haxe code, sockets and TLS (mbedTLS 4.1 LTS, TLS 1.3, session resumption), MySQL 8 and SQLite, string and number formatting that matches the other Haxe targets, and faster maps, strings, sorting and cppia. [Changes.md](Changes.md) lists them all.
- **Tested releases.** A release is cut deliberately from a commit CI has tested, and ships with the build tool and the cppia host prebuilt for Windows, macOS and Linux.
- **Broad CI.** Every change runs the test suites on Windows (32 and 64-bit), macOS (x86_64 and Arm64) and Linux (32 and 64-bit) with Haxe 4.3.7, runs the GC tests with big blocks, compiles the GC under 32 define configurations at both block sizes, and checks installing with haxelib. The same suites run against the Haxe nightly build once a day.
- **Open development.** Issues and pull requests are welcome.

## Performance

The benchmark suite in [bench](bench) builds the same Haxe sources twice, once against libhxcpp 6.0.0 and once against stock hxcpp 4.3.53 (the release Lime's CI builds with), with the same Haxe, C++ compiler and flags. The key measures from the latest run:

<!-- bench:start -->
| Area | Benchmark | libhxcpp 6.0.0 vs hxcpp 4.3.53 |
| --- | --- | ---: |
| Allocation | Small objects (`alloc_small`) | ~0.97x |
| Allocation | Strings (`alloc_strings`) | 2.08x |
| Collection pauses | Slowest 0.1% of work units (`gc_pauses`) | 2.56x |
| Maps | Int keys (`map_int`) | ~0.97x |
| Maps | String keys (`map_string`) | 1.31x |
| Maps | Object keys (`map_object`) | 7.18x |
| Strings | Building with StringBuf (`string_build`) | 1.56x |
| Sorting | Objects with a comparator (`sort_object`) | ~0.95x |
| CPPIA | Virtual calls in the JIT (`cppia_jit_virtual_call`) | ~1.0x |
| CPPIA | Object allocation in the JIT (`cppia_jit_alloc_objects`) | 0.93x |
| Startup | Start and exit (`noop`) | 1.08x |
| Memory | Peak memory, all benchmarks | 1.03x median, 0.77x worst (`map_string`) |

Above 1 favours libhxcpp; "~" marks a difference within the run-to-run spread, or for speed within the 5% that code layout alone can move it. Windows, AMD Ryzen 9 9950X 16-Core Processor; MSVC 194435213, 64-bit; Haxe 4.3.7; libhxcpp 6.0.0 at 72eedd0b; 2026-10-26 08:50:01.
<!-- bench:end -->

[BENCHMARKS.md](BENCHMARKS.md) has every benchmark, with absolute numbers, collection pause percentiles, peak memory for each benchmark and how the comparison works. These numbers are from one Windows machine; Linux and macOS results are still to come. Where libhxcpp is still slower than stock hxcpp, that is a bug we are working on, and the [6.0.0 checklist](https://github.com/openfl/libhxcpp/issues/192) tracks it.

## Installing

libhxcpp is a haxelib of its own, `libhxcpp`, that stands in for hxcpp. Install it, then run its setup once:

```
haxelib install libhxcpp
haxelib run libhxcpp setup
```

`main` is the development version of libhxcpp 6.0.0. Until 6.0.0 is published, install it from git instead:

```
haxelib git libhxcpp https://github.com/openfl/libhxcpp
haxelib run libhxcpp setup
```

A git checkout needs its build tool compiled once. The first build offers to do it, or you can do it yourself:

```
cd <the path printed by: haxelib libpath libhxcpp>
cd tools/hxcpp
haxe compile.hxml
```

A release can also be installed from its zip on the [releases](https://github.com/openfl/libhxcpp/releases) page, with the build tool and the cppia host already built: `haxelib install libhxcpp-<version>.zip`, then the setup.

### What setup does

The Haxe compiler, `-lib hxcpp` and libraries that depend on hxcpp all ask haxelib for `hxcpp`. Setup points that name at libhxcpp, with `haxelib dev hxcpp <libhxcpp's folder>`, so they all get the same runtime. hxcpp itself, if it is installed, is not touched.

After `haxelib update libhxcpp`, the next build notices the newer version and moves the redirect to it by itself.

Setup applies to the haxelib repository it runs in, so a project with a local repository (`haxelib newrepo`) can use libhxcpp while other projects keep hxcpp.

### Switching back to hxcpp

```
haxelib run libhxcpp off
```

Everything uses hxcpp again straight away. If hxcpp is not installed, install it with `haxelib install hxcpp`.

## Using libhxcpp

Your project needs no changes. A build that uses the library, as `-lib hxcpp` or `-lib libhxcpp`, gets the defines `hxcpp` and `libhxcpp`, both set to libhxcpp's version, so code can tell which runtime it is built against:

```haxe
#if libhxcpp
// built against libhxcpp
#end

#if (libhxcpp >= version("6.0.1"))
// built against libhxcpp 6.0.1 or newer
#end
```

Compare versions with `version()`. A comparison with a plain string, such as `(libhxcpp >= "6.0.1")`, compares text and goes wrong once a part of the version reaches two digits. In a release, native code can include `HxcppVersion.h` for `LIBHXCPP_VERSION`.

## Sharing mbedTLS with a library

Haxe's TLS (`sys.ssl`, and `haxe.Http` with https) runs on mbedTLS, which libhxcpp bundles. A library that bundles mbedTLS too, such as Lime, can offer its copy: in static builds (iOS, tvOS, `static_link`) libhxcpp then builds its TLS on the library's mbedTLS instead of its own, so the app carries one copy. The library sets the define `hxcpp_mbedtls_provider` to a small provider file describing its mbedTLS; libhxcpp uses it when the mbedTLS major version is one it supports, and its own copy otherwise. [docs/MbedTLSProvider.md](docs/MbedTLSProvider.md) describes how to offer one.

## Reloading cppia code

A host that loads replacement cppia code in a running process can release the module it replaces with `untyped module.release()`. Its classes, statics and resources are let go at once, and its code is freed once nothing from it is still in use, so repeated reloads do not grow memory. [docs/CppiaModuleRelease.md](docs/CppiaModuleRelease.md) describes the contract and its limitations: profiler and telemetry builds keep released modules loaded.

## Compatibility

libhxcpp keeps hxcpp's build XML, defines and API, and works with the code Haxe 4.2 and later generates for hxcpp. A few fixes deliberately change behaviour to match the other Haxe targets, for example how `Std.string` formats floats, NaN and Infinity, and TLS connections now require TLS 1.2 or newer. Changes.md describes each one.

### Haxe versions

| Haxe | libhxcpp 6 |
| --- | --- |
| 4.2 and 4.3 | Supported. Every change is tested with 4.3.7 |
| 5.0.0-preview.1 | Supported. Every change runs libhxcpp's suites and Haxe's own unit tests with it on Linux, and the core suites on Windows and macOS |
| 5 nightly builds | Not supported. libhxcpp supports Haxe 5 releases, and nightlies change between them |
| Older than 4.2 | Not supported. The build stops and says so |

Haxe keeps one `hxcpp_api_level` for a whole release cycle while the code it generates changes under it, so the level alone cannot say which runtime a build needs: 5.0.0-preview.1 and the nightlies after it both pass 500. libhxcpp tells them apart by the Haxe build each one names in the code it generates, where a nightly adds `+<commit>` to its version. A released build that generates code for an earlier runtime than its level says, as 5.0.0-preview.1 does for the level 430 runtime, is built against that runtime, so it needs no matching snapshot. Build with `-v` to see which runtime level a build used.

## Versions and releases

libhxcpp follows [semantic versioning](https://semver.org) with its own numbers, starting at 6.0.0. Pre-releases are tagged `-alpha`, `-beta` or `-rc`. Each release is tagged `v<version>`, has its notes in [Changes.md](Changes.md), and is published on haxelib as `libhxcpp` and on the [releases](https://github.com/openfl/libhxcpp/releases) page. [RELEASING.md](RELEASING.md) describes how a release is made.

## Building from source

Build the command-line tools:

```
REPO=$(pwd)
cd ${REPO}/tools/run
haxe compile.hxml
cd ${REPO}/tools/hxcpp
haxe compile.hxml
cd $REPO
```

Build the cppia host:

```
REPO=$(pwd)
cd ${REPO}/project
haxe compile-cppia.hxml
cd $REPO
```

Then you can run a cppia script with `haxelib run hxcpp file.cppia`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for building against a checkout, running the test suites and what a pull request needs. Report problems on the [issue tracker](https://github.com/openfl/libhxcpp/issues).

## License

libhxcpp is under the BSD-style license in [LICENSE.txt](LICENSE.txt), the same as hxcpp. Copyright in hxcpp's code stays with its contributors; the work added since 2025 is copyright Dimensionscape LLC. Bundled third-party libraries keep their own licenses.
