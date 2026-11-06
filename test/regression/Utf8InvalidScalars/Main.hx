// cpp.encoding.Utf8.encode wrote bytes for a surrogate or a value past
// U+10FFFF, which the UTF-16 encoder refuses (openfl/libhxcpp#407). It is API
// level 500, which Haxe 4 and Haxe 5.0.0-preview.1 do not build against, so
// the checks are native code that passes there. The result has a bit set for
// each check that failed.
@:cppFileCode('
#if (HXCPP_API_LEVEL>=500)
static int scalarFailures()
{
	using cpp::marshal::View;
	int failed = 0;
	const char32_t invalid[] = { 0xD800, 0xDFFF, 0x110000, (char32_t)-1 };
	for (char32_t point : invalid)
	{
		unsigned char data[4] = { 0x55, 0x55, 0x55, 0x55 };
		View<uint8_t> view(cpp::Pointer<uint8_t>(data), 4);
		bool refused = false;
		try { cpp::encoding::Utf8::encode(point, view); } catch (Dynamic &) { refused = true; }
		if (!refused)
			failed |= 1;
		for (unsigned char byte : data)
			if (byte != 0x55)
				failed |= 2;
		refused = false;
		try { cpp::encoding::Utf16::encode(point, view); } catch (Dynamic &) { refused = true; }
		if (!refused)
			failed |= 4;
	}
	const char32_t valid[] = { 0, 0x7F, 0x80, 0xD7FF, 0xE000, 0x10FFFF };
	for (char32_t point : valid)
	{
		unsigned char data[4];
		View<uint8_t> view(cpp::Pointer<uint8_t>(data), 4);
		int length = cpp::encoding::Utf8::encode(point, view);
		if (cpp::encoding::Utf8::codepoint(view.slice(0, length)) != point)
			failed |= 8;
	}
	return failed;
}
#else
static int scalarFailures() { return 0; }
#endif
')
class Main {
	static function main() {
		var failed:Int = untyped __cpp__("scalarFailures()");
		Sys.println("invalid scalars are rejected by UTF8 encoding: " + (failed & 1 == 0));
		Sys.println("invalid scalars leave the output buffer unchanged: " + (failed & 2 == 0));
		Sys.println("UTF16 encoding rejects the same invalid scalars: " + (failed & 4 == 0));
		Sys.println("valid boundary scalars round trip: " + (failed & 8 == 0));
	}
}
