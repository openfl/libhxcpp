// How many of a group's files have compiled, shown before each file as it
//  starts: a count, or with HXCPP_PROGRESS_PERCENT a percentage
class Progress
{
   var finished:Int;
   var total:Int;
   var percent:Bool;

   public function new(inTotal:Int, inPercent = false)
   {
      finished = 0;
      total = inTotal;
      percent = inPercent;
   }

   public function label():String
   {
      if (percent)
      {
         // Pad to the width of 100.0% so the file names line up
         var tenths = total > 0 ? Std.int(finished * 1000 / total) : 1000;
         var done = Std.int(tenths / 10) + "." + (tenths % 10) + "%";
         while (done.length < 6)
            done = done + " ";
         return "\x1b[1m[" + done + "]\x1b[0m";
      }
      var done = Std.string(finished);
      while (done.length < Std.string(total).length)
         done = " " + done;
      return "\x1b[1m[" + done + "/" + total + "]\x1b[0m";
   }

   public function fileDone()
   {
      Log.lock();
      finished++;
      Log.unlock();
   }
}
