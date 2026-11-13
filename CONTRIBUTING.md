# Contributing to libhxcpp

Issues and pull requests are welcome.

## Reporting a problem

Say which Haxe and libhxcpp versions you use, the platform and C++ compiler, and any defines that matter (`HXCPP_GC_MOVING`, `HXCPP_GC_BIG_BLOCKS`, `HXCPP_DEBUG`...). A small program that shows the problem is the most useful thing you can add.

## Working on libhxcpp

Point haxelib at your checkout, run the setup so builds that ask for hxcpp use it, and build the build tool once:

```
haxelib dev libhxcpp <path to your checkout>
haxelib run libhxcpp setup
cd <path to your checkout>/tools/hxcpp
haxe compile.hxml
```

`haxelib run libhxcpp off` switches builds back to hxcpp, and `haxelib dev libhxcpp` with no path back to the libhxcpp you had installed.

`test/haxelib/check.sh` checks the setup itself: installing, setup, an update and `off`, in a haxelib repository of its own.

C++ formatting is in `.clang-format`. Format the lines you change (for example with your editor's format-selection command or `git clang-format`), not whole files: much of the existing code predates it, and reformatting a file buries the change that matters. Code under `project/thirdparty` keeps its upstream formatting.

## Tests

Each suite builds against the checkout haxelib points at. Use `-D HXCPP_M32`, `-D HXCPP_M64` or `-D HXCPP_ARM64` for the architecture.

| Suite | Directory | Build | Run |
|---|---|---|---|
| Runtime and GC | `test/haxe` | `haxe compile.hxml -D HXCPP_M64` | `bin/TestMain` |
| Standard library | `test/std` | `haxe compile64.hxml` | `cpp64/Test` |
| Regressions, one program each | `test/regression` | `haxe --run Run -D HXCPP_M64` | (runs them) |
| GC defines (Linux or WSL, g++ or clang++) | repository root | `test/gc-defines/check.sh [compiler]` | (runs them) |
| GC under load, one collector mode per build | `test/gc_stress` | `haxe compile.hxml -D HXCPP_M64` plus the mode's defines | `bin/Stress 30` |

`test/haxe` needs the `utest` library. `test/std` needs `hx4compat` and utest from git (`haxelib git utest https://github.com/haxe-utest/utest`).

## Defines and configurations

Most of the collector's defines are not used by any default build, so code behind them breaks without a test noticing.

- Write one form of the code for every configuration when the cost is negligible: one type that holds the largest value any block size needs, not one type per define. Where a type must hold something derived from the block size, tie it to the block geometry with a `static_assert`.
- A new or newly documented define comes with coverage: an entry in `test/gc-defines/check.sh` (or the equivalent build check for its area), a mode in the GC stress jobs in `.github/workflows/test.yml` when it changes how the collector runs, and a line in `docs/build_xml/Defines.md`.
- A CI failure is fixed, or tracked as an issue and fixed, never merged around, even when `main` fails the same way.
- The Haxe 5 preview workflow is the exception: Haxe 5 is not released, so its failures are warnings. Each still gets an issue, but it holds back neither a merge nor a release.
- A pull request runs a smaller set: the quick checks, the sanitizers and the core suites on Linux 64, Windows 64 and macOS arm64. A push to `main` runs everything, Haxe 5 included, and what fails there after a merge is fixed or tracked the same way. To run everything before merging, run the main workflow on the branch from the Actions tab.

## Pull requests

- Open them against `main`, one fix or feature each.
- Add a test that fails without the change when one is possible: a case in `test/haxe` or `test/std`, or a program of its own in `test/regression` when the problem needs a fresh process (a crash, something at exit, a race).
- Say what was wrong and how the change fixes it, in the pull request and in the commit message.
- Add a line to the unreleased section at the top of [Changes.md](Changes.md) for anything a user would notice. Releases themselves are described in [RELEASING.md](RELEASING.md).

## License

Contributions are made under the project's license, in [LICENSE.txt](LICENSE.txt). You keep the copyright in what you contribute.
