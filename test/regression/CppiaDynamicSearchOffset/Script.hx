class Script {
    static var calls = 0;
    static function offset():Int { calls++; return 2; }
    static function fail():Int { throw "offset failure"; }
    static function main() {
        var typed = ["a", "b", "a", "b", "a"];
        Sys.println("typed=" + typed.indexOf("a", 2) + "," + typed.lastIndexOf("a", 2));
        var values:Array<Dynamic> = ["a", "b", "a", "b", "a"];
        Sys.println("default=" + values.indexOf("a") + "," + values.lastIndexOf("a"));
        Sys.println("offset=" + values.indexOf("a", 2) + "," + values.lastIndexOf("a", 2));
        Sys.println("negative=" + values.indexOf("a", -2) + "," + values.lastIndexOf("a", -2));
        Sys.println("outside=" + values.indexOf("a", 99) + "," + values.lastIndexOf("a", 99));
        Sys.println("effect=" + values.indexOf("a", offset()) + " calls=" + calls);
        try {
            values.lastIndexOf("a", fail());
            Sys.println("offset exception missed");
        } catch (e:Dynamic) {
            Sys.println("caught=" + e);
        }
    }
}
