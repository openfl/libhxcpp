// The SSL library includes <winsock2.h> before <hxcpp.h>, so on Windows the
// min and max macros were defined when Array.h's incremental scan called
// std::min, and the build stopped there.
class Main {
	static function main() {
		cpp.NativeSsl.ssl_debug_set(0);
		Sys.println("built with the SSL library");
	}
}
