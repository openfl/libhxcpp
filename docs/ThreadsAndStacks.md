Threads And Stacks
-------------------


### Conservative, co-operation
Hxcpp uses conservative stop-the-world GC, where the threads need to co-operate.
 - Threads must not change GC pointers in the collection phase
 - The thread stacks/registers must be scanned for GC pointers
 - Threads must not block without letting the GC system know not to wait for them, otherwise GC blocks until end of block 
   + call hx::GCEnterBlocking() / gc_enter_blocking() / (cpp.vm.Gc.enterGCFreeZone() from Haxe) before potentially blocking system call (fs, network, etc)
   + call hx::GCExitBlocking() / gc_exit_blocking() / (cpp.vm.Gc.exitGCFreeZone() from Haxe) before making more GC calls
   + Might need to pre-allocate buffers
   + Don't forget the exit blocking on error condition

### Foreign Threads
When you create a thread from haxe, it starts attached.  Before a non-haxe created thread can interact with hxcpp, some care must be taken, since GC allocations are done using a GC context per thread, and all threads must respect the stopped world.
  - Foreign threads must be attached-detached
     - SetTopOfStack(int * inTop,bool inPush)
     - *inTop* = pointer to top of stack to attach, or '0' to remove stack
     - *inPush* = usually true.  recursive attachment/detachment
  - Must not change things when the world is stopped
  - Must define their stack range for scanning
  - If you are attached, you may need to enter/exit gc free zone
  - Must release context when done, if no more calls are going to be made
  - Make sure local variables are covered in stack
    - compiler may reorder, so be careful
  - Read documentation because some things, eg audio callbacks, happen on other threads
  - You can use other techniques, eg
     - create a haxe thread, which blocks waiting for signal
     - foreign thread generates request and signals haxe thread
     - haxe thread performs job and generates data then signals foreign thread
     - foreign picks up data and carries on




### Top of Stack

 - To understand how to handle threads, you need a mental picture of the c++ stack
 - The stack usually goes "down".  That is, if the first stack location is 10000, the next one will be 9999 etc.
 - Historical, but consistent.  Except for emscripten which goes up - but still use same terminology/picture, just change the less-thans to greater-thans in code.

Say the system starts each program stack at 10000, the stack might look like this, with local variables and arguments pushed on the stack:

```
 10000 
 -----------------------------------------------
 9996  startup temp variable
 9992  startup temp variable
       -- main function --
 9988  main return address    - order and details of this are ABI specific
 9984  char ** argv
 9980  int     argc
```

Hxcpp then runs it main code, which starts with the macro HX_TOP_OF_STACK, which expands to something like:
```
   int t0 = 99;
   hx::SetTopOfStack(&t0,false);
   ...
   __boot_all();
   __hxcpp_main();

       -- main function --
 9988  main return address      order and details of this are ABI specific
 9984  char ** argv
 9980  int     argc
 9976  int     t0
       -- hx::SetTopOfStack --

    records '9976' as top of stack for this thread
```

Later, many generated functions deep, `__hxcpp_main` generates an allocation call which
triggers a collection

```
 ...
 8100  Array<Bullet>   bullets
       -- alloc Enemy --
 ...
       -- Call collect --

 8050 int   bottomOfStackTemp
      MarkConservative(&bottomOfStackTemp, 9976) -> scans stack from 8050 -> 9976
      MarkConservative(Capture registers)

```

Enter/exit use similar technique, where the registers are captured and the bottomOfStack is 'locked-in' when the "enter gc free zone" call is made.
```
 8100  Array<Bullet>   bullets
       -- EnterGCFreeZone --
 8088 int   bottomOfStackTemp
      thread->setBottomOfStack(&bottomOfStackTemp)
      thread->captureRegisters()
      return
      * any changes here will not affect GC
```

Now, when another thread does a collection, the gc-free thread can be scanned from 8088 to 9976, regardless of any stuff happening lower dowsn the stack.


### Not Called From Main 

Top of stack can be tricky to get right when a gui framework does not really have a "main".


```
 10000 
 -----------------------------------------------
 9996  startup temp variable
 9992  startup temp variable
       -- main function --
       setupWindows(onReadyCallback)......
          ...
 8000  
       -- onReadyCallback --
 7976  int     t0
       SetTopOfStack(&t0,false) -> 7966
       __hxcpp_main();
          setOnFrameCallack(haxeOnFrame)
          return;
```

Later, the haxeOnFrame callback is trigger, but not "below" `__hxcpp_main`

```
 9800  -- haxeOnFrame ---
     // Top of stack will be below bottom of stack.

```

Solutions:
  - Make sure you get in at top of main
    + may scan too much?
  - Ratchet up top-of-stack in callbacks, inForce = false
    + gc_set_top_of_stack(void * inTopOfStack,bool inForce);
  - Detach main thread after hxcpp_main and reattach each callback
    + android solution because render callbacks happen on different threads
    + gc_set_top_of_stack(&base,true); // attach
    + gc_set_top_of_stack(0,true); // detach



