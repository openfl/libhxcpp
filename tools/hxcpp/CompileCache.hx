import sys.FileSystem;

class CompileCache
{
   public static var hasCache = false;
   public static var compileCache:String;


   public static function init(inDefines:Map<String, String>) : Bool
   {
      compileCache = "";
      hasCache = false;

      if (inDefines.exists("HXCPP_COMPILE_CACHE"))
      {
         compileCache = inDefines.get("HXCPP_COMPILE_CACHE");
         compileCache = compileCache.split("\\").join("/");
         // Don't get upset by trailing slash
         while(compileCache.length>1)
         {
            var l = compileCache.length;
            var last = compileCache.substr(l-1);
            if (last=="/")
               compileCache = compileCache.substr(0,l-1);
            else
               break;
         }

         if (!FileSystem.exists(compileCache))
         {
            try{
               PathManager.mkdir(compileCache);
            }
            catch(e:Dynamic)
            {
               Log.error("Could not create compiler cache directory \"" + compileCache + "\"");
            }
         }

         if (FileSystem.exists(compileCache) && FileSystem.isDirectory(compileCache))
         {
            hasCache = true;
         }
         else
         {
            Log.error("Could not find compiler cache \"" + compileCache + "\"");
            //throw "Could not find compiler cache: " + compileCache;
         }
      }


      if (hasCache)
      {
         Log.info("", "\x1b[33;1mUsing compiler cache: " + compileCache + "\x1b[0m");
      }

      return hasCache;
   }


/*
   public static function getPchCacheName(inProject:String,hash:String,header:String, inExt:String)
   {
      var dir = compileCache + "/" + inProject + "/pch" + hash.substr(0,8);
      try
      {
         if (!FileSystem.exists(dir))
            PathManager.mkdir(dir);
      } catch(e:Dynamic) { }
      return dir + "/" + header + inExt;
   }
*/

   public static function getPchDir(inProject:String,hash:String)
   {
      var dir = compileCache + "/" + inProject + "/pch" + hash.substr(0,8);
      try
      {
         if (!FileSystem.exists(dir))
            PathManager.mkdir(dir);
      } catch(e:Dynamic) { }
      return dir;
   }

   public static function getCacheName(inProject:String,hash:String,inExt:String)
   {
      var dir = compileCache + "/" + inProject + "/" + hash.substr(0,2);
      try
      {
         if (!FileSystem.exists(dir))
            PathManager.mkdir(dir);
      } catch(e:Dynamic) { }
      return dir + "/" + hash.substr(2) + inExt;
   }

   // Sizing the cache stats every file in it, which took seconds once it held
   //  tens of thousands, and ran before every build, even one with nothing to
   //  compile (libhxcpp #473). Once in ten minutes keeps it near its limit
   public static function dueForTrim():Bool
   {
      var stamp = compileCache + "/.trimmed";
      try
      {
         if (FileSystem.exists(stamp) && Date.now().getTime() - FileSystem.stat(stamp).mtime.getTime() < 10 * 60 * 1000.0)
            return false;
         sys.io.File.saveContent(stamp, "");
      }
      catch(e:Dynamic) { }
      return true;
   }

   // Trimming keeps what builds use, but reusing an entry leaves it as it
   //  was: an object up to date in the cache is linked from there, and
   //  Windows does not update access times. Changing an object's date would
   //  relink every build. So each target lists here the entries its last
   //  build used, and the list's date is when it used them
   static inline var USE_DIR = ".used";

   public static function recordUse(inTarget:String, inEntries:Array<String>)
   {
      if (!hasCache)
         return;
      var prefix = compileCache + "/";
      var seen = new Map<String,Bool>();
      var names = new Array<String>();
      for(entry in inEntries)
      {
         entry = entry.split("\\").join("/");
         if (!StringTools.startsWith(entry, prefix))
            continue;
         var parts = entry.substr(prefix.length).split("/");
         if (parts.length<2)
            continue;
         // A precompiled header's directory is used as a whole
         var name = parts[1].substr(0,3)=="pch" ? parts[0] + "/" + parts[1] : parts.join("/");
         if (!seen.exists(name))
         {
            seen.set(name,true);
            names.push(name);
         }
      }
      names.sort(Reflect.compare);
      var content = names.join("\n");
      var dir = compileCache + "/" + USE_DIR;
      var record = dir + "/" + haxe.crypto.Md5.encode(inTarget);
      try
      {
         // The same entries recorded within the hour is near enough
         if (FileSystem.exists(record) && Date.now().getTime() - FileSystem.stat(record).mtime.getTime() < 3600 * 1000.0 &&
               sys.io.File.getContent(record)==content)
            return;
         if (!FileSystem.exists(dir))
            PathManager.mkdir(dir);
         sys.io.File.saveContent(record, content);
      }
      catch(e:Dynamic)
      {
         Log.v('Could not record the cache entries used in $record: $e');
      }
   }

