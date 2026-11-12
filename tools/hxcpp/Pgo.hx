import haxe.io.Path;
import sys.FileSystem;

using StringTools;

// Profile-guided optimisation: HXCPP_PGO=generate builds a program that
//  counts what it runs, HXCPP_PGO=use builds it again from those counts.
//
// The counts go in HXCPP_PGO_DIR (default "pgo" in the build directory).
//  HXCPP_PGO_RUN_DIR names the folder the program is run from when that is
//  not where it is linked (a tool that copies it to an application folder):
//  MSVC's profiling runtime is put there too, and the counts MSVC writes
//  beside the program are gathered from there as well.
//  MSVC applies them when it links, GCC and Clang when they compile, so a
//  change of mode or of counts relinks an MSVC build and recompiles a GCC or
//  Clang one. The objects have a folder of their own, so the plain optimised
//  build's objects are left alone.
class Pgo
{
   public static var active(default,null) = false;
   public static var stampFile(default,null):String = null;

   static var generate = false;
   static var family = "";
   static var dir = "";
   static var runDir:String = null;
   static var defines:haxe.ds.StringMap<String>;

   // Before finish-setup.xml: sets the defines the toolchain files test
   public static function setup(inDefines:haxe.ds.StringMap<String>)
   {
      defines = inDefines;
      if (!defines.exists("HXCPP_PGO"))
         return;

      var mode = defines.get("HXCPP_PGO");
      if (mode!="generate" && mode!="use")
         Log.error('HXCPP_PGO must be "generate" or "use", not "$mode"');

      if (defines.exists("debug"))
      {
         Log.warn("HXCPP_PGO is ignored in a debug build");
         defines.remove("HXCPP_PGO");
         return;
      }
      if (defines.exists("HXCPP_FAST_LINK"))
         Log.error("HXCPP_PGO cannot be combined with HXCPP_FAST_LINK");

      var toolchain = defines.get("toolchain");
      if (toolchain=="msvc" && !defines.exists("winrt") && !defines.exists("HXCPP_MSVC_LLVM"))
         family = "msvc";
      else if (toolchain=="mac" && !defines.exists("xcompile"))
         family = "clang";
      else if (toolchain=="linux" && !defines.exists("xcompile"))
         family = isClang() ? "clang" : "gcc";
      else
         Log.error("HXCPP_PGO needs MSVC on Windows, GCC or Clang on Linux, or Clang on macOS, building for the machine it runs on");

      generate = mode=="generate";
      dir = defines.exists("HXCPP_PGO_DIR") ? defines.get("HXCPP_PGO_DIR") : "pgo";
      if (!PathManager.isAbsolute(dir))
         dir = PathManager.combine(Sys.getCwd(), dir);
      dir = Path.normalize(dir);
      PathManager.mkdir(dir);

      defines.set("HXCPP_PGO_DIR", dir);
      if (defines.exists("HXCPP_PGO_RUN_DIR"))
      {
         runDir = defines.get("HXCPP_PGO_RUN_DIR");
         if (!PathManager.isAbsolute(runDir))
            runDir = PathManager.combine(Sys.getCwd(), runDir);
         runDir = Path.normalize(runDir);
      }
      defines.set("HXCPP_OPTIMIZE_LINK", "1");
      defines.set(generate ? "HXCPP_PGO_GENERATE" : "HXCPP_PGO_USE", "1");
      defines.set("HXCPP_PGO_" + family.toUpperCase(), "1");
      active = true;
   }

   // The compiler the Linux toolchain will run, as linux-toolchain.xml picks it
   static function isClang()
   {
      var cxx = defines.exists("CXX") ? defines.get("CXX") : (defines.exists("bsd") ? "c++" : "g++");
      var parts = cxx.split(" ");
      try
      {
         for(line in ProcessManager.readStdout(parts[0], parts.slice(1).concat(["--version"])))
            if (line.indexOf("clang")>=0)
               return true;
      }
      catch(e:Dynamic) { }
      return false;
   }

   // Before the targets build: checks and merges the counts, and starts the
   //  objects again when the mode or the counts have changed
   public static function prepare(objDir:String)
   {
      if (!active)
         return;

      if (!generate && family=="msvc")
         gatherMsvcCounts();

      var counts = countFiles();
      if (!generate)
      {
         if (counts.length==0)
            Log.error('HXCPP_PGO=use found no counts in $dir: build with HXCPP_PGO=generate, then run the program');
         if (family=="clang")
            mergeClang(counts);
      }

      var stamp = (generate ? "generate" : "use") + "\n" + dir + "\n";
      if (!generate)
         for(file in counts)
         {
            var stat = FileSystem.stat(file);
            stamp += file + " " + stat.size + " " + stat.mtime.getTime() + "\n";
         }

      stampFile = objDir + "/pgo.stamp";
      var last = FileSystem.exists(stampFile) ? sys.io.File.getContent(stampFile) : "";
      if (last!=stamp)
      {
         // MSVC's objects are the same in both modes; it is only the link
         //  that changes, and the newer stamp brings that about
         if (family!="msvc" && FileSystem.exists(objDir))
         {
            Log.info("", " - \x1b[1mPGO:\x1b[0m starting the objects again for " + (generate ? "generate" : "use"));
            PathManager.removeDirectory(objDir);
         }
         PathManager.mkdir(objDir);
         sys.io.File.saveContent(stampFile, stamp);
      }
   }

