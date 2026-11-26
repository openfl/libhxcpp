import haxe.io.Bytes;
import hxcpp.ssl.DigestStream;
import sys.ssl.Digest;
import sys.ssl.DigestAlgorithm;

// A digest fed in pieces must equal the digest of the whole input, however
// it is split: at every point of a buffer longer than the hashes' blocks, a
// byte at a time, in a few large pieces, with no input at all, and with
// zero-length pieces between the others.
final algorithms = [DigestAlgorithm.MD5, DigestAlgorithm.SHA1, DigestAlgorithm.SHA256, DigestAlgorithm.SHA512];

function pattern(length:Int, seed:Int):Bytes {
	final b = Bytes.alloc(length);
	var x = seed;
	for (i in 0...length) {
		x = (x * 1103515245 + 12345) & 0x7fffffff;
		b.set(i, x >> 16);
	}
	return b;
}

function streamed(alg:DigestAlgorithm, data:Bytes, cuts:Array<Int>):Bytes {
	final stream = new DigestStream(alg);
	var pos = 0;
	for (cut in cuts) {
		stream.update(data, pos, cut - pos);
		pos = cut;
	}
	stream.update(data, pos, data.length - pos);
	return stream.finish();
}

function report(name:String, ok:Bool) {
	Sys.println(name + ": " + ok);
}

function main() {
	// Longer than two of SHA-512's 128-byte blocks, so splits fall before,
	// on and after block boundaries for every algorithm
	final small = pattern(300, 1);
	final large = pattern(3 * 1024 * 1024 + 17, 2);
	final empty = Bytes.alloc(0);

	for (alg in algorithms) {
		final name = Std.string(alg);
		final whole = Digest.make(small, alg);

		var everySplit = true;
		for (cut in 0...small.length + 1) {
			if (streamed(alg, small, [cut]).compare(whole) != 0) {
				Sys.stderr().writeString('$name differs split at $cut\n');
				everySplit = false;
			}
		}
		report('$name split at every point matches Digest.make', everySplit);

		var twoSplits = true;
		var cut = 0;
		while (cut <= small.length) {
			var second = cut;
			while (second <= small.length) {
				if (streamed(alg, small, [cut, second]).compare(whole) != 0)
					twoSplits = false;
				second += 7;
			}
			cut += 5;
		}
		report('$name split in three matches Digest.make', twoSplits);

		final bytewise = new DigestStream(alg);
		for (i in 0...small.length)
			bytewise.update(small, i, 1);
		report('$name one byte at a time matches Digest.make', bytewise.finish().compare(whole) == 0);

		final third = Std.int(large.length / 3);
		report('$name in three large updates matches Digest.make',
			streamed(alg, large, [third, 2 * third]).compare(Digest.make(large, alg)) == 0);
		report('$name in one large update matches Digest.make',
			streamed(alg, large, []).compare(Digest.make(large, alg)) == 0);

		final none = new DigestStream(alg);
		report('$name of no input matches Digest.make of empty bytes', none.finish().compare(Digest.make(empty, alg)) == 0);

		final zeros = new DigestStream(alg);
		zeros.update(empty, 0, 0);
		zeros.update(small, 0, 0);
		zeros.update(small, 0, 100);
		zeros.update(small, 100, 0);
		zeros.update(empty, 0, 0);
		zeros.update(small, 100, 200);
		zeros.update(small, 300, 0);
		report('$name with zero-length updates between others matches Digest.make', zeros.finish().compare(whole) == 0);

		final onlyZeros = new DigestStream(alg);
		onlyZeros.update(small, 0, 0);
		onlyZeros.update(small, small.length, 0);
		report('$name of zero-length updates alone matches Digest.make of empty bytes',
			onlyZeros.finish().compare(Digest.make(empty, alg)) == 0);
	}

	// Against published values too, not only against Digest.make
	final abc = Bytes.ofString("abc");
	final sha256 = new DigestStream(DigestAlgorithm.SHA256);
	sha256.update(abc, 0, 1);
	sha256.update(abc, 1, 2);
	report("SHA256 of abc is the FIPS 180-2 value",
		sha256.finish().toHex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	final md5 = new DigestStream(DigestAlgorithm.MD5);
	md5.update(abc, 0, 3);
	report("MD5 of abc is the RFC 1321 value", md5.finish().toHex() == "900150983cd24fb0d6963f7d28e17f72");

	// The input is read, not changed
	final copy = small.sub(0, small.length);
	streamed(DigestAlgorithm.SHA256, small, [10, 150]);
	report("input bytes are left unchanged", copy.compare(small) == 0 && empty.length == 0 && empty.getData().length == 0);
}
