import utest.Test;
import utest.Assert;

class TestSearch extends Test
{
   static var starts:Array<Int> = [-100, -9, -3, -1, 0, 1, 2, 4, 5, 8, 9, 100, 5000];

   // What indexOf and lastIndexOf on an array give, by a plain scan
   static function arrayIndexOf(a:Array<Int>, v:Int, from:Int):Int
   {
      if (from < 0)
         from = from + a.length < 0 ? 0 : from + a.length;
      for(i in from...a.length)
         if (a[i]==v)
            return i;
      return -1;
   }

   static function arrayLastIndexOf(a:Array<Int>, v:Int, from:Int):Int
   {
      var i = from >= a.length ? a.length - 1 : from < 0 ? from + a.length : from;
      while(i >= 0)
      {
         if (a[i]==v)
            return i;
         i--;
      }
      return -1;
   }

   static function stringIndexOf(text:String, needle:String, from:Int):Int
   {
      if (from < 0)
         from = 0;
      if (needle.length==0)
         return from > text.length ? text.length : from;
      var i = from;
      while(i + needle.length <= text.length)
      {
         if (text.substr(i, needle.length)==needle)
            return i;
         i++;
      }
      return -1;
   }

   static function stringLastIndexOf(text:String, needle:String, from:Int):Int
   {
      if (needle.length==0)
         return from < 0 ? 0 : from > text.length ? text.length : from;
      var i = from + needle.length > text.length ? text.length - needle.length : from;
      if (i < 0)
         i = 0;
      while(i >= 0)
      {
         if (text.substr(i, needle.length)==needle)
            return i;
         i--;
      }
      return -1;
   }

   // Int starts are taken without boxing them; typed, boxed, omitted, null,
   // reflected and Dynamic field starts must all give the same result. A
   // field read converts to both an Int and a Dynamic, so it needs its own
   // overload in C++ or the call does not compile
   function checkArray(a:Array<Int>, v:Int, ?pos:haxe.PosInfos)
   {
      Assert.equals(arrayIndexOf(a, v, 0), a.indexOf(v), pos);
      Assert.equals(arrayLastIndexOf(a, v, a.length - 1), a.lastIndexOf(v), pos);
      Assert.equals(a.indexOf(v), a.indexOf(v, null), pos);
      Assert.equals(a.lastIndexOf(v), a.lastIndexOf(v, null), pos);
      for(s in starts)
      {
         var boxed:Dynamic = s;
         var indexOf = Reflect.field(a, "indexOf");
         var lastIndexOf = Reflect.field(a, "lastIndexOf");
         Assert.equals(arrayIndexOf(a, v, s), a.indexOf(v, s), pos);
         Assert.equals(arrayLastIndexOf(a, v, s), a.lastIndexOf(v, s), pos);
         Assert.equals(a.indexOf(v, s), a.indexOf(v, boxed), pos);
         Assert.equals(a.lastIndexOf(v, s), a.lastIndexOf(v, boxed), pos);
         Assert.equals(a.indexOf(v, s), Reflect.callMethod(a, indexOf, [v, s]), pos);
         Assert.equals(a.lastIndexOf(v, s), Reflect.callMethod(a, lastIndexOf, [v, s]), pos);
         var holder:Dynamic = { start: s };
         Assert.equals(a.indexOf(v, s), a.indexOf(v, holder.start), pos);
         Assert.equals(a.lastIndexOf(v, s), a.lastIndexOf(v, holder.start), pos);
      }
   }

   function checkString(text:String, needle:String, ?pos:haxe.PosInfos)
   {
      Assert.equals(stringIndexOf(text, needle, 0), text.indexOf(needle), pos);
      Assert.equals(stringLastIndexOf(text, needle, text.length), text.lastIndexOf(needle), pos);
      Assert.equals(text.indexOf(needle), text.indexOf(needle, null), pos);
      Assert.equals(text.lastIndexOf(needle), text.lastIndexOf(needle, null), pos);
      var dyn:Dynamic = text;
      for(s in starts)
      {
         var boxed:Dynamic = s;
         Assert.equals(stringIndexOf(text, needle, s), text.indexOf(needle, s), pos);
         Assert.equals(stringLastIndexOf(text, needle, s), text.lastIndexOf(needle, s), pos);
         Assert.equals(text.indexOf(needle, s), text.indexOf(needle, boxed), pos);
         Assert.equals(text.lastIndexOf(needle, s), text.lastIndexOf(needle, boxed), pos);
         Assert.equals(text.indexOf(needle, s), dyn.indexOf(needle, s), pos);
         Assert.equals(text.lastIndexOf(needle, s), dyn.lastIndexOf(needle, s), pos);
         var holder:Dynamic = { start: s };
         Assert.equals(text.indexOf(needle, s), text.indexOf(needle, holder.start), pos);
         Assert.equals(text.lastIndexOf(needle, s), text.lastIndexOf(needle, holder.start), pos);
      }
   }

