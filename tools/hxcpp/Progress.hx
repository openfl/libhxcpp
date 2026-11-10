// How many of a group's files have compiled, shown before each file as it starts
class Progress {
   var finished:Int;
   var total:Int;
   var usePercent:Bool;

   public function new(inTotal:Int, usePercent:Bool = false) {
      finished = 0;
      total = inTotal;
      this.usePercent = usePercent;
   }

   public function label():String {
      if (usePercent) {
         var percent = finished / total;
         var pct = Std.int(percent * 1000) / 10;
         var str = Std.string(pct);
         if (Std.int(pct) == pct) {
            str += ".0";
         }
         return "\x1b[1m[" + str + "%]\x1b[0m";
      } else {
         var done = Std.string(finished);
         while (done.length < Std.string(total).length)
            done = " " + done;
         return "\x1b[1m[" + done + "/" + total + "]\x1b[0m";
      }
   }

   public function fileDone() {
      Log.lock();
      finished++;
      Log.unlock();
   }
}