### Entering main more than once

Some embedders call the program's `main` more than once: a wasm host calling `callMain` again, or a plugin host that loads a Haxe module and runs it per document. Two things carry over from one run to the next.

The thread stays attached with the first run's top of stack. The generated `main` starts with `HX_TOP_OF_STACK`, which is `hx::SetTopOfStack(&t0,false)`, and that call only ever widens the top: if the next run starts higher up the stack the top moves up, and if it starts deeper the old top stays, so collections keep scanning up to wherever the first run began. Release the thread between runs, from native code once `main` has returned:

```
hx::SetTopOfStack(0,false);
```

`SetTopOfStack(&top,false)` holds the thread attached until a `SetTopOfStack(0,false)`, and each `SetTopOfStack(&top,true)` adds a count that a `SetTopOfStack(0,true)` takes away. Once neither holds it, the thread is released: its stack range is cleared and it stops being scanned. The next `main` attaches it again with its own top of stack. Only release from outside Haxe code, with no Haxe frames still on the stack.

A wasm host can export the call from the Haxe side and call it from JavaScript after `callMain` returns:

```haxe
@:cppFileCode('
#include <emscripten.h>
extern "C" EMSCRIPTEN_KEEPALIVE void hxcpp_release_main() { hx::SetTopOfStack(0,false); }
')
class Main { ... }
```

Do not release a thread that keeps running Haxe code after `main` returns, such as an emscripten main loop or a GUI framework's callbacks: those rely on the attachment `main` made.

Each run also boots the program again. `main` calls `hx::Boot()` and `__boot_all()`, which run every class's static initialisers, so statics start from their initial values in each run; what the previous run left reachable only from statics becomes garbage. Nothing closes the previous run's threads, sockets, files or native handles, so close them before `main` returns.

The generated `main` returns 0, or -1 after printing an uncaught exception. `Sys.exit(code)` calls the C `exit` instead of returning from `main`.

A Haxe exception that never reaches `main`'s catch, as one thrown in a callback the OS or a library calls, through a `noexcept` function, or out of the function of a thread Haxe did not start, ends the process through `std::terminate`. Before it does, hxcpp prints it as `main` would, its stack and then `Error : ` and the message, and says it escaped through native code. A program linked for the Windows subsystem also shows it in a message box. With MSVC's runtime, which keeps a terminate handler for each thread, this covers the threads the collector has known: the threads Haxe starts, and foreign threads once they attach.


### Cppia script stacks

A thread that runs cppia scripts keeps their frames, the arguments and locals of each script call, on a script stack of its own, apart from its native stack. A call that would pass its end throws "Stack Overflow" in the script. It is 128 KB by default, which a script uses up after a few hundred to a few thousand nested calls, depending on how many locals its functions have.

`HXCPP_CPPIA_STACK_SIZE` sets the size in bytes for every thread, the main thread included:

```
-D HXCPP_CPPIA_STACK_SIZE=16777216
```

A host can also change it while it runs. The new size applies to each thread that attaches from then on, and to the calling thread when no script frame is on its stack, as in the host's `main` before it runs a script. A thread that is already running keeps its size, since its stack cannot be replaced under a running script, so set the size before starting the threads that run scripts:

```haxe
untyped __global__.__scriptable_cppia_set_stack_size(16 << 20);
// The calling thread's size
var bytes:Int = untyped __global__.__scriptable_cppia_stack_size();
```

Sizes are rounded up to a multiple of 64 KB, and 64 KB is the smallest.

On Windows, Linux, Android and Apple platforms a stack reserves address space for its whole size and commits memory as frames reach it, as a native thread stack does, so a large size costs only address space until a script recurses that deep. Elsewhere, Emscripten included, the stack is allocated at its full size. Either way the collector scans only the part in use. The stack never moves, since script frames, calls from the host into scripts and jitted code all hold pointers into it.

Each script call also takes native stack, several hundred bytes for an interpreted call and less for a jitted one. A thread that runs out of native stack before its script stack crashes instead of throwing, so raise both when scripts recurse deep. On Windows the linker's `-STACK` flag sets the main thread's native stack and the default for the threads a program starts; a Haxe class can add it with `@:buildXml('<target id="haxe"><flag value="-STACK:268435456" if="windows"/></target>')`. On Linux and macOS the main thread's native stack comes from `ulimit -s`, usually 8 MB; the threads a program starts get the same on Linux and 512 KB on macOS.


### Debugging.
  - in debug mode, hxcpp will check for calls from unattached threads
  - hxcpp can log conservative ranges.  With a native debugger you can check the address of
     your local variables and ensure they are included.
  - hxcpp will scan native objects on the stack, but will not follow non-haxe pointers to other objects, so additional GC roots may be required.


