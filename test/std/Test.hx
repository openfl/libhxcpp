import haxe.zip.Compress;
import haxe.zip.Uncompress;
import haxe.zip.FlushMode;
import sys.FileSystem;
import sys.db.Connection;
import sys.db.Sqlite;
import sys.db.Mysql;
import sys.FileSystem;
import haxe.io.Bytes;
import haxe.io.BytesData;

import cpp.Random;
import cpp.net.Poll;
import sys.io.File;
import sys.io.FileSeek;
import sys.net.Host;
import sys.net.Socket;
import sys.net.UdpSocket;
import sys.io.Process;

#if haxe4
import sys.thread.Deque;
import sys.thread.Thread;
#else
import cpp.vm.Deque;
import cpp.vm.Thread;
#end

using cpp.NativeArray;
using cpp.AtomicInt;

import utest.Assert;

@:buildXml('<include name="${HXCPP}/src/hx/libs/ssl/Build.xml"/>')
extern class SslTest
{
   @:native("_hx_ssl_init")
   extern public static function socket_init():Void;
}

extern class SqliteNative
{
   @:native("_hx_sqlite_get_autocommit")
   extern public static function autocommit(handle:Dynamic):Bool;
}

extern class MysqlNative
{
   @:native("_hx_mysql_server_status")
   extern public static function serverStatus(handle:Dynamic):Int;
}

// A server listening on loopback with a client connected to it, for the poll
// tests. The connection is left pending unless the test accepts it.
class LoopbackSockets
{
   public var server(default,null):Socket;
   public var client(default,null):Socket;
   var peer:Socket;

   public function new()
   {
      server = new Socket();
      server.bind(new Host("127.0.0.1"), 0);
      server.listen(1);

      client = new Socket();
      client.connect(new Host("127.0.0.1"), server.host().port);
   }

   public function accept()
   {
      peer = server.accept();
   }

   public function close()
   {
      if (peer != null)
         peer.close();
      client.close();
      server.close();
   }
}

class Test extends utest.Test
{
   var x:Int;

   public function new()
   {
      super();
      
      x = 1;
   }

   public static function log(t:String)
   {
      Sys.println(t);
   }

   public static function v(t:String)
   {
      Sys.println("  " + t);
   }

   public static function thisFile()
   {
      #if nme_install_tool
      var bytes:haxe.io.Bytes = ApplicationMain.getAsset("Test.hx");
      #else
      var bytes = sys.io.File.getBytes("Test.hx");
      #end
      return bytes;
   }

