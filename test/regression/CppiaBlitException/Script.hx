class Script {
    static function typedBlit():Void {
        var dest = [7, 8];
        var source = [1, 2];
        var caught = false;
        cpp.NativeArray.blit(dest, 0, source, 0, 2);
        try {
            cpp.NativeArray.blit(dest, -1, source, 0, 1);
        } catch (e:Dynamic) {
            caught = true;
        }
        if (!caught || dest.join(",") != "1,2") throw "typed blit did not recover";
    }
    static function dynamicBlit():Void {
        var dest:Array<Dynamic> = [7, "eight"];
        var source:Array<Dynamic> = [1, "two"];
        var caught = false;
        try {
            cpp.NativeArray.blit(dest, 0, source, 1, 2);
        } catch (e:Dynamic) {
            caught = true;
        }
        if (!caught || dest.join(",") != "7,eight") throw "dynamic blit did not recover";
    }
    static function exactSize():Void {
        var values = [7, 8];
        var caught = false;
        try {
            cpp.NativeArray.setSize(values, -1);
        } catch (e:Dynamic) {
            caught = true;
        }
        if (!caught || values.join(",") != "7,8") throw "exact size did not recover";
    }
    static function resizeLimit():Void {
        var values = [7, 8];
        var caught = false;
        try {
            values.resize(0x40000000);
        } catch (e:Dynamic) {
            caught = true;
        }
        if (!caught || values.join(",") != "7,8") throw "resize limit did not recover";
    }
    static function main():Void {
        var args = Sys.args();
        var cases = args.length > 1 ? [args[1]] : ["typed-blit", "dynamic-blit", "exact-size", "resize-limit"];
        for (name in cases) {
            switch (name) {
                case "typed-blit": typedBlit();
                case "dynamic-blit": dynamicBlit();
                case "exact-size": exactSize();
                case "resize-limit": resizeLimit();
                default: throw "unknown case: " + name;
            }
            Sys.println(name + ": OK");
        }
    }
}
