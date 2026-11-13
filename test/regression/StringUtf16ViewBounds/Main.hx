// String::create from a UTF-16 view completed a high surrogate at the end of
// the view with the unit past it (openfl/libhxcpp#412). It is API level 500,
// so the checks are native code that passes below it.
@:cppFileCode('
#if (HXCPP_API_LEVEL>=500)
#include <cpp/marshal/View.hpp>
static bool boundedUtf16Prefix() {
	char16_t data[3] = {0xD83D, 0xDE00, 0};
	try {
		String value = String::create(cpp::marshal::View<char16_t>(data, 1));
		return value.length <= 1;
	} catch (...) { return true; }
}
static bool completeUtf16Pair() {
	char16_t data[3] = {0xD83D, 0xDE00, 0};
	String value = String::create(cpp::marshal::View<char16_t>(data, 2));
	// fromCharCode gives the string either way, smart strings or not
	return value == String::fromCharCode(0x1F600);
}
static bool terminatedUtf16View() {
	char16_t data[4] = {0x0100, 0, 65, 0};
	String value = String::create(cpp::marshal::View<char16_t>(data, 3));
	return value == String::fromCharCode(0x0100);
}
#else
static bool boundedUtf16Prefix() { return true; }
static bool completeUtf16Pair() { return true; }
static bool terminatedUtf16View() { return true; }
#endif
')
class Main {
	static function main() {
		var bounded = true;
		var pair = true;
		var terminated = true;
		bounded = untyped __cpp__("boundedUtf16Prefix()");
		pair = untyped __cpp__("completeUtf16Pair()");
		terminated = untyped __cpp__("terminatedUtf16View()");
		Sys.println("UTF16 construction stays inside the supplied view: " + bounded);
		Sys.println("a complete surrogate pair is preserved: " + pair);
		Sys.println("a NUL terminator ends native string construction: " + terminated);
	}
}
