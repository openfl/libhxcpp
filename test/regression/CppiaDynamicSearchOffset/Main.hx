class Main {
    static function main():Void {
        var script = haxe.io.Path.join([haxe.io.Path.directory(Sys.programPath()), "script.cppia"]);
        for (mode in ["interpreter", "jit"]) {
            Sys.println(mode + ":");
            cpp.cppia.Host.enableJit(mode == "jit");
            cpp.cppia.Host.runFile(script);
        }
    }
}
