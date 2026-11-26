import haxe.io.Bytes;
import hxcpp.ssl.DigestStream;
import sys.ssl.Digest;
import sys.ssl.DigestAlgorithm;

// The natives behind DigestStream, called directly as a scriptable host or
// other native code can, to pass them what the class never does
@:include("hx/StdLibs.h")
extern class DigestNative {
	@:native("_hx_ssl_dgst_new") static function create(alg:String):Dynamic;
	@:native("_hx_ssl_dgst_update") static function update(handle:Dynamic, data:haxe.io.BytesData, pos:Int, len:Int):Void;
	@:native("_hx_ssl_dgst_finish") static function finish(handle:Dynamic):haxe.io.BytesData;
}

// Each misuse throws a Haxe error the program can catch, and leaves the
// stream as it was: none crashes, and the program goes on to finish
function throws(name:String, f:() -> Void) {
	var thrown = false;
	try {
		f();
	} catch (e:Dynamic) {
		thrown = true;
		Sys.stderr().writeString('$name: $e\n');
	}
	Sys.println('$name throws: $thrown');
}

function main() {
	final data = Bytes.ofString("0123456789abcdef");
	final empty = Bytes.alloc(0);

	throws("an unknown algorithm", () -> new DigestStream(cast "SHA3"));
	throws("an empty algorithm name", () -> new DigestStream(cast ""));
	throws("a null algorithm", () -> new DigestStream(null));

	final stream = new DigestStream(DigestAlgorithm.SHA256);
	stream.update(data, 0, 4);
	throws("a negative pos", () -> stream.update(data, -1, 2));
	throws("a negative len", () -> stream.update(data, 4, -1));
	throws("a pos past the end", () -> stream.update(data, data.length + 1, 0));
	throws("a len past the end", () -> stream.update(data, 4, data.length - 3));
	throws("a pos and len past the end", () -> stream.update(data, data.length, 1));
	throws("a pos and len whose sum overflows", () -> stream.update(data, 4, 0x7fffffff));
	throws("a large pos and len whose sum overflows", () -> stream.update(data, 0x7fffffff, 0x7fffffff));
	throws("a minimum pos", () -> stream.update(data, -2147483647 - 1, 1));
	throws("a pos into empty bytes", () -> stream.update(empty, 1, 0));
	throws("null bytes", () -> stream.update(null, 0, 0));
	// None of the failed updates was hashed
	stream.update(data, 4, data.length - 4);
	stream.update(data, data.length, 0);
	final first = stream.finish();
	Sys.println("a stream that saw misuse still digests what it was given: " + (first.compare(Digest.make(data, DigestAlgorithm.SHA256)) == 0));

	throws("an update after finish", () -> stream.update(data, 0, 1));
	throws("a zero-length update after finish", () -> stream.update(data, 0, 0));
	throws("a finish after finish", () -> stream.finish());

	// Handles of the wrong kind, or none, passed to the natives
	final handle = DigestNative.create("SHA1");
	DigestNative.update(handle, data.getData(), 0, data.length);
	throws("a null handle to update", () -> DigestNative.update(null, data.getData(), 0, 1));
	throws("a null handle to finish", () -> DigestNative.finish(null));
	throws("a String as the handle", () -> DigestNative.update("SHA1", data.getData(), 0, 1));
	throws("an object as the handle", () -> DigestNative.finish({s: handle}));
	throws("an Array as the handle", () -> DigestNative.finish(data.getData()));
	cpp.NativeSsl.init();
	final conf = cpp.NativeSsl.conf_new(false);
	throws("a TLS configuration handle", () -> DigestNative.update(conf, data.getData(), 0, 1));
	cpp.NativeSsl.conf_close(conf);
	throws("an unknown algorithm to the native", () -> DigestNative.create("NOPE"));
	throws("a null algorithm to the native", () -> DigestNative.create(null));
	final sha1 = Bytes.ofData(DigestNative.finish(handle));
	Sys.println("the native handle digests what it was given: " + (sha1.compare(Digest.make(data, DigestAlgorithm.SHA1)) == 0));
	throws("a native handle already freed by finish, to update", () -> DigestNative.update(handle, data.getData(), 0, 1));
	throws("a native handle already freed by finish, to finish", () -> DigestNative.finish(handle));

	// Other streams are not disturbed by any of this
	final other = new DigestStream(DigestAlgorithm.MD5);
	other.update(data, 0, data.length);
	Sys.println("a new stream digests: " + (other.finish().compare(Digest.make(data, DigestAlgorithm.MD5)) == 0));
	Sys.println("done");
}

