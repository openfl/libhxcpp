package hxcpp.ssl;

#if !cpp
#error "DigestStream is available on the native cpp target"
#end

import haxe.io.Bytes;
import sys.ssl.DigestAlgorithm;

/**
	A digest that takes its input in pieces, so a large input, such as a file
	read a block at a time, can be hashed without holding all of it in memory.
	The digest of input given in any number of updates is the one
	`sys.ssl.Digest.make` gives for all of it at once.

	```haxe
	var stream = new DigestStream(SHA256);
	stream.update(block, 0, read);
	var digest = stream.finish();
	```

	A stream gives one digest: once finished, it throws on any further call.
	A stream that is never finished is freed by the collector. Misuse throws:
	an unknown algorithm, bytes that are null, and a range outside them.

	Builds that use it need `-lib hxcpp` or `-lib libhxcpp`. They link the ssl
	library, as `sys.ssl` does, whether or not they use any `sys.ssl` class.
**/
class DigestStream {
	var handle:Dynamic;

	/**
		Starts a digest with `alg`, named as `sys.ssl.Digest.make` names it.
		Throws if the algorithm is unknown.
	**/
	public function new(alg:DigestAlgorithm) {
		handle = Native.create(alg);
	}

	/**
		Adds `len` bytes of `data` from `pos` to the digest. A `len` of 0
		adds nothing. Throws if `data` is null, if `pos` or `len` is negative
		or the range passes the end of `data`, or if the stream is finished.
	**/
	public function update(data:Bytes, pos:Int, len:Int):Void {
		Native.update(handle, data == null ? null : data.getData(), pos, len);
	}

	/**
		Returns the digest of everything given to `update`, and frees the
		stream's native state. Throws if the stream is already finished.
	**/
	public function finish():Bytes {
		return Bytes.ofData(Native.finish(handle));
	}
}

// The include puts the ssl library into the link, as cpp.NativeSsl does for
// sys.ssl, so a program that uses no sys.ssl class still has these
@:include("hx/StdLibs.h")
@:buildXml('<include name="${HXCPP}/src/hx/libs/ssl/Build.xml"/>')
private extern class Native {
	@:native("_hx_ssl_dgst_new") static function create(alg:String):Dynamic;
	@:native("_hx_ssl_dgst_update") static function update(handle:Dynamic, data:haxe.io.BytesData, pos:Int, len:Int):Void;
	@:native("_hx_ssl_dgst_finish") static function finish(handle:Dynamic):haxe.io.BytesData;
}
