import haxe.io.Bytes;
import hxcpp.ssl.DigestStream;
import sys.ssl.Digest;
import sys.ssl.DigestAlgorithm;

// Streams that are never finished hold mbedTLS memory until the collector
// finalizes them. Their memory counts towards collection pressure while they
// live, and finalizing them frees it and takes it off again; streams kept
// through those collections still work.
class Main {
	static inline var STREAMS = 20000;

	static var kept:Array<DigestStream>;
	static var data = Bytes.ofString("libhxcpp digest stream");

	static function large():Float {
		return cpp.vm.Gc.memInfo64(cpp.vm.Gc.MEM_INFO_LARGE);
	}

	static function collect():Float {
		for (i in 0...3)
			cpp.vm.Gc.run(true);
		return large();
	}

	static function keep() {
		kept = [];
		for (i in 0...STREAMS) {
			final stream = new DigestStream(DigestAlgorithm.SHA512);
			stream.update(data, 0, i % data.length);
			kept.push(stream);
		}
	}

	static function drop() {
		kept = null;
	}

	static function abandon(count:Int) {
		for (i in 0...count) {
			final stream = new DigestStream(i % 2 == 0 ? DigestAlgorithm.SHA256 : DigestAlgorithm.SHA512);
			stream.update(data, 0, data.length);
		}
	}

	static function verifyHalf():Bool {
		var ok = true;
		for (i in 0...STREAMS >> 1) {
			final expected = Digest.make(data.sub(0, i % data.length), DigestAlgorithm.SHA512);
			if (kept[i].finish().compare(expected) != 0)
				ok = false;
		}
		return ok;
	}

	static function main() {
		var baseline = collect();

		keep();
		// Each holds a SHA-512 context, some 200 bytes
		final grown = collect() - baseline;
		Sys.stderr().writeString('kept $STREAMS streams: $grown bytes\n');
		// The byte count assumes libhxcpp's own mbedTLS. On a build that takes another
		// library's (hxcpp_mbedtls_provider), what mbedTLS allocates is not counted, each
		// stream reports only its context, and this line prints false
		Sys.println("unfinished streams contribute to GC pressure: " + (grown > STREAMS * 100));
		Sys.println("kept streams digest correctly after collections: " + verifyHalf());
		Sys.println("finishing half the streams frees their memory: " + (collect() - baseline < grown * 0.75));

		drop();
		final left = collect() - baseline;
		Sys.stderr().writeString('after dropping them: $left bytes\n');
		Sys.println("finalizing dropped streams removes their pressure: " + (left < 1024 * 1024));

		for (round in 0...10)
			abandon(STREAMS);
		final churned = collect() - baseline;
		Sys.stderr().writeString('after abandoning ${10 * STREAMS}: $churned bytes\n');
		Sys.println("abandoned streams are freed by collection: " + (churned < 1024 * 1024));

		final after = new DigestStream(DigestAlgorithm.SHA256);
		after.update(data, 0, data.length);
		Sys.println("a new stream digests after the finalizers ran: " + (after.finish().compare(Digest.make(data, DigestAlgorithm.SHA256)) == 0));
	}
}
