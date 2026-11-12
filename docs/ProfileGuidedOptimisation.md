Profile-guided optimisation
---------------------------

`HXCPP_PGO` builds a release program from a record of what it did when it ran. Build it instrumented, run it on
the work it is for, and build it again from the counts that run left:

```sh
haxe build.hxml -D HXCPP_PGO=generate
./Main
haxe build.hxml -D HXCPP_PGO=use
```

It works with MSVC on Windows, GCC or Clang on Linux (the one `CXX` names, else g++), and Clang on macOS, for the
machine that builds. It turns on `HXCPP_OPTIMIZE_LINK`, which it needs, and a debug build ignores it. It cannot be
combined with `HXCPP_FAST_LINK`.

Trained and timed on a replication benchmark, a best case, it took a further 4 to 29% off the time of the
link-time optimised program with MSVC, 19 to 43% with GCC and 12 to 46% with Clang.

### Training

- The instrumented program is slower, and writes its counts when it exits normally: returning from `main` or
  calling `Sys.exit`. A process that is killed writes none, and with MSVC a program that kills another running copy
  of itself loses its own counts as well. Several runs add up.
- Train on the work that matters. Code no run reached is optimised as usual with GCC, and for size with MSVC and
  Clang.

### Where the counts go

- The counts are kept in `pgo` in the build folder; `-D HXCPP_PGO_DIR=<path>` puts them elsewhere.
- An instrumented MSVC program writes its counts beside itself, as `Main!1.pgc` and so on; the next `use` build
  moves them into `pgo`, beside the `.pgd` the link reads them from. The `generate` build copies MSVC's profiling
  runtime, `pgort140.dll`, beside the program, which needs it to run.
- With Clang the counts are merged with `llvm-profdata`, which has to be as new as the compiler. The build looks
  beside the compiler, then for `llvm-profdata-<version>` and `llvm-profdata` on the `PATH`, and uses `xcrun` on
  macOS; set `LLVM_PROFDATA` to choose one.

### Rebuilds

The objects go in a folder of their own, so the plain optimised build's are left alone. MSVC applies the counts
when it links and GCC and Clang when they compile, so a change of mode or of counts relinks an MSVC build and
recompiles a GCC or Clang one. A `generate` build that links clears the counts, which no longer fit the new
program, and a `use` build without any refuses.
