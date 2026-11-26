import haxe.io.Bytes;
import hxcpp.ssl.DigestStream;
import sys.ssl.DigestAlgorithm;

// A program that uses DigestStream and no sys.ssl class: nothing else asks
// for the ssl library, so the class itself has to, or the natives are
// missing from the link. The digests are checked against published values,
// as sys.ssl.Digest is not here to compare with.
function main() {
	Sys.println("sys.ssl.Digest is not compiled in: " + (Type.resolveClass("sys.ssl.Digest") == null));
	Sys.println("sys.ssl.Socket is not compiled in: " + (Type.resolveClass("sys.ssl.Socket") == null));

	final abc = Bytes.ofString("abc");
	final sha1 = new DigestStream(DigestAlgorithm.SHA1);
	sha1.update(abc, 0, 2);
	sha1.update(abc, 2, 1);
	Sys.println("SHA1 of abc: " + sha1.finish().toHex());

	final sha256 = new DigestStream(DigestAlgorithm.SHA256);
	Sys.println("SHA256 of nothing: " + sha256.finish().toHex());

	// The million a's of FIPS 180-2, a thousand at a time
	final thousand = Bytes.alloc(1000);
	thousand.fill(0, 1000, "a".code);
	final sha512 = new DigestStream(DigestAlgorithm.SHA512);
	for (i in 0...1000)
		sha512.update(thousand, 0, 1000);
	Sys.println("SHA512 of a million a: " + sha512.finish().toHex());
}
