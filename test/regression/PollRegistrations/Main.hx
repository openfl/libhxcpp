import cpp.net.Poll;
import haxe.io.Bytes;
import sys.net.Address;
import sys.net.Host;
import sys.net.Socket;
import sys.net.UdpSocket;

// What Poll reports as registrations change between prepare calls, where
// epoll and kqueue keep them in the kernel from one call to the next, and
// select and poll() are handed the whole set each time. Every result is
// compared with what is known to be ready.
class Main {
	static var sender = new UdpSocket();

	static function udp():UdpSocket {
		var s = new UdpSocket();
		s.bind(new Host("127.0.0.1"), 0);
		return s;
	}

	static function ping(s:UdpSocket) {
		var to = new Address();
		to.host = s.host().host.ip;
		to.port = s.host().port;
		sender.sendTo(Bytes.ofString("x"), 0, 1, to);
	}

	static function drain(s:UdpSocket) {
		s.readFrom(Bytes.alloc(16), 0, 16, new Address());
	}

	static function indexes(list:Array<Int>):String {
		var shown = [];
		var i = 0;
		while (list[i] >= 0)
			shown.push(list[i++]);
		return "[" + shown.join(",") + "]";
	}

	// Polls until the expected indexes are ready, as bytes can take a moment
	// to arrive: loopback usually delivers at once, but not before send
	// returns, and a macOS runner has held a datagram or a TCP segment back
	// for more than two seconds, the bytes still in the sender's buffer and
	// none in the receiver's (openfl/libhxcpp#431). A check that matches stops
	// at once, so only a failing one waits the whole time.
	static inline var patience = 10.0;

	static function events(poll:Poll, expectRead:String, expectWrite = "[]"):String {
		var got = "";
		var deadline = haxe.Timer.stamp() + patience;
		do {
			poll.events(0.02);
			got = indexes(poll.readIndexes) + " " + indexes(poll.writeIndexes);
		} while (got != '$expectRead $expectWrite' && haxe.Timer.stamp() < deadline);
		return got;
	}

	static function check(name:String, poll:Poll, expectRead:String, expectWrite = "[]") {
		var got = events(poll, expectRead, expectWrite);
		var ok = got == '$expectRead $expectWrite';
		Sys.println('$name: $ok');
		if (!ok)
			Sys.stderr().writeString('  got $got, expected $expectRead $expectWrite
');
	}

	static function main() {
		var unprepared = new Poll(4);
		unprepared.events(0);
		Sys.println('a poll never prepared finds nothing: ${unprepared.readIndexes.length == 0 && unprepared.writeIndexes.length == 0}');

		var sockets = [for (i in 0...200) udp()];
		var poll = new Poll(400);
		poll.prepare(cast sockets, []);
		check("idle", poll, "[]");

		for (i in [3, 64, 65, 150, 199])
			ping(sockets[i]);
		check("ready in prepared order", poll, "[3,64,65,150,199]");
		check("still ready on the next call", poll, "[3,64,65,150,199]");
		poll.prepare(cast sockets, []);
		check("prepared again", poll, "[3,64,65,150,199]");

		var reversed = sockets.copy();
		reversed.reverse();
		poll.prepare(cast reversed, []);
		check("reordered", poll, "[0,49,134,135,196]");

		poll.prepare(cast sockets.slice(0, 100), []);
		check("some left out", poll, "[3,64,65]");
		poll.prepare(cast sockets, []);
		check("put back", poll, "[3,64,65,150,199]");

		for (i in [3, 64, 65, 150, 199])
			drain(sockets[i]);
		check("read", poll, "[]");

		var listener = new Socket();
		listener.bind(new Host("127.0.0.1"), 0);
		listener.listen(4);
		var client = new Socket();
		client.connect(new Host("127.0.0.1"), listener.host().port);
		var server = listener.accept();
		poll.prepare([], [client]);
		check("write only", poll, "[]", "[0]");
		poll.prepare([client], [client]);
		check("one socket both ways, idle", poll, "[]", "[0]");
		server.output.writeString("hello");
		check("one socket both ways, readable", poll, "[0]", "[0]");
		// Windows' select reports a socket listed twice once, and its walk of
		// the result finds the first index only, as it did before
		poll.prepare([client, server, client], [server]);
		check("one socket twice", poll, Sys.systemName() == "Windows" ? "[0]" : "[0,2]", "[0]");
		poll.prepare([client], [server]);
		client.input.readString(5);
		check("read again", poll, "[]", "[0]");

		// The peer closing is a hang-up, ready to read
		server.close();
		poll.prepare([client], []);
		check("peer closed", poll, "[0]");
		client.close();

		// A closed socket's descriptor goes to the next socket made, which is
		// not the one registered for it
		var old = udp();
		poll.prepare(cast [old], []);
		check("before close", poll, "[]");
		old.close();
		var reused = udp();
		poll.prepare(cast [reused], []);
		check("new socket with a reused descriptor, idle", poll, "[]");
		ping(reused);
		check("new socket with a reused descriptor, ready", poll, "[0]");
		drain(reused);

		// Closed after prepare, a socket is reported ready to read, as poll()
		// reported its descriptor and Windows' select failed for it
		var closing = udp();
		poll.prepare(cast [sockets[0], closing], []);
		check("before closing", poll, "[]");
		closing.close();
		poll.events(0);
		var closedReported = poll.readIndexes.slice(0, 2).indexOf(1) >= 0;
		Sys.println('a socket closed after prepare is ready: $closedReported');

		// Poll.poll prepares and waits in one call
		ping(sockets[7]);
		var ready = [];
		var deadline = haxe.Timer.stamp() + patience;
		do {
			ready = poll.poll(cast sockets, 0.02);
		} while (ready.length == 0 && haxe.Timer.stamp() < deadline);
		Sys.println('poll: ${ready.length == 1 && ready[0] == cast sockets[7]}');
		drain(sockets[7]);

		// An idle poll still waits out its timeout
		poll.prepare(cast sockets, []);
		var start = haxe.Timer.stamp();
		poll.events(0.1);
		Sys.println('idle poll waits for its timeout: ${haxe.Timer.stamp() - start >= 0.05}');

		// Two polls over the same sockets keep their own registrations
		var other = new Poll(4);
		other.prepare(cast [sockets[1]], []);
		poll.prepare(cast [sockets[1], sockets[2]], []);
		ping(sockets[2]);
		check("one poll", poll, "[1]");
		check("another poll", other, "[]");

		for (s in sockets)
			s.close();
		reused.close();
		listener.close();
	}
}
