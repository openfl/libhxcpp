class Script {
    static var sizeCalls = 0;
    static function size():Int { sizeCalls++; return 1; }
    static function main() {
        var typed = [1, 2, 3];
        typed.reverse();
        typed.resize(1);
        Sys.println("typed=" + typed.join(","));
        var values:Array<Dynamic> = [1, "two", 3];
        values.reverse();
        Sys.println("reverse=" + values.join(","));
        values.resize(size());
        Sys.println("shrink=" + values.join(",") + " sizeCalls=" + sizeCalls);
        values.resize(3);
        Sys.println("grow=" + values.join(",") + " length=" + values.length);
        values.resize(0);
        Sys.println("empty=" + values.length);
    }
}
