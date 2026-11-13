// String::utf8_str into a view checked for the bytes but not the terminator,
// and stopped a UTF-16 string at its first NUL (openfl/libhxcpp#408). It is
// API level 500, so the checks are native code that passes below it.
@:cppFileCode('
#if (HXCPP_API_LEVEL>=500)
#include <cpp/marshal/View.hpp>
static int shortUtf8Buffer(bool empty) {
	char buffer[4] = {63, 63, 63, 88};
	bool result = true;
	bool threw = false;
	try {
		String value = empty ? String::emptyString : HX_CSTRING("abc");
		result = value.utf8_str(cpp::marshal::View<char>(buffer, empty ? 0 : 3));
	} catch (...) { threw = true; }
	int flags = (!result && !threw) ? 1 : 0;
	if (buffer[0] == 63 && buffer[1] == 63 && buffer[2] == 63 && buffer[3] == 88) flags |= 2;
	if (threw) flags |= 4;
	return flags;
}
static bool utf8BufferMatches(String value, Array<unsigned char> expected) {
	char buffer[64];
	memset(buffer, 0x55, sizeof(buffer));
	int length = -1;
	bool result = value.utf8_str(cpp::marshal::View<char>(buffer, sizeof(buffer)), &length);
	return result && length == expected->length + 1
		&& memcmp(buffer, expected->GetBase(), expected->length) == 0
		&& buffer[expected->length] == 0;
}
#else
static int shortUtf8Buffer(bool) { return 3; }
static bool utf8BufferMatches(String, Array<unsigned char>) { return true; }
#endif
')
class Main {
	static function main() {
		var shortAscii = true;
		var empty = true;
		var embedded = true;
		var ordinary = true;
		shortAscii = untyped __cpp__("shortUtf8Buffer(false) == 3");
		empty = untyped __cpp__("shortUtf8Buffer(true) == 3");
		for (value in ["\u0100\x00tail", "\x00\u0100tail", "\u0100\x00"])
			if (!matches(value)) embedded = false;
		for (value in ["abc", "a\x00b", "\u0100tail", String.fromCharCode(0x1F600)])
			if (!matches(value)) ordinary = false;
		if (Sys.args().length > 0) {
			Sys.println("short buffer flags: " + untyped __cpp__("shortUtf8Buffer(false)"));
			Sys.println("empty buffer flags: " + untyped __cpp__("shortUtf8Buffer(true)"));
		}
		Sys.println("ASCII buffer without terminator space is rejected without writes: " + shortAscii);
		Sys.println("empty buffer without terminator space is rejected without writes: " + empty);
		Sys.println("UTF16 strings retain embedded NULs in UTF8 buffers: " + embedded);
		Sys.println("buffers with room preserve ordinary text: " + ordinary);
	}
	static function matches(value:String):Bool {
		var expected = haxe.io.Bytes.ofString(value).getData();
		return untyped __cpp__("utf8BufferMatches({0}, {1})", value, expected);
	}
}
