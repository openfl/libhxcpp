class Script {
    static var calls:Int;
    static var first:Array<Dynamic>;
    static var second:Array<Dynamic>;
    static function reset() {
        calls = 0;
        first = [1, "two", 3];
        second = [4, "five", 6];
    }
    static function receiver():Array<Dynamic> {
        calls++;
        return calls == 1 ? first : second;
    }
    static function main() {
        reset();
        receiver().resize(1);
        Sys.println('resize calls=$calls first=$first second=$second');
        #if cpp
        reset();
        cpp.NativeArray.setSize(receiver(), 1);
        Sys.println('setSize calls=$calls first=$first second=$second');
        #end
    }
}
