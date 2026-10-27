import haxe.io.Bytes;
import haxe.Resource;
import sys.ssl.Digest;
import sys.ssl.DigestAlgorithm;
import sys.ssl.Key;

// Digests, signatures and keys, through mbedTLS's cryptography: the
// signatures need its random numbers, the encrypted key its PKCS#5.
// ca-key-encrypted.pem is an RSA 2048 key, PKCS#8 under AES-256-CBC with the
// password "hxcpp-test", and server-key.pem a P-256 one; the -pub files are
// their public halves. The digests of "abc" are the standard test vectors.

function digests() {
	final abc = Bytes.ofString("abc");
	for (alg in [MD5, SHA1, SHA224, SHA256, SHA384, SHA512, RIPEMD160])
		Sys.println('$alg: ${Digest.make(abc, alg).toHex()}');
	try {
		Digest.make(abc, cast "SHA0");
		Sys.println("SHA0: made");
	} catch (e:Dynamic) {
		Sys.println('SHA0: $e');
	}
}

function signs(what:String, priv:Key, pub:Key) {
	final data = Bytes.ofString("The quick brown fox jumps over the lazy dog");
	final tampered = Bytes.ofString("The quick brown fox jumps over the lazy cog");
	for (alg in [SHA256, SHA384, SHA512]) {
		final signature = Digest.sign(data, priv, alg);
		Sys.println('$what $alg: verifies ${Digest.verify(data, signature, pub, alg)}, tampered ${Digest.verify(tampered, signature, pub, alg)}');
	}
}

function main() {
	digests();
	signs("RSA", Key.readPEM(Resource.getString("ca-key-encrypted"), false, "hxcpp-test"), Key.readPEM(Resource.getString("ca-pub"), true));
	signs("P-256", Key.readPEM(Resource.getString("server-key"), false), Key.readPEM(Resource.getString("server-pub"), true));
	try {
		Key.readPEM(Resource.getString("ca-key-encrypted"), false, "wrong");
		Sys.println("wrong password: read");
	} catch (e:Dynamic) {
		Sys.println("wrong password: refused");
	}
}
