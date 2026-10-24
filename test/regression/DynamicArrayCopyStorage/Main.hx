class Main {
 static function check(operation:String):Bool {
  var typed:Array<Int>=[1000,2000,3000];
  var source:Array<Dynamic>=typed;
  var result:Array<Dynamic>=switch(operation) {
   case "copy": source.copy();
   case "slice": source.slice(0,2);
   case "splice": source.splice(0,2);
   case "filter": source.filter(value -> value!=3000);
   case "map": source.map(value -> value);
   default: throw "operation";
  };
  var size=result.length;
  var initial=result[0]==1000 && result[size-1]==(size==3 ? 3000 : 2000);
  result.push("later");
  result.push(null);
  result.push(1.5);
  var wide=haxe.Int64.make(0x200000,1);
  result.push(wide);
  var exact=false;
  var last:Dynamic=result[size+3];
  try exact=haxe.Int64.isInt64(last) && haxe.Int64.compare(cast last,wide)==0 catch(_:Dynamic) {}
  var sourceCorrect=operation=="splice" ? typed.length==1 && typed[0]==3000 : typed.length==3 && typed[0]==1000 && typed[2]==3000;
  return initial && sourceCorrect && result.length==size+4 && result[size]=="later" && result[size+1]==null && result[size+2]==1.5 && exact;
 }
 static function main() {
  for(operation in ["copy","slice","splice","filter","map"])
   Sys.println(operation+" result accepts heterogeneous values: "+check(operation));
 }
}