   // When each entry was last used, as far as the records tell
   static function readUse(ioRecords:Array<{ filename:String, time:Float }>)
   {
      var used = new Map<String,Float>();
      var dir = compileCache + "/" + USE_DIR;
      if (!FileSystem.exists(dir) || !FileSystem.isDirectory(dir))
         return used;
      for(file in FileSystem.readDirectory(dir))
      {
         var filename = dir + "/" + file;
         try
         {
            var time = FileSystem.stat(filename).mtime.getTime();
            ioRecords.push( { filename:filename, time:time } );
            for(name in sys.io.File.getContent(filename).split("\n"))
               if (name!="" && (!used.exists(name) || used.get(name)<time))
                  used.set(name, time);
         }
         catch(e:Dynamic) { }
      }
      return used;
   }

   // When a file in the cache was last used: its own dates, or the last record
   //  that lists it. A precompiled header's directory is recorded as a whole
   static function lastUse(inUsed:Map<String,Float>, inProject:String, inDir:String, inFile:String, inInfo:sys.FileStat)
   {
      var time = inInfo.atime==null ? inInfo.mtime.getTime() : Math.max(inInfo.atime.getTime(),inInfo.mtime.getTime());
      var name = inDir.substr(0,3)=="pch" ? inProject + "/" + inDir : inProject + "/" + inDir + "/" + inFile;
      return inUsed.exists(name) ? Math.max(time, inUsed.get(name)) : time;
   }

