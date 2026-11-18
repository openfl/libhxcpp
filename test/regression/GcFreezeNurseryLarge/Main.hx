class Holder {
	public var text:String;
	public var wide:String;

	public function new(t:String, w:String) {
		text = t;
		wide = w;
	}
}

// With HXCPP_GC_NURSERY, ObjectSizeSafe took a large allocation with a flag
// in its header, as a long string has for its hash slot or its UTF-16 data,
// for a small one, and gave its size as 0: _hx_gc_freeze copied none of it,
// and the frozen string read past its copy.
class Main {
	static function freeze<T>(value:T):T
		return untyped __cpp__("_hx_gc_freeze({0})", value);

	static function size(s:String):Int
		return untyped __cpp__("(int)::hx::ObjectSizeSafe((void *){0}.raw_ptr())", s);

	static function main() {
		var buf = new StringBuf();
		for (i in 0...5000)
			buf.add(String.fromCharCode(97 + i % 26));
		var text = buf.toString();
		var wide = text + "é";
		var frozen = freeze(new Holder(text, wide));
		Sys.println('size covers the string: ${size(text) >= 5000}');
		Sys.println('frozen copy equal: ${frozen.text == text}');
		Sys.println('frozen UTF-16 copy equal: ${frozen.wide == wide}');
	}
}
