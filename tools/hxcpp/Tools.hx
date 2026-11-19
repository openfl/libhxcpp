class Tools {
  private static var onExit:Array<Int->Void> = [];

  public static function addOnExitHook(fn:Int->Void) {
    onExit.push(fn);
  }

  public static function exit(exitCode:Int) {
    // On a compile thread the hooks waited for this thread itself, since one
    //  waits for every compile thread to finish, so the build hung. The job
    //  ends here instead, and the main thread exits once the jobs have
    //  (libhxcpp #471)
    if (ThreadPool.onWorker()) {
      if (BuildTool.threadExitCode == 0)
        BuildTool.threadExitCode = exitCode == 0 ? -1 : exitCode;
      throw new ThreadPool.WorkerExit();
    }
    for (hook in onExit) {
      hook(exitCode);
    }
    Sys.exit(exitCode);
  }
}
