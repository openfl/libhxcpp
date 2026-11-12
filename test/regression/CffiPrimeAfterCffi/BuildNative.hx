#if macro
import haxe.macro.Context;
import haxe.macro.Expr;
class BuildNative {
 public static function run():Array<Field> {
  var args=["run","hxcpp","Build.xml"];
  for(define in ["HXCPP_M32","HXCPP_M64","HXCPP_ARM64","HXCPP_DEBUG"]) {
   if(Context.defined(define)) args.push("-D"+define);
  }
  var original=Sys.getCwd();
  Sys.setCwd("native");
  var status=Sys.command("haxelib",args);
  Sys.setCwd(original);
  if(status!=0) Context.error("Native fixture build failed",Context.currentPos());
  return Context.getBuildFields();
 }
}
#end