   public static function clear(inDays:Int,inMB:Int,inLogInfo:Bool,inProject:String)
   {
      try
      {
        var projects = FileSystem.readDirectory(compileCache);
        var deleted = 0;
        var total = 0;
        var t0 = haxe.Timer.stamp();
        var tooOld = Date.now().getTime() - inDays * 24 * 3600 * 1000.0;
        var sizeKB:Float = 0;
        var fileInfo = [];
        var records = [];
        var used = readUse(records);

        for(project in projects)
        {
           if (inProject!=null && inProject!=project)
              continue;
           // Not the use records, nor the time of the last trim
           if (project.substr(0,1)==".")
              continue;
           var projectHasDirs = false;
           var projDir = compileCache + "/" + project;
           if(!FileSystem.isDirectory(projDir))
               continue;
           var dirs = FileSystem.readDirectory(projDir);
           for(dir in dirs)
           {
              var path = projDir + "/" + dir;
              if(!FileSystem.isDirectory(path)) {
                  FileSystem.deleteFile(path);
                  continue;
              }
              if (dir.length!=2 && dir!="lib" && dir.substr(0,3)!="pch" )
              {
                 Log.warn('bad cache name "$dir" found - try manually clearing');
                 continue;
              }
              var dirFiles = FileSystem.readDirectory(path);
              var allDeleted = true;
              for(file in dirFiles)
              {
                 total++;
                 var filename = path + "/" + file;
                 var doDelete = true;
                 if (inDays>0)
                 {
                    var info = FileSystem.stat(filename);
                    var time = lastUse(used, project, dir, file, info);
                    if (time>=tooOld)
                       doDelete = false;
                 }
                 else if (inMB>0)
                 {
                    var info = FileSystem.stat(filename);
                    var time = lastUse(used, project, dir, file, info);
                    fileInfo.push( {filename:filename, time:time, size:info.size } );
                    sizeKB += info.size/1024;
                    doDelete = false;
                 }

                 if (doDelete)
                 {
                    try
                    {
                    FileSystem.deleteFile(filename);
                    deleted++;
                    }
                    catch(e:Dynamic)
                    {
                       Log.warn('Could not delete $filename');
                    }
                 }
                 else
                    allDeleted = false;
              }
              if (allDeleted)
              {
                 try
                 {
                    FileSystem.deleteDirectory(path);
                 }
                 catch(e:Dynamic)
                 {
                    Log.warn('Could not delete directory $path');
                 }
              }
              else
                 projectHasDirs = true;
           }
           if (!projectHasDirs)
           {
              try
              {
                 FileSystem.deleteDirectory(projDir);
              }
              catch(e:Dynamic)
              {
                Log.warn('Could not delete directory $projDir');
              }
           }
        }
          
        // A record older than every entry kept keeps nothing. Trimming one
        //  project leaves the records, which list others' entries too
        var recordsBefore:Null<Float> = inDays>0 ? tooOld : null;
        if (inMB*1024<sizeKB)
        {
           // newest first
           fileInfo.sort( function(a,b) return a.time > b.time ? -1 : 1 );
           var keepKB:Float = inMB*1024;
           for(info in fileInfo)
           {
              if (keepKB>0)
              {
                 sizeKB -= info.size/1024;
                 keepKB -= info.size/1024;
              }
              else
              {
                 if (recordsBefore==null && inProject==null)
                    recordsBefore = info.time;
                 try
                 {
                    FileSystem.deleteFile(info.filename);
                    deleted++;
                 }
                 catch(e:Dynamic)
                 {
                   Log.warn('Could not delete ${info.filename}');
                 }
              }
           }
        }
        if (inDays<=0 && inMB<=0 && inProject==null)
           recordsBefore = Math.POSITIVE_INFINITY;
        if (recordsBefore!=null)
           for(record in records)
              if (record.time<recordsBefore)
                 try { FileSystem.deleteFile(record.filename); } catch(e:Dynamic) { }

        var t = haxe.Timer.stamp()-t0;
        var projString = inProject==null ? "" : ' from project $inProject';
        var message = inMB > 0 ?
             'Cache: removed $deleted/$total files$projString, leaving ' + Std.int(sizeKB/1024) + 'MB, in $t seconds' :
             'Cache: removed $deleted/$total files$projString in $t seconds';
        if (inLogInfo)
           Log.info(message);
        else
           Log.v(message);
      }
      catch(error:Dynamic)
      {
         Log.warn("Error cleaning cache: " + error);
      }

   }

   public static function list(inDetails:Bool,inProject:String)
   {
      try
      {
        Sys.println('Cache Directory: $compileCache');
        var t0 = haxe.Timer.stamp();
        var files = new Array<String>();
        var projects = FileSystem.readDirectory(compileCache);
        var size = 0.0;
        var count = 0;
        var used = inDetails ? readUse([]) : null;

        for(project in projects)
        {
           if (inProject!=null && inProject!=project)
              continue;
           if (project.substr(0,1)==".")
              continue;
           var projSize = size;
           var projCount = count;
           var projDir = compileCache + "/" + project;
           if(!FileSystem.isDirectory(projDir))
               continue;
           var dirs = FileSystem.readDirectory(projDir);
           for(dir in dirs)
           {
              var path = projDir + "/" + dir;
              if(!FileSystem.isDirectory(path))
                  continue;
              var dirFiles = FileSystem.readDirectory(path);
              for(file in dirFiles)
              {
                 var filename = path + "/" + file;
                 var info = FileSystem.stat(filename);
                 if (inDetails)
                 {
                    // Last used, as trimming takes it
                    var time = lastUse(used, project, dir, file, info);
                    Sys.println('$filename : ${info.size} bytes, ${Date.fromTime(time)}');
                 }
                 count++;
                 size += info.size;
              }
              //files = files.concat(dirFiles);
           }
           projSize = Std.int( (size - projSize)/1024 );
           projCount = count - projCount;
           Sys.println('Project $project\t: ${projSize}k in $projCount files');
        }

        var k = Std.int(size/1024);
        var t = haxe.Timer.stamp()-t0;
        var projString = inProject==null ? "" : ' in project $inProject';
        Sys.println('Found: ${k}k in $count files$projString in $t seconds');
      }
      catch(error:Dynamic)
      {
         Log.warn("Error accessing cache: " + error);
      }
   }


}


