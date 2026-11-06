import haxe.io.Bytes;
import haxe.io.BytesData;
import haxe.zip.Compress;
import haxe.zip.Uncompress;

@:include("hx/StdLibs.h")
extern class NativeZip {
	@:native("_hx_zip_get_adler32")
	static function getAdler32(handle:Dynamic):Int;
	@:native("_hx_zip_update_adler32")
	static function updateAdler32(adler:Int, data:BytesData, pos:Int, len:Int):Int;
	@:native("_hx_zip_update_crc32")
	static function updateCrc32(crc:Int, data:BytesData, pos:Int, len:Int):Int;
}

// "hello world, hello dictionary" deflated with the preset dictionary
//  "hello world", whose adler32, 0x1a0b045d, follows the two header bytes
final dictionaryStream = "78f91a0b045dcb40307514209c94cce492ccfcbcc4a24a00a2e60b13";
final dictionaryId = 0x1a0b045d;

// The native stream a Compress or Uncompress works on: Haxe 4's std and Haxe
//  5.0.0-preview.1's keep the handle in `s`, later Haxe 5 keeps an hx::zip
//  object in `impl`. Both report haxe_ver 5, so the field is found at run time.
function nativeStream(o:Dynamic):Dynamic {
	var impl = Reflect.field(o, "impl");
	return impl != null ? impl : Reflect.field(o, "s");
}

inline function deflater(c:Compress):Dynamic
	return nativeStream(c);

inline function inflater(u:Uncompress):Dynamic
	return nativeStream(u);

function claim(name:String, value:Bool)
	Sys.println(name + ": " + value);

function throws(f:() -> Void):Bool {
	try {
		f();
	} catch (e:Dynamic) {
		return true;
	}
	return false;
}

function presetDictionary() {
	final src = Bytes.ofHex(dictionaryStream);
	final dst = Bytes.alloc(64);
	final u = new Uncompress();
	var r = null;
	try {
		r = u.execute(src, 0, dst, 0);
	} catch (e:Dynamic) {
		Sys.stderr().writeString('execute: $e\n');
	}
	claim("a stream asking for a preset dictionary reports what it read", r != null && !r.done && r.read == 6 && r.write == 0);
	claim("its adler32 is the dictionary's id", NativeZip.getAdler32(inflater(u)) == dictionaryId);
	claim("calling it again, which can make no progress, throws", throws(() -> u.execute(src, 6, dst, 0)));
	u.close();
	claim("Uncompress.run on it throws", throws(() -> Uncompress.run(src)));
	final plain = Compress.run(Bytes.ofString("hello world, hello dictionary"), 9);
	claim("a stream without a dictionary still inflates", Uncompress.run(plain).toString() == "hello world, hello dictionary");
}

function checksums() {
	final digits = Bytes.ofString("123456789");
	claim("crc32 of 123456789", NativeZip.updateCrc32(0, digits.getData(), 0, 9) == 0xcbf43926);
	claim("adler32 of 123456789", NativeZip.updateAdler32(1, digits.getData(), 0, 9) == 0x091e01de);

	final fox = Bytes.ofString("The quick brown fox jumps over the lazy dog");
	final data = fox.getData();
	var crc = NativeZip.updateCrc32(0, data, 0, 10);
	crc = NativeZip.updateCrc32(crc, data, 10, fox.length - 10);
	var adler = NativeZip.updateAdler32(1, data, 0, 10);
	adler = NativeZip.updateAdler32(adler, data, 10, fox.length - 10);
	claim("checksums continue across pieces", crc == 0x414fa339 && adler == 0x5bdc0fda);
	claim("checksums of a range", NativeZip.updateCrc32(0, data, 4, 5) == haxe.crypto.Crc32.make(fox.sub(4, 5))
		&& NativeZip.updateAdler32(1, data, 4, 5) == haxe.crypto.Adler32.make(fox.sub(4, 5)));

	var matched = true;
	for (n in [1, 3, 100, 5552, 5553, 65536]) {
		final b = Bytes.alloc(n);
		for (i in 0...n)
			b.set(i, (i * 131 + n) & 0xff);
		if (NativeZip.updateCrc32(0, b.getData(), 0, n) != haxe.crypto.Crc32.make(b)
			|| NativeZip.updateAdler32(1, b.getData(), 0, n) != haxe.crypto.Adler32.make(b))
			matched = false;
	}
	claim("checksums match haxe.crypto", matched);

	final empty = Bytes.alloc(0);
	claim("nothing leaves a checksum as it was", NativeZip.updateCrc32(0x414fa339, empty.getData(), 0, 0) == 0x414fa339
		&& NativeZip.updateAdler32(0x5bdc0fda, empty.getData(), 0, 0) == 0x5bdc0fda
		&& NativeZip.updateCrc32(0x414fa339, data, 7, 0) == 0x414fa339
		&& NativeZip.updateAdler32(0x5bdc0fda, data, fox.length, 0) == 0x5bdc0fda);
	claim("a range outside the bytes is refused", throws(() -> NativeZip.updateCrc32(0, data, 40, 10))
		&& throws(() -> NativeZip.updateAdler32(1, data, -1, 2))
		&& throws(() -> NativeZip.updateCrc32(0, data, 0, -1)));

	// A stream's adler32 is the checksum of what it has read or written: the
	//  last four bytes of a zlib stream
	final text = Bytes.ofString("hello world, hello dictionary");
	final c = new Compress(9);
	c.setFlushMode(FINISH);
	final out = Bytes.alloc(128);
	final w = c.execute(text, 0, out, 0);
	final trailer = (out.get(w.write - 4) << 24) | (out.get(w.write - 3) << 16) | (out.get(w.write - 2) << 8) | out.get(w.write - 1);
	final deflated = NativeZip.getAdler32(deflater(c));
	c.close();
	final u = new Uncompress();
	final back = Bytes.alloc(64);
	u.execute(out, 0, back, 0);
	final inflated = NativeZip.getAdler32(inflater(u));
	u.close();
	final expected = NativeZip.updateAdler32(1, text.getData(), 0, text.length);
	claim("a stream's adler32 is its data's", w.done && deflated == expected && inflated == expected && trailer == expected);
	claim("a closed stream is refused", throws(() -> NativeZip.getAdler32(deflater(c))));
}

function main() {
	presetDictionary();
	checksums();
}