   public function testArrayStarts()
   {
      var ints = [3, 1, 4, 1, 5, 9, 2, 6, 5];
      checkArray(ints, 1);
      checkArray(ints, 5);
      checkArray(ints, 7);
      var big = [for(i in 0...10000) i % 100];
      var ok = true;
      for(i in 0...10000)
         ok = ok && big.indexOf(i % 100, i)==i && big.lastIndexOf(i % 100, i)==i;
      Assert.isTrue(ok);
   }

   public function testOtherArrayStarts()
   {
      var floats = [0.5, 1.5, 0.5, 2.5];
      var start = 1;
      Assert.equals(2, floats.indexOf(0.5, start));
      Assert.equals(0, floats.lastIndexOf(0.5, start));
      Assert.equals(-1, floats.indexOf(0.5, -1));
      var words = ["a", "b", "a", "c"];
      Assert.equals(2, words.indexOf("a", start));
      Assert.equals(2, words.lastIndexOf("a", start + 1));
   }

   public function testStringStarts()
   {
      checkString("abcabcab", "ab");
      checkString("abcabcab", "c");
      checkString("abcabcab", "");
      checkString("αβγαβγ", "βγ");
   }

   // Long needles whose start keeps matching the text switch to a search
   // that stays linear in the text; split, indexOf and lastIndexOf must give
   // what a plain scan gives, in byte, wide and mixed encodings
   static function repeat(count:Int, unit:String):String
   {
      var b = new StringBuf();
      for(i in 0...count)
         b.add(unit);
      return b.toString();
   }

   static function plainSplit(text:String, delimiter:String):Array<String>
   {
      var parts = [];
      var last = 0;
      var at = stringIndexOf(text, delimiter, 0);
      while(at >= 0)
      {
         parts.push(text.substring(last, at));
         last = at + delimiter.length;
         at = stringIndexOf(text, delimiter, last);
      }
      parts.push(text.substr(last));
      return parts;
   }

   function checkLong(text:String, needle:String, expectedParts:Int, ?pos:haxe.PosInfos)
   {
      var parts = text.split(needle);
      Assert.equals(expectedParts, parts.length, pos);
      Assert.same(plainSplit(text, needle), parts, pos);
      var at = [-5, 0, 1, needle.length, text.length >> 2, text.length >> 1, text.length - needle.length - 1,
                text.length - needle.length, text.length, text.length + 10];
      for(s in at)
      {
         Assert.equals(stringIndexOf(text, needle, s), text.indexOf(needle, s), pos);
         if (s >= 0)
            Assert.equals(stringLastIndexOf(text, needle, s), text.lastIndexOf(needle, s), pos);
      }
      Assert.equals(stringLastIndexOf(text, needle, text.length), text.lastIndexOf(needle), pos);
   }

   public function testLongByteNeedles()
   {
      var a = repeat(20000, "a");
      var near = repeat(500, "a") + "b";
      checkLong(a, near, 1);
      checkLong(a + near + "x" + a + near + near + a, near, 4);
      checkLong(near + a + near, near, 3);
      checkLong(near + near + "y" + near, near, 4);
      var periodic = repeat(100, "ab") + "c";
      checkLong(repeat(5000, "ab") + periodic + repeat(5000, "ab") + periodic, periodic, 3);
      var nulNeedle = repeat(100, "a\x00") + "b";
      var nul = repeat(2000, "a\x00");
      checkLong(nul + nulNeedle + nul, nulNeedle, 2);
   }

   public function testLongWideNeedles()
   {
      var wide = repeat(20000, "α");
      var wideNear = repeat(500, "α") + "β";
      checkLong(wide, wideNear, 1);
      checkLong(wide + wideNear + "x" + wide + wideNear, wideNear, 3);
   }

   public function testLongMixedNeedles()
   {
      var near = repeat(500, "a") + "b";
      var mixedText = repeat(20000, "a") + "é";
      checkLong(mixedText, near, 1);
      checkLong(mixedText + near + mixedText + near, near, 3);
      checkLong(repeat(20000, "a"), repeat(500, "a") + "é", 1);
      var asciiFromWide = (repeat(500, "a") + "é").substr(0, 500) + "b";
      checkLong(repeat(20000, "a") + near + repeat(20000, "a"), asciiFromWide, 2);
   }

   public function testLastIndexOfStart()
   {
      var near = repeat(500, "a") + "b";
      var found = "start" + near + "middle" + near + "end";
      var first = 5;
      var last = first + near.length + 6;
      Assert.equals(last, found.lastIndexOf(near));
      Assert.equals(first, found.lastIndexOf(near, last - 1));
      Assert.same(["a", "b", "", "c", ""], "a,b,,c,".split(","));
      Assert.same(["x", "y", "", "z"], "x--y----z".split("--"));
   }
}