   // After a program or library is linked
   public static function linked(output:String)
   {
      if (!active)
         return;
      if (!generate)
      {
         // The optimised program no longer needs MSVC's profiling runtime
         //  where it runs (the copy beside the link is left with the build)
         if (family=="msvc" && runDir!=null)
         {
            var runtime = Path.join([runDir, "pgort140.dll"]);
            if (FileSystem.exists(runtime) && programFolders(Path.directory(output)).length>1)
               FileSystem.deleteFile(runtime);
         }
         return;
      }

      // Counts from an earlier build of the program no longer fit it
      for(file in countFiles())
         FileSystem.deleteFile(file);
      if (family=="msvc")
         for(folder in programFolders(Path.directory(output)))
            for(file in msvcCountsBeside(folder))
               FileSystem.deleteFile(file);
      var merged = dir + "/default.profdata";
      if (FileSystem.exists(merged))
         FileSystem.deleteFile(merged);

      // The instrumented program needs MSVC's profiling runtime beside it,
      //  wherever it is run from
      if (family=="msvc")
      {
         var found:String = null;
         for(folder in programFolders(Path.directory(output)))
         {
            var dest = Path.join([folder, "pgort140.dll"]);
            if (FileSystem.exists(dest))
               continue;
            if (found==null)
               found = findOnPath("pgort140.dll");
            if (found==null)
            {
               Log.warn("pgort140.dll is not on the PATH: the instrumented program needs it beside it to run");
               break;
            }
            PathManager.mkdir(folder);
            sys.io.File.copy(found, dest);
         }
      }
   }

   // Where the program may run: where it is linked, and HXCPP_PGO_RUN_DIR
   static function programFolders(linkedDir:String)
   {
      var folders = [linkedDir=="" ? "." : linkedDir];
      if (runDir!=null && Path.normalize(PathManager.combine(Sys.getCwd(), folders[0]))!=runDir)
         folders.push(runDir);
      return folders;
   }

   // An instrumented MSVC program writes its counts beside itself, as
   //  name!1.pgc, name!2.pgc...; the link reads them from beside the .pgd.
   //  They are moved there under numbers of their own, so runs add up
   static function gatherMsvcCounts()
   {
      var name = defines.get("HXCPP_PGO_NAME");
      var next = 1;
      for(file in countFiles())
      {
         var number = Std.parseInt(Path.withoutExtension(file).split("!").pop());
         if (number!=null && number>=next)
            next = number + 1;
      }
      for(folder in programFolders(Sys.getCwd()))
         for(file in msvcCountsBeside(folder))
         {
            var dest = '$dir/$name!$next.pgc';
            next++;
            sys.io.File.copy(file, dest);
            FileSystem.deleteFile(file);
         }
   }

   static function msvcCountsBeside(inDir:String)
   {
      var prefix = defines.get("HXCPP_PGO_NAME") + "!";
      var found = new Array<String>();
      if (inDir=="")
         inDir = ".";
      if (FileSystem.exists(inDir))
         for(file in FileSystem.readDirectory(inDir))
            if (file.startsWith(prefix) && file.endsWith(".pgc"))
               found.push(Path.join([inDir, file]));
      return found;
   }

   static function countFiles()
   {
      var found = new Array<String>();
      var ext = family=="msvc" ? ".pgc" : family=="gcc" ? ".gcda" : ".profraw";
      // GCC names each file after its object's full path, in folders below
      collect(dir, ext, family=="gcc", found);
      found.sort(Reflect.compare);
      return found;
   }

   static function collect(inDir:String, ext:String, below:Bool, found:Array<String>)
   {
      if (!FileSystem.exists(inDir))
         return;
      for(name in FileSystem.readDirectory(inDir))
      {
         var path = inDir + "/" + name;
         if (FileSystem.isDirectory(path))
         {
            if (below)
               collect(path, ext, below, found);
         }
         else if (name.endsWith(ext))
            found.push(path);
      }
   }

   static function mergeClang(counts:Array<String>)
   {
      var merged = dir + "/default.profdata";
      if (FileSystem.exists(merged))
      {
         var made = FileSystem.stat(merged).mtime.getTime();
         var stale = false;
         for(file in counts)
            if (FileSystem.stat(file).mtime.getTime()>made)
               stale = true;
         if (!stale)
            return;
      }

      var tool = findProfdata();
      var args = ["merge", "-output=" + merged].concat(counts);
      var parts = tool.split(" ");
      if (ProcessManager.runCommand("", parts[0], parts.slice(1).concat(args), true, true, false, "\x1b[1mMerge counts:\x1b[0m " + merged)!=0)
         Log.error("llvm-profdata could not merge the counts in " + dir + "; set LLVM_PROFDATA to the one that matches the compiler");
   }

   // llvm-profdata must be as new as the clang that wrote the counts
   static function findProfdata()
   {
      if (defines.exists("LLVM_PROFDATA"))
         return defines.get("LLVM_PROFDATA");
      if (defines.get("toolchain")=="mac")
         return "xcrun llvm-profdata";

      var cxx = defines.exists("CXX") ? defines.get("CXX").split(" ")[0] : "clang++";
      try
      {
         var beside = ProcessManager.readStdout(cxx, ["-print-prog-name=llvm-profdata"])[0];
         if (beside!=null && PathManager.isAbsolute(beside) && FileSystem.exists(beside))
            return beside;
         var version = ProcessManager.readStdout(cxx, ["-dumpversion"])[0];
         if (version!=null)
         {
            var versioned = "llvm-profdata-" + version.split(".")[0];
            if (findOnPath(versioned)!=null)
               return versioned;
         }
      }
      catch(e:Dynamic) { }
      return "llvm-profdata";
   }

   static function findOnPath(name:String):String
   {
      var path = Sys.getEnv("PATH");
      if (path==null)
         return null;
      for(part in path.split(BuildTool.isWindows ? ";" : ":"))
      {
         if (part=="")
            continue;
         var candidate = part + "/" + name;
         if (FileSystem.exists(candidate))
            return candidate;
      }
      return null;
   }
}