   function implTestDb(cnx:Connection) : Int
   {
      v("connected :" + cnx);
      if (cnx.dbName() == "SQLite") {
        cnx.request("
          CREATE TABLE IF NOT EXISTS UserPwd (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              name TEXT,
              age INTEGER,
              money DOUBLE,
              password BLOB
          )");
         cnx.request("SELECT '豪鬼' test");
      } else {
        cnx.request("
          CREATE TABLE IF NOT EXISTS UserPwd (
              id INTEGER NOT NULL AUTO_INCREMENT,
              name TEXT,
              age INTEGER,
              money DOUBLE,
              password BLOB,
              PRIMARY KEY(id)
          )");
      }
      var dels = cnx.request("DELETE FROM UserPwd");
      Assert.equals(0, dels.nfields, "Bad DELETE'd result");
      v("deleted " + dels.length + " existing rows");

      cnx.request("INSERT INTO UserPwd (name,age,money,password) VALUES ('John',32,100.45,X'c0ffee')");
      cnx.request("INSERT INTO UserPwd (name,age,money,password) VALUES ('Bob',14,4.50,X'deadbeef01020304')");

      var rset = cnx.request("SELECT * FROM UserPwd");

      var length = rset.length;
      v("found "+length+" users");
      Assert.equals(2, length, "Bad user count");
      for( row in rset )
      {
         var pass:Dynamic = row.password;
         var password = Std.isOfType(pass, haxe.io.BytesData) ? haxe.io.Bytes.ofData(pass) : pass;
         var md5 = haxe.crypto.Md5.make(password).toHex().substr(0,8);
         v("  user "+row.name+" is "+row.age+" years old,  password:" + md5);
         Assert.isFalse(md5!="5f80e231" && md5!="8ed0b363", "Bad binary blob store");
      }
      return 0;
   }

   function testDate()
   {
      log("Test date");
      var now = Date.now();
      v(Std.string(now));
      var then = Date.fromString("1977-06-11");
      v(Std.string(then));

      Assert.isFalse(now.getTime()<=then.getTime(), "Date fromString - time travel");

      var later = DateTools.makeUtc(1996,5,4,17,55,11);
      v(Std.string(later));

      var diff:Float = untyped __global__.__hxcpp_timezone_offset(now.mSeconds);
      v("timezone offet:" + diff);

      final early = Date.fromTime(-1000);
      Assert.notNull(early.toString());
      Assert.equals(1969, early.getUTCFullYear());
      Assert.equals(11, early.getUTCMonth());
      Assert.equals(31, early.getUTCDate());
      Assert.equals(23, early.getUTCHours());
      Assert.equals(59, early.getUTCSeconds());

      final local1960 = new Date(1960, 0, 15, 12, 30, 0);
      Assert.equals("1960-01-15 12:30:00", local1960.toString());
      Assert.equals(1960, local1960.getFullYear());
      Assert.equals(12, local1960.getHours());

      Assert.equals("2040-06-01 00:00:00", new Date(2040, 5, 1, 0, 0, 0).toString());

      Assert.equals(0.0, DateTools.makeUtc(1970, 0, 1, 0, 0, 0));
      Assert.equals(-315619200000.0, DateTools.makeUtc(1960, 0, 1, 0, 0, 0));
      Assert.equals(1960, Date.fromTime(DateTools.makeUtc(1960, 0, 1, 0, 0, 0)).getUTCFullYear());
   }

   function testCompress()
   {
      log("Test compress");
      final bytes = thisFile();

      final compress = new Compress(9);
      compress.setFlushMode(FlushMode.FINISH);
      final buffer = haxe.io.Bytes.alloc(bytes.length * 2 + 100);
      final r = compress.execute(bytes,0,buffer,0);
      compress.close();
      final compressed = buffer.sub(0,r.write);
      v("compressed size " + compressed.length );

      v("try closing too many times...");
      compress.close();

      var decompressed = Uncompress.run(compressed);
      v("decompressed size:" + decompressed.length + "/" + bytes.length);
      Assert.equals(0, decompressed.compare(bytes), "Compress/Uncompress mismatch");
   }


   function testRegexp()
   {
      log("Test Regexp/BytesInput");

      // This utf8 comment is parsed by the code ...
      //  su©©ess
      var success = false;
      var utf8Match = ~/su©+ess/u;

      // This comment is parsed by the code ...
      //XML<xml value="Hello World!"/>
      var match = ~/^..XML/;
      var input = new haxe.io.BytesInput(thisFile());
      try {
        while(true)
        {
           final line = input.readLine();
           if (match.match(line))
           {
              final xml = Xml.parse(line.substr(5));
              v("found xml:" + xml.firstElement().get("value"));
              Assert.equals("Hello World!", xml.firstElement().get("value"));
           }
           if (utf8Match.match(line))
           {
              v("found utf8:" + utf8Match.matched(0));
              success = true;
           }
        }
      } catch (e:Dynamic) { }

      Assert.isTrue(success, "Could not find success in utf8 code");
   }

   function testRegexpMixedUnicode() {
      var success = true;

      // when matching a utf8 subject string against a utf16 pattern and vice versa
      for (pattern => subject in ["[A-Za-zÀ-ÖØ-öø-ÿ0-9]+" => "a", "[A-Z]+" => "ÀÖA"]) {
         Assert.isTrue(new EReg(pattern, "").match(subject), '"$subject" does not match against ~/$pattern/');
      }
   }

   function testRegexpLongSubject() {
      // With -D HXCPP_PCRE_JIT this pattern exhausts the JIT's stack on a long
      // subject, so the match has to be run again by the interpreter
      var comment = new StringBuf();
      comment.add("/*");
      for (i in 0...100000)
         comment.add("x");
      var unterminated = ~/\/\*(((?!\*\/).)*)$/s;
      Assert.isTrue(unterminated.match(comment.toString()));
      // and a utf16 subject
      Assert.isTrue(unterminated.match(comment.toString() + "é"));
   }

   function testRegexpLoneSurrogate() {
      // A lone surrogate matches as U+FFFD would, and match positions and
      // matched text refer to the original string
      var lone = String.fromCharCode(0xD800);
      var subject = "ab" + lone + "cdé";
      var cd = ~/c(d)/;
      Assert.isTrue(cd.match(subject));
      Assert.equals(3, cd.matchedPos().pos);
      Assert.equals("d", cd.matched(1));
      var any = ~/b(.)c/;
      Assert.isTrue(any.match(subject));
      Assert.equals(lone, any.matched(1));
   }

   function testSqlite()
   {
      log("Test sqlite");
      final dbFile = "hxcpp.db";
      final cnx    = Sqlite.open(dbFile);

      Assert.equals(0, implTestDb(cnx));

      cnx.close();
   }

   static function integerText(value:Dynamic):String
   {
      return switch (Type.typeof(value))
      {
         case TInt: "Int " + value;
         case TClass(c) if (Type.getClassName(c) == "cpp::Int64"): "Int64 " + value;
         // Haxe 5 gives Int64s a ValueType of their own, which Haxe 4 does not have
         case other if (Std.string(other) == "TInt64"): "Int64 " + value;
         case other: Std.string(other) + " " + value;
      }
   }

   function testSqliteValues()
   {
      log("Test sqlite values");
      final cnx = Sqlite.open(":memory:");
      cnx.request("CREATE TABLE vals (i INTEGER, f REAL, s TEXT, b BLOB, n INTEGER)");

      for (i in ["2147483647", "-2147483648", "2147483648", "-2147483649", "1727600000000", "9007199254740993", "9223372036854775807"])
         cnx.request('INSERT INTO vals (i) VALUES ($i)');
      final ints = [for (row in cnx.request("SELECT i FROM vals ORDER BY rowid")) integerText(row.i)];
      Assert.same(["Int 2147483647", "Int -2147483648", "Int64 2147483648", "Int64 -2147483649",
         "Int64 1727600000000", "Int64 9007199254740993", "Int64 9223372036854775807"], ints);

      cnx.request("DELETE FROM vals");
      cnx.request("INSERT INTO vals VALUES (1, 0.1, " + cnx.quote("it's 豪鬼") + ", X'00ff10', NULL)");
      final row = cnx.request("SELECT * FROM vals").next();
      Assert.equals(0.1, row.f);
      Assert.equals("it's 豪鬼", row.s);
      Assert.equals("00ff10", Bytes.ofData(row.b).toHex());
      Assert.isTrue(Reflect.hasField(row, "n"));
      Assert.isNull(row.n);
      cnx.close();
   }

   // The kind of value a row's fixed field holds: a number or text is held
   // in the field itself, not in a box
   static function fixedFieldKind(row:Dynamic, name:String):String
   {
      final count = Reflect.fields(row).length;
      for (k in 0...count)
      {
         final key:String = untyped __cpp__("static_cast<hx::Anon_obj*>({0}.mPtr)->getFixed()[{1}].key", row, k);
         if (key != name)
            continue;
         final type:Int = untyped __cpp__("(int)static_cast<hx::Anon_obj*>({0}.mPtr)->getFixed()[{1}].value.type", row, k);
         if (type == untyped __cpp__("(int)cpp::Variant::typeObject")) return "object";
         if (type == untyped __cpp__("(int)cpp::Variant::typeString")) return "string";
         if (type == untyped __cpp__("(int)cpp::Variant::typeDouble")) return "double";
         if (type == untyped __cpp__("(int)cpp::Variant::typeInt")) return "int";
         if (type == untyped __cpp__("(int)cpp::Variant::typeInt64")) return "int64";
         if (type == untyped __cpp__("(int)cpp::Variant::typeBool")) return "bool";
         return "other";
      }
      return "missing";
   }

   function testSqliteInlineValues()
   {
      log("Test sqlite values held in the row");
      final cnx = Sqlite.open(":memory:");
      cnx.request("CREATE TABLE t (i INTEGER, f REAL, b BOOL, s TEXT, big INTEGER, n INTEGER, blob BLOB)");
      cnx.request("INSERT INTO t VALUES (123456, 2.5, 1, 'héllo wörld', 1727600000000, NULL, x'00ff10')");
      cnx.request("INSERT INTO t VALUES (-7, -0.125, 0, '', -9007199254740993, NULL, x'')");
      final rows = [for (row in cnx.request("SELECT * FROM t ORDER BY i DESC")) row];
      cnx.close();
      cpp.vm.Gc.run(true);

      final a:Dynamic = rows[0];
      final b:Dynamic = rows[1];
      Assert.equals(123456, a.i);
      Assert.equals(2.5, a.f);
      Assert.equals(true, a.b);
      Assert.equals("héllo wörld", a.s);
      Assert.equals(11, (a.s : String).length);
      final big:haxe.Int64 = a.big;
      Assert.isTrue(big == haxe.Int64.make(402, 1023147008));
      Assert.isNull(a.n);
      Assert.isTrue(Reflect.hasField(a, "n"));
      Assert.equals("00ff10", Bytes.ofData(a.blob).toHex());
      Assert.equals(-7, b.i);
      Assert.equals(-0.125, b.f);
      Assert.equals(false, b.b);
      Assert.equals("", b.s);
      final negative:haxe.Int64 = b.big;
      Assert.isTrue(negative == haxe.Int64.make(-2097153, -1));
      Assert.equals(0, Bytes.ofData(b.blob).length);
      Assert.isTrue(Std.isOfType(a.i, Int) && Std.isOfType(a.f, Float) && Std.isOfType(a.b, Bool) && Std.isOfType(a.s, String));
      Assert.equals(123456, Reflect.field(a, "i"));
      Assert.equals(7, Reflect.fields(a).length);

      Assert.equals("int", fixedFieldKind(b, "i"));
      Assert.equals("double", fixedFieldKind(b, "f"));
      Assert.equals("bool", fixedFieldKind(b, "b"));
      Assert.equals("string", fixedFieldKind(b, "s"));
      Assert.equals("int64", fixedFieldKind(b, "big"));
      Assert.equals("object", fixedFieldKind(b, "n"));
      Assert.equals("object", fixedFieldKind(b, "blob"));

      a.i = 99;
      Reflect.setField(a, "s", "changed");
      a.extra = true;
      cpp.vm.Gc.run(true);
      Assert.equals(99, a.i);
      Assert.equals("changed", a.s);
      Assert.equals(true, a.extra);
   }

   function testSqliteFailedStatement()
   {
      log("Test sqlite failed statement");
      final cnx = Sqlite.open(":memory:");
      cnx.request("CREATE TABLE users (email TEXT UNIQUE)");
      cnx.request("INSERT INTO users VALUES ('a@b.c')");
      Assert.equals(1, cnx.lastInsertId());

      var error = "";
      try
         cnx.request("INSERT INTO users VALUES ('a@b.c')")
      catch (e:Dynamic)
         error = Std.string(e);
      Assert.isTrue(error.indexOf("UNIQUE constraint failed: users.email") >= 0, error);

      Assert.equals(1, cnx.request("SELECT COUNT(*) AS c FROM users").next().c);
      cnx.close();
   }

   function testSqliteTransaction()
   {
      log("Test sqlite transaction");
      final cnx = Sqlite.open(":memory:");
      final handle = Reflect.field(cnx, "c");
      cnx.request("CREATE TABLE t (v INTEGER)");
      Assert.isTrue(SqliteNative.autocommit(handle));

      cnx.startTransaction();
      Assert.isFalse(SqliteNative.autocommit(handle));
      cnx.request("INSERT INTO t VALUES (1)");
      cnx.rollback();
      Assert.isTrue(SqliteNative.autocommit(handle));
      Assert.equals(0, cnx.request("SELECT COUNT(*) AS c FROM t").next().c);

      cnx.request("BEGIN");
      Assert.isFalse(SqliteNative.autocommit(handle));
      cnx.request("INSERT INTO t VALUES (2)");
      cnx.request("COMMIT");
      Assert.isTrue(SqliteNative.autocommit(handle));
      Assert.equals(1, cnx.request("SELECT COUNT(*) AS c FROM t").next().c);
      cnx.close();
   }

   static function connectMysql():Connection
   {
      try
      {
         return Mysql.connect({
            host : "localhost",
            port : 3306,
            user : "hxcpp",
            pass : "hxcpp",
            socket : null,
            database : "hxcpp"
         });
      }
      catch(e:Dynamic)
      {
         if (Sys.getEnv("HXCPP_TEST_MYSQL") == "1")
            Assert.fail("No MySQL server: " + e);
         else
            v("mysql got error - ok because no server" );
      }
      return null;
   }

   function testMysql()
   {
      log("Test mysql");
      final cnx = connectMysql();
      if (cnx!=null)
      {
        Assert.equals(0, implTestDb(cnx));

        cnx.close();
      }
      else
      {
         Assert.pass();
      }
   }

   function testMysqlValues()
   {
      log("Test mysql values");
      final cnx = connectMysql();
      if (cnx == null)
      {
         Assert.pass();
         return;
      }
      cnx.request("DROP TABLE IF EXISTS Vals");
      cnx.request("CREATE TABLE Vals (big_v BIGINT, ubig_v BIGINT UNSIGNED, uint_v INT UNSIGNED, exact_v DECIMAL(20,4),
         day_v DATE, moment_v DATETIME(3), late_v DATE, null_v INT, bin_v VARCHAR(8) COLLATE utf8mb4_bin)");
      cnx.request("INSERT INTO Vals VALUES (9007199254740993, 18446744073709551615, 4294967295, 12345678901234.5678,
         '1960-01-15', '2040-06-01 12:34:56.789', '2040-06-01', NULL, 'abc')");
      final row = cnx.request("SELECT * FROM Vals").next();

      Assert.equals("Int64 9007199254740993", integerText(row.big_v));
      Assert.equals("18446744073709551615", row.ubig_v);
      Assert.equals("Int64 4294967295", integerText(row.uint_v));
      Assert.equals("12345678901234.5678", row.exact_v);

      final day:Date = row.day_v;
      Assert.equals(DateTools.makeUtc(1960, 0, 15, 0, 0, 0), day.getTime());
      final moment:Date = row.moment_v;
      Assert.equals(DateTools.makeUtc(2040, 5, 1, 12, 34, 56) + 789, Math.fround(moment.getTime()));
      final late:Date = row.late_v;
      Assert.equals(DateTools.makeUtc(2040, 5, 1, 0, 0, 0), late.getTime());

      Assert.isTrue(Reflect.hasField(row, "null_v"));
      Assert.isNull(row.null_v);
      Assert.equals("abc", row.bin_v);

      final count = cnx.request("SELECT COUNT(*) FROM Vals").next();
      Assert.equals(1, Reflect.field(count, "COUNT(*)"));

      // A name used twice keeps its later column, as it did when rows were
      // built a field at a time
      final twice:Dynamic = cnx.request("SELECT 1 AS a, 2 AS b, 3 AS a").next();
      Assert.equals(3, twice.a);
      Assert.equals(2, Reflect.fields(twice).length);
      row.added = 4;
      Reflect.deleteField(row, "bin_v");
      Assert.equals(4, row.added);
      Assert.isFalse(Reflect.hasField(row, "bin_v"));
      Assert.equals(9, Reflect.fields(row).length);

      cnx.request("DROP TABLE Vals");
      cnx.close();
   }

   function testMysqlSession()
   {
      log("Test mysql session");
      final cnx = connectMysql();
      if (cnx == null)
      {
         Assert.pass();
         return;
      }
      final handle = Reflect.field(cnx, "__c");

      final text = "it's a \\ backslash";
      Assert.equals(text, cnx.request("SELECT " + cnx.quote(text) + " AS v").next().v);
      cnx.request("SET SESSION sql_mode = 'NO_BACKSLASH_ESCAPES'");
      Assert.isTrue((MysqlNative.serverStatus(handle) & 512) != 0);
      Assert.equals(text, cnx.request("SELECT " + cnx.quote(text) + " AS v").next().v);

      cnx.request("DROP TABLE IF EXISTS Txn");
      cnx.request("CREATE TABLE Txn (v INT) ENGINE=InnoDB");
      cnx.startTransaction();
      Assert.isTrue((MysqlNative.serverStatus(handle) & 1) != 0);
      cnx.request("INSERT INTO Txn VALUES (1)");
      cnx.rollback();
      Assert.isTrue((MysqlNative.serverStatus(handle) & 1) == 0);
      Assert.equals(0, cnx.request("SELECT COUNT(*) AS c FROM Txn").next().c);

      cnx.request("DROP TABLE Txn");
      cnx.close();
   }

   function testRandom()
   {
      log("Test Random");

      final rand = new Random();
      final f0 = rand.float();
      final f1 = rand.float();

      v('samples $f0,$f1');
      Assert.notEquals(f0, f1, "Not random enough");

      rand.setSeed(1);
      final i0 = rand.int(256);
      rand.setSeed(2);
      final i1 = rand.int(256);

      Assert.equals( 91, i0, "Non-repeatable random seed");
      Assert.equals(217, i1, "Non-repeatable random seed");

      var tries = 0;
      while(rand.int(1000)!=999)
         tries++;

      Assert.equals(749, tries, "Non-repeatable random iterations");
   }

   public static function tryFunc( func ) : Bool
   {
      try
      {
         func();
         return true;
      }
      catch(e:Dynamic) { }
      return false;
   }

   function testFile()
   {
      log("Test File");
      final filename = "testfile.txt";
      tryFunc( function() FileSystem.deleteFile(filename) );
      tryFunc( function() FileSystem.deleteFile(filename+"-copy") );
      tryFunc( function() FileSystem.deleteFile(filename+".bin") );
      tryFunc( function() FileSystem.deleteFile(filename+".out") );

      final contents = "line1\nline2\n";
      final bytes    = Bytes.ofString(contents);

      v("compare...");
      File.saveContent(filename,contents);
      Assert.equals(contents, File.getContent(filename));

      v("copy...");
      File.copy(filename,filename+"-copy");
      Assert.equals(0, File.getBytes(filename+"-copy").compare(bytes), "copy getBytes mismatch");
      
      v("file in...");
      File.saveBytes(filename+".bin",bytes);
      final fileIn = File.read(filename+".bin");
      Assert.equals(contents.charCodeAt(0), fileIn.readByte(), "File readByte mismatch");
      final buffer = Bytes.alloc(5);
      buffer.set(0,'-'.code);
      buffer.set(4,'+'.code);
      Assert.equals(3, fileIn.readBytes(buffer,1,3), "Could not read 3 bytes");
      Assert.equals("-ine+", buffer.toString(), "Bad sub-buffer readBytes");

      v("seek...");
      Assert.equals(4, fileIn.tell());
      fileIn.seek(4, SeekCur);
      Assert.equals(8, fileIn.tell());
      fileIn.seek(7, SeekBegin );
      Assert.equals(7, fileIn.tell());

      final rest = Bytes.alloc( contents.length - fileIn.tell() );
      fileIn.readBytes(rest,0,rest.length);
      Assert.isFalse(fileIn.eof(), "File at end, but not eof");
      fileIn.seek(-contents.length, SeekEnd);
      Assert.equals(0, fileIn.tell(), "File seek from end to beginning failed");
      fileIn.close();

      v("write...");
      final fileOut = File.write(filename+".out");

      fileOut.writeByte('W'.code);
      Assert.equals(3, fileOut.writeBytes(buffer,1,3), "Could not write 3 bytes");
      Assert.equals(4, fileOut.tell(), "Bad tell on file write");

      fileOut.flush();
      Assert.equals("Wine", File.getContent(filename+".out"), "Bad reading after flush");

      fileOut.seek(1,SeekBegin);
      fileOut.writeByte('a'.code);
      fileOut.close();

      final contents =  File.getContent(filename+".out");
      v("have :" + contents);
      Assert.equals("Wane", contents, "Bad readback after seek");

      v("cleanup...");
      FileSystem.deleteFile(filename);
      FileSystem.deleteFile(filename + "-copy");
      FileSystem.deleteFile(filename + ".bin");
      FileSystem.deleteFile(filename + ".out");
   }

   function testLocalhost()
   {
      log("Test Host");
      
      final localhost = Host.localhost();
      
      v('localhost :$localhost');

      Assert.notNull(localhost);
      Assert.notEquals(0, localhost.length);
   }

   function testHost()
   {
      log("Test Host");
      
      v('host :${ new Host("github.com") }');

      Assert.pass();
   }

   function testFileSystem()
   {
      log("Test FileSystem");
      tryFunc( function() FileSystem.deleteFile("dir/file.txt") );
      tryFunc( function() FileSystem.deleteFile("dir/txt.file") );
      tryFunc( function() FileSystem.deleteFile("dir/child") );
      tryFunc( function() FileSystem.deleteDirectory("dir") );

      v("create dir");
      FileSystem.createDirectory("dir");
      FileSystem.createDirectory("dir/child");
      File.saveContent("dir/file.txt","hello");
      
      final stat = FileSystem.stat("dir/file.txt");
      v(Std.string(stat));

      Assert.equals(5, stat.size, "File does not contain 5 bytes");
      Assert.equals(19, Std.string(stat.ctime).length, "File ctime does not appear to be a date");

      v("exists");
      Assert.isTrue(FileSystem.exists("dir"));
      Assert.isTrue(FileSystem.exists("dir/file.txt"));

      final files = FileSystem.readDirectory("dir");
      v("dir contents:" + files);

      Assert.equals(2, files.length);
      Assert.contains("file.txt", files);
      Assert.contains("child", files);

      Assert.raises(() -> FileSystem.deleteDirectory("dir/junk"));
      Assert.raises(() -> FileSystem.deleteFile("dir/junk"));
      Assert.raises(() -> FileSystem.deleteFile("dir/child"));
      Assert.raises(() -> FileSystem.deleteDirectory("dir/file.txt"));

      final fullPath = FileSystem.fullPath("dir/child");
      v('fullPath: $fullPath');
      final fullPath = FileSystem.fullPath("dir/file.txt");
      v('fullPath: $fullPath');

      v("isDirectory...");
      Assert.isFalse(FileSystem.isDirectory("dir/file.txt"), "file appears to be a directory");
      Assert.isTrue(FileSystem.isDirectory("dir/child"), "directory appears to not be a directory");
      Assert.isFalse(FileSystem.isDirectory("dir/junk"), "junk appears to be a directory");
      Assert.isFalse(FileSystem.isDirectory("dir/file.txt"), "file appears to be a directory");

      v("rename...");

      Assert.raises(() -> FileSystem.rename("dir/a", "dir/b"), String, "No error renaming missing file");

      FileSystem.rename("dir/file.txt","dir/txt.file");
      
      Assert.isFalse(FileSystem.exists("dir/file.txt"));
      Assert.isTrue(FileSystem.exists("dir/txt.file"));

      v("cleanup..");
      FileSystem.deleteFile("dir/txt.file");
      FileSystem.deleteDirectory("dir/child");
      FileSystem.deleteDirectory("dir");

      // Assert.equals(0, FileSystem.readDirectory(".").indexOf("dir"), "Directory removed, but sill there?");
      if (FileSystem.readDirectory(".").indexOf("dir")>=0) {
         Assert.fail("Directory removed, but sill there?:" + FileSystem.readDirectory("."));
      }
   }

   function testSys()
   {
      log("Test Sys");

      {
         final key   = "myKey";
         final value = "123";

         Sys.putEnv(key, value);

         Assert.equals(value, Sys.getEnv(key));
         Assert.equals(value, Sys.environment().get(key));
      }

      {
         v("little sleep...");

         final t0 = Sys.time();
         Sys.sleep(0.1);
         final t1 = Sys.time();

         v("Slept for: " + (t1-t0));

         Assert.isFalse(t1<=t0 || (t1-t0)>10, "Too sleepy");
      }
      
      v("CpuTime: " + Sys.cpuTime());
      v("Cwd: " + Sys.getCwd());
      v("Program Path: " + Sys.programPath());
      v("Ignore getChar auto test");
      v("Args: " + Sys.args());
      v("SystemName: " + Sys.systemName());
      v("Skipping  Sys.setTimeLocale" + Sys.setTimeLocale);
   }

   function testCommand()
   {
      log("Test Command");

      final expected = 13;
      
      Assert.equals(expected, Sys.command( Sys.programPath(), ["exit", Std.string(expected)]));
   }

   function testPoll()
   {
      log("Test poll");
      
      var poll = new Poll(4);
      poll.prepare([],[]);
      var t0 = Sys.time();
      v("poll...");
      poll.poll([],0.1);
      var t = Sys.time()-t0;
      v('took ${t}s');
      /*
      if (t<0.1)
         return error("Timeout too soon");
      if (t>10)
         return error("Timeout too slow");
      */

      Assert.pass();
   }


   // Readiness is not required to be visible on the first pass, only to become
   // visible at all, so poll a few times before giving up.
   static function pollUntilReady(poll:Poll, read:Array<Socket>, write:Array<Socket>)
   {
      for(attempt in 0...20)
      {
         poll.prepare(read, write);
         poll.events(0.05);
         if (poll.readIndexes[0] == 0 || poll.writeIndexes[0] == 0)
            return;
         Sys.sleep(0.01);
      }
   }

   // A listening socket with a connection waiting to be accepted is readable.
   // _hx_std_socket_poll_events used to be able to return without reporting it,
   // leaving the caller to conclude nothing had arrived.
   function testPollReportsReadableSocket()
   {
      log("Test poll reports a readable socket");

      var sockets = new LoopbackSockets();
      var poll = new Poll(4);
      pollUntilReady(poll, [sockets.server], []);

      v('readIndexes[0] = ${poll.readIndexes[0]}');
      Assert.equals(0, poll.readIndexes[0], "Listening socket with a pending connection was not reported readable");

      sockets.close();
   }

   // The ready lists are the caller's only result, so they have to describe the
   // poll that just ran and not the one before it. An early return that left
   // them untouched reported the previous call's sockets as ready again.
   function testPollClearsIndexesBetweenCalls()
   {
      log("Test poll clears stale indexes");

      var sockets = new LoopbackSockets();
      var poll = new Poll(4);
      pollUntilReady(poll, [sockets.server], []);
      Assert.equals(0, poll.readIndexes[0], "Setup failed: socket never became readable");

      // Nothing is registered now, so nothing can be ready.
      poll.prepare([], []);
      poll.events(0.01);

      v('after empty poll: readIndexes[0] = ${poll.readIndexes[0]}, writeIndexes[0] = ${poll.writeIndexes[0]}');
      Assert.equals(-1, poll.readIndexes[0], "Read index survived a poll with no sockets registered");
      Assert.equals(-1, poll.writeIndexes[0], "Write index survived a poll with no sockets registered");

      sockets.close();
   }

   // A connected socket with room in its send buffer is writable, and the
   // write list is filled by the same pass that fills the read list.
   function testPollReportsWritableSocket()
   {
      log("Test poll reports a writable socket");

      var sockets = new LoopbackSockets();
      sockets.accept();
      var poll = new Poll(4);
      pollUntilReady(poll, [], [sockets.client]);

      v('writeIndexes[0] = ${poll.writeIndexes[0]}');
      Assert.equals(0, poll.writeIndexes[0], "Connected socket was not reported writable");

      sockets.close();
   }

   // Readiness has to mean readiness. When select() failed the error path
   // marked every registered read socket ready, so an idle connection looked
   // readable and a caller woke up to read nothing -- for as long as it stayed
   // idle.
   function testPollDoesNotReportIdleSocketAsReadable()
   {
      log("Test poll does not invent readability");

      var sockets = new LoopbackSockets();
      sockets.accept();

      // Nothing has been sent either way, so neither end has anything to read.
      var poll = new Poll(4);
      poll.prepare([sockets.client], []);
      poll.events(0.05);

      v('readIndexes[0] = ${poll.readIndexes[0]}');
      Assert.equals(-1, poll.readIndexes[0], "Idle socket with no pending data was reported readable");

      sockets.close();
   }

   function testUdpSocket()
   {
      log("Test UdpSocket");
      
      var udp = new UdpSocket();

      udp.close();

      Assert.pass();
   }

   // A Lock, not a shared Bool, so the main thread sees the client finish
   static var socketClientDone:sys.thread.Lock;
   public static function readOutput(proc:Process)
   {
      var done = socketClientDone = new sys.thread.Lock();
      Thread.create( function() {
         while(true)
         {
            try
            {
               var line = proc.stdout.readLine();
               v("---> " + line);
            }
            catch(e:Dynamic)
            {
               break;
            }
         }
         var code = proc.exitCode();
         v("process exit code :" + code);
         done.release();
      });
   }

   function testSocket()
   {
      log("Test Socket");
      v("Spawn client..");
      var proc = new Process( Sys.programPath(), ["socket"] );
      readOutput(proc);
      v("connect localhost..");
      var host = new Host("localhost");
      var socket = new Socket();
      socket.bind(host,0xcccc);
      v("listen...");
      socket.listen(1);
      v("accept...");
      var connected = socket.accept();
      v("connected!");

      var input = connected.input;
      var output = connected.output;

      v("send...");
      connected.setFastSend(true);
      connected.write("ping");
      var buffer = Bytes.alloc(8);

      Assert.equals(8, input.readBytes(buffer,0,buffer.length));
      Assert.equals("pingpong", buffer.toString());

      v("close connection..");
      connected.close();
      v("close original..");
      socket.close();

      v("wait for client...");
      Assert.isTrue(socketClientDone.wait(1.0), "Socket client did not finish");
   }

   public static function socketClient()
   {
      log("Client proc...");
      var host = new Host("localhost");
      var socket:Socket = null;
      for(attempt in 0...10)
      {
         try
         {
            log("Client connect...");
            socket = new Socket();
            socket.connect(host,0xcccc);
            break;
         }
         catch(e:Dynamic)
         {
            socket.close();
            socket = null;
            log("Client failed :" + e);
         }
         log("sleep...");
         Sys.sleep(0.25);
      }

      if (socket==null)
         Sys.exit(-1);
      v("connected client");

      var input = socket.input;
      var output = socket.output;

      var buffer = Bytes.alloc(4);
      input.readBytes(buffer,0,4);
      v("client got " + buffer);
      output.writeBytes( Bytes.ofString( buffer.toString() + "pong" ), 0, 8);
      output.flush();

      v("bye");
      socket.shutdown(true,true);
   }

   // A whole stream read from a socket, sent by another thread that closes
   // its end when done
   static function readWholeStream(payload:Bytes):Bytes
   {
      final server = new Socket();
      server.bind(new Host("127.0.0.1"), 0);
      server.listen(1);
      final port = server.host().port;
      final sent = new sys.thread.Lock();
      Thread.create(() -> {
         final client = new Socket();
         client.connect(new Host("127.0.0.1"), port);
         client.output.writeFullBytes(payload, 0, payload.length);
         client.close();
         sent.release();
      });
      final conn = server.accept();
      final received:BytesData = cpp.NativeSocket.socket_read(@:privateAccess conn.__s);
      sent.wait();
      conn.close();
      server.close();
      return Bytes.ofData(received);
   }

   // Reading a whole stream appends each received chunk with the growth push
   // gets; every byte must arrive, for an empty stream and for many chunks
   function testSocketReadWhole()
   {
      log("Test reading a whole stream from a socket");
      for (size in [0, 1, 16383, 16385, 1 << 20, (3 << 20) + 7])
      {
         final payload = Bytes.alloc(size);
         for (i in 0...size)
            payload.set(i, (i * 131 + (i >> 9)) & 0xff);
         final received = readWholeStream(payload);
         Assert.equals(size, received.length);
         Assert.equals(0, received.compare(payload));
      }
   }

   function testSsl()
   {
      log("Test ssl");

      SslTest.socket_init();

      Assert.pass();
   }

   function testSerialization()
   {
      log("Test serialization");

      var orig:haxe.Int64 = haxe.Int64.make(0xdeadbeef,0xbeefdead);
      var recon:haxe.Int64 = haxe.Unserializer.run(haxe.Serializer.run(orig));

      Assert.equals(orig, recon);
   }

   // Hide from optimizer
   static function getOne() return 1;

   function testThread()
   {
      log("Test thread");
      v("atomics..");

      var a:AtomicInt = getOne();
      var aPtr = cpp.Pointer.addressOf(a);
      Assert.isFalse(aPtr.exchangeIf(2,3));
      Assert.isTrue(aPtr.exchangeIf(1,3));
      Assert.equals(3, a);
      Assert.equals(3, aPtr.atomicInc());
      Assert.equals(4, a);
      Assert.equals(4, aPtr.atomicDec());
      Assert.equals(3, a);

      a = getOne();
      v('deque a=$a');

      var q = new Deque<Int>();
      q.add(1);
      q.add(2);
      q.add(3);
      var mainThread = Thread.current();
      // +100000
      Thread.create(function() {
         q.pop(true);
         for(i in 0...100000)
            aPtr.atomicInc();
         mainThread.sendMessage(null);
      });
      // +100000
      Thread.create(function() {
         q.pop(true);
         for(i in 0...100000)
            aPtr.atomicInc();
         mainThread.sendMessage(null);
      } );
      // -200000
      Thread.create(function() {
         q.pop(true);
         for(i in 0...200000)
            aPtr.atomicDec();
         mainThread.sendMessage(null);
      } );

      v("wait for reply..");
      for(i in 0...3)
      {
         Thread.readMessage(true);
         v('got $i');
      }

      Assert.equals(1, a);
   }

   function testFloatReads()
   {
      log("Test float bytes");

      final value = 46;
      final bytes = haxe.io.Bytes.alloc(1+4+8);
      bytes.fill(0,1,value);

      // Test unaligned read/write
      bytes.setFloat(1,1.25);
      bytes.setDouble(5,1.25);

      Assert.equals(value, bytes.get(0), "Bad byte 0");
      Assert.equals(bytes.getDouble(5), bytes.getFloat(1), "Bad byte read/write");
   }

   public dynamic function getX() return x;

   function testDynamicMember()
   {
      log("Test dynamic member");

      Assert.equals(1, getX(), "Bad dynamic member function");
   }


   @:noDebug
   function testNoDebug()
   {
      log("Test noDebug");

      // Just testing to see it it compiles...
      Assert.notNull(new haxe.io.BytesBuffer(), "bad alloc");
   }

   @:noDebug
   function testNoDebugNoAlloc()
   {
      Assert.pass("Test noDebug, no alloc");
   }

   function testIntParsing()
   {
      log("Test int parsing");

      Assert.equals( 1, Std.parseInt('0x1'));
      Assert.equals( 1, Std.parseInt(' 0x1'));
      Assert.equals( 1, Std.parseInt('\t0x1'));
      Assert.equals( 0, Std.parseInt('   0x'));
      Assert.equals( 0, Std.parseInt('   0xyz'));
      Assert.equals(-1, Std.parseInt('-0x1'));
      Assert.equals(-1, Std.parseInt(' -0x1'));
      Assert.equals(-1, Std.parseInt('\t-0x1'));
      Assert.equals( 0, Std.parseInt('   -0x'));
      Assert.equals( 0, Std.parseInt('   -0xyz'));
      Assert.equals( 5, Std.parseInt('  5'));
      Assert.equals( 5, Std.parseInt(' \t\n5'));
   }

   // parseInt and parseFloat read only the ASCII start of a UTF-16 string,
   // so a non-ASCII suffix must not change what the ASCII part alone gives
   function testParseWideSuffix()
   {
      log("Test number parsing with a non-ASCII suffix");
      final inputs = [
         "123", " 123", "\t\n-42", "+7", "0x1F", "-0x10", "0X7fffffff", "12.5", "-12.5e-3", "1e5", ".5", "-.5", "5.", "Infinity",
         "-Infinity", "+Infinity", "NaN", "nan", "inf", "", " ", "-", "+", "x12", "2147483648", "-2147483649", "99999999999999999999",
         "1e400", "0.1e-400", "007", "1_000", "12abc", "0x", "0xg", "  0x1A", "3.14159265358979323846", "1.7976931348623157e308"
      ];
      final suffixes = ["α", " Zürich", " ", "１２", "\u0000", "é123", "日本"];
      for (input in inputs)
      {
         final int = Std.parseInt(input);
         final float = Std.parseFloat(input);
         for (suffix in suffixes)
         {
            Assert.equals(int, Std.parseInt(input + suffix), '"$input" with a suffix');
            final wide = Std.parseFloat(input + suffix);
            if (Math.isNaN(float))
               Assert.isTrue(Math.isNaN(wide), '"$input" with a suffix');
            else
               Assert.equals(float, wide, '"$input" with a suffix');
         }
      }

      final buffer = new StringBuf();
      for (i in 0...4096)
         buffer.add("α");
      final suffix = buffer.toString();
      Assert.equals(123, Std.parseInt("123 " + suffix));
      Assert.equals(1.5, Std.parseFloat("1.5 " + suffix));
      Assert.isNull(Std.parseInt(suffix + "123"));
      Assert.isTrue(Math.isNaN(Std.parseFloat(suffix + "1.5")));
      Assert.isNull(Std.parseInt(" " + "5"));
      Assert.isTrue(Math.isNaN(Std.parseFloat("　" + "5")));
      Assert.isNull(Std.parseInt("１２３"));
      Assert.isTrue(Math.isNaN(Std.parseFloat("１.５")));
   }

   public static function main()
   {
      switch Sys.args() {
         case [ 'exit', code ]:
            Sys.exit(Std.parseInt(code));
         case [ 'socket' ]:
            socketClient();
         case _:
            utest.UTest.run([ new Test() ]);
      }
   }
}
