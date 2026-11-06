class Main {
    static function main():Void {
        var args = Sys.args();
        if (args.length == 0) {
            var failed = false;
            for (mode in ["interpreter", "jit"]) {
                Sys.println(mode + ":");
                for (name in ["typed-blit", "dynamic-blit", "exact-size", "resize-limit"]) {
                    var result = Sys.command(Sys.programPath(), [mode, name]);
                    if (result != 0) {
                        Sys.println(name + ": FAIL (child exit " + result + ")");
                        failed = true;
                    }
                }
            }
            if (failed) Sys.exit(1);
            return;
        }
        cpp.cppia.Host.enableJit(args[0] == "jit");
        var path = haxe.io.Path.join([haxe.io.Path.directory(Sys.programPath()), "script.cppia"]);
        try {
            cpp.cppia.Host.runFile(path);
        } catch (e:Dynamic) {
            Sys.println("FAIL: exception escaped the script: " + e);
            Sys.exit(1);
        }
    }
}
