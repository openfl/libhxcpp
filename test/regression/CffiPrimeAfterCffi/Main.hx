// The ndll includes CFFI.h before CFFIPrime.h
@:build(BuildNative.run())
class Main {
 static function main() {
  cpp.Lib.pushDllSearchPath(haxe.io.Path.join([haxe.io.Path.directory(Sys.programPath()),"../native/ndll",cpp.Lib.getBinDirectory()]));
  var fallback=cpp.Lib.load("afterprime","afterEcho",1);
  var fast=cpp.Prime.load("afterprime","afterEcho","ss",false);
  for(text in ["plain","café","\u{1f600}"]) {
   var ordinary:String=fallback(text);
   var direct:String=fast(text);
   Sys.println("direct Prime preserves the string: "+(direct==text));
   Sys.println("CFFI fallback preserves the same string: "+(ordinary==text));
  }
  var makeFallback=cpp.Lib.load("afterprime","afterMakeText",0);
  var made:String=makeFallback();
  Sys.println("CFFI fallback preserves a native Unicode result: "+(made=="日本語"));
 }
}
