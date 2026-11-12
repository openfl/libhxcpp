import cpp.net.Poll;
import haxe.io.Bytes;
import sys.net.Address;
import sys.net.Host;
import sys.net.Socket;
import sys.net.UdpSocket;

// Libraries that make socket handles of their own, such as CrossByte's
// accept, declare a SocketWrapper in stock hxcpp's layout, the descriptor
// after the object header. Such a handle has no poll serial, so epoll and
// kqueue took the next socket given a closed one's descriptor for the socket
// registered, whose registration the kernel had dropped at the close: a
// server polling the connections it accepted never heard from a new one.
@:cppFileCode('
#ifdef HX_WINDOWS
typedef size_t LibrarySocket;
#else
typedef int LibrarySocket;
#endif

static int sSocketType = 0;

struct SocketWrapper : public hx::Object
{
	LibrarySocket socket;

	int __GetType() const override { return sSocketType; }
};

// Where both layouts keep the descriptor: right after the object header
static LibrarySocket &descriptorOf(hx::Object *inHandle)
{
	const size_t offset = (sizeof(hx::Object) + alignof(LibrarySocket) - 1) / alignof(LibrarySocket) * alignof(LibrarySocket);
	return *reinterpret_cast<LibrarySocket *>(reinterpret_cast<char *>(inHandle) + offset);
}

// A library handle for the descriptor of a socket the runtime made, which
// lets go of it
static Dynamic takeDescriptor(Dynamic inOwn)
{
	sSocketType = inOwn->__GetType();
	SocketWrapper *handle = new SocketWrapper();
	handle->socket = descriptorOf(inOwn.mPtr);
	descriptorOf(inOwn.mPtr) = (LibrarySocket)-1;
	return handle;
}

static double descriptorNumber(Dynamic inHandle)
{
	return (double)descriptorOf(inHandle.mPtr);
}
')
class Main {
	static var sender = new UdpSocket();

	static function udp():UdpSocket {
		var s = new UdpSocket();
		s.bind(new Host("127.0.0.1"), 0);
		return s;
	}

	// A socket whose handle a library made, as a sys.net.Socket has one
	static function library():{socket:UdpSocket, handle:Dynamic, port:Int} {
		var own = udp();
		var port = own.host().port;
		var handle:Dynamic = untyped __cpp__("takeDescriptor({0})", @:privateAccess own.__s);
		var socket = Type.createEmptyInstance(UdpSocket);
		@:privateAccess socket.__s = handle;
		return {socket: socket, handle: handle, port: port};
	}

	static function descriptor(handle:Dynamic):Float {
		return untyped __cpp__("descriptorNumber({0})", handle);
	}

	static function ping(port:Int) {
		var to = new Address();
		to.host = new Host("127.0.0.1").ip;
		to.port = port;
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

	// Polls until the expected indexes are ready, as a datagram can take a
	// moment to arrive
	static function check(name:String, poll:Poll, expectRead:String, expectWrite = "[]") {
		var got = "";
		for (attempt in 0...50) {
			poll.events(0.02);
			got = indexes(poll.readIndexes) + " " + indexes(poll.writeIndexes);
			if (got == '$expectRead $expectWrite')
				break;
		}
		var ok = got == '$expectRead $expectWrite';
		Sys.println('$name: $ok');
		if (!ok)
			Sys.stderr().writeString('  got $got, expected $expectRead $expectWrite\n');
	}

	static function main() {
		var poll = new Poll(8);

		var first = library();
		poll.prepare(cast [first.socket], []);
		check("a library's socket, idle", poll, "[]");
		ping(first.port);
		check("a library's socket, ready", poll, "[0]");
		drain(first.socket);
		poll.prepare(cast [first.socket], []);
		check("prepared again, idle", poll, "[]");
		ping(first.port);
		check("prepared again, ready", poll, "[0]");
		drain(first.socket);
		poll.prepare([], cast [first.socket]);
		check("prepared for writing", poll, "[]", "[0]");
		poll.prepare(cast [first.socket], []);
		check("prepared for reading again", poll, "[]");

		// Closed, its descriptor goes to the next socket made, here another
		// library's
		var was = descriptor(first.handle);
		cpp.NativeSocket.socket_close(first.handle);
		var second = library();
		if (descriptor(second.handle) != was)
			Sys.stderr().writeString('  the descriptor was not given out again: $was, then ${descriptor(second.handle)}\n');
		poll.prepare(cast [second.socket], []);
		check("a library's socket with a reused descriptor, idle", poll, "[]");
		ping(second.port);
		check("a library's socket with a reused descriptor, ready", poll, "[0]");
		drain(second.socket);

		// And from a library's socket to one the runtime made
		cpp.NativeSocket.socket_close(second.handle);
		var own = udp();
		poll.prepare(cast [own], []);
		check("a runtime socket with a library's descriptor, idle", poll, "[]");
		ping(own.host().port);
		check("a runtime socket with a library's descriptor, ready", poll, "[0]");
		drain(own);

		own.close();
		sender.close();
	}
}
