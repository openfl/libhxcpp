// cpp.encoding.Utf8.encode gave a null array for the empty string
// (openfl/libhxcpp#406). It is API level 500, which Haxe 4 and Haxe
// 5.0.0-preview.1 do not build against, so the checks are native code
// that passes there.
@:cppFileCode('
#if (HXCPP_API_LEVEL>=500)
static bool encodes(String inText, const unsigned char *inBytes, int inLength)
{
	Array<uint8_t> bytes = cpp::encoding::Utf8::encode(inText);
	if (!bytes.mPtr || bytes->length != inLength)
		return false;
	for (int i = 0; i < inLength; i++)
		if (bytes[i] != inBytes[i])
			return false;
	return true;
}
static bool emptyEncoding() { return encodes(HX_CSTRING(""), nullptr, 0); }
static bool asciiEncoding()
{
	static const unsigned char abc[] = { 0x61, 0x62, 0x63 };
	return encodes(HX_CSTRING("abc"), abc, 3);
}
static bool unicodeEncoding()
{
	static const unsigned char wide[] = { 0xC4, 0x80 };
	return encodes(String::fromCharCode(0x100), wide, 2);
}
#else
static bool emptyEncoding() { return true; }
static bool asciiEncoding() { return true; }
static bool unicodeEncoding() { return true; }
#endif
')
class Main {
	static function main() {
		Sys.println("empty UTF8 encoding returns an empty array: " + (untyped __cpp__("emptyEncoding()") : Bool));
		Sys.println("ASCII UTF8 encoding remains intact: " + (untyped __cpp__("asciiEncoding()") : Bool));
		Sys.println("Unicode UTF8 encoding remains intact: " + (untyped __cpp__("unicodeEncoding()") : Bool));
	}
}
