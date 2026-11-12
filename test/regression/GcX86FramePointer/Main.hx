// With frame pointers omitted, a 32-bit compiler can keep the only reference
// to values in EBP across a collection. CaptureX86 must save EBP with the other
// callee-saved registers. The warm-up affects allocation and register layout.
@:cppFileCode('
extern "C" void *hx_cffi(const char *);
static ::String viewText(Dynamic value) {
    typedef const char *(*View)(hx::Object *);
    static View view = (View)hx_cffi("val_string");
    return ::String::create(view(value.mPtr));
}
')
class Main {
    static function text(n:Int):String {
        var b = new StringBuf();
        for (i in 0...n) b.addChar(0x3bb);
        b.add("end");
        return b.toString();
    }
    static function view(v:Dynamic):String return untyped __cpp__('viewText({0})', v);
    static function main() {
        view(text(4096));
        for (round in 0...20) {
            var values:Array<Dynamic> = [];
            for (i in 0...64) {
                var boxed:Dynamic = text(i + 1);
                view(boxed);
                values.push(boxed);
            }
            for (i in 0...3) cpp.vm.Gc.run(true);
            cpp.vm.Gc.compact();
            for (i in 0...64) {
                var actual = view(values[i]);
                if (actual != text(i + 1)) throw 'round $round view $i got "$actual"';
            }
        }
        Sys.println("views survived");
    }
}
