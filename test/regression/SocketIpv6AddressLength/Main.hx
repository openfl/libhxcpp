import cpp.NativeSocket;
import haxe.io.Bytes;
import haxe.io.BytesData;

// cpp.NativeSocket's IPv6 functions take the address as a byte array. Three
// of them read 16 bytes from its start without looking at its length, so a
// shorter array was read past its end: formatted, connected to or bound to as
// whatever followed it in memory, and a null array crashed. Only
// host_reverse_ipv6 checked. The bytes are 0xff, so an over-read shows.
function address(length:Int):BytesData {
	final bytes = Bytes.alloc(length);
	bytes.fill(0, length, 0xff);
	return bytes.getData();
}

function show(s:String):String {
	return s == null ? "null" : s;
}

function threw(f:() -> Void):String {
	try {
		f();
		return "nothing";
	} catch (e:Dynamic) {
		return Std.string(e);
	}
}

function main() {
	for (length in [0, 4, 15, 17])
		Sys.println('to_string of $length bytes: ${show(NativeSocket.host_to_string_ipv6(address(length)))}');
	Sys.println('to_string of 16 bytes: ${show(NativeSocket.host_to_string_ipv6(address(16)))}');
	Sys.println('reverse of 4 bytes: ${show(NativeSocket.host_reverse_ipv6(address(4)))}');

	NativeSocket.socket_init();
	final socket = NativeSocket.socket_new_ip(false, true);
	// Not blocking, so a connect to an over-read address does not wait
	NativeSocket.socket_set_blocking(socket, false);
	Sys.println('connect to 4 bytes threw: ${threw(() -> NativeSocket.socket_connect_ipv6(socket, address(4), 80))}');
	Sys.println('bind to 4 bytes threw: ${threw(() -> NativeSocket.socket_bind_ipv6(socket, address(4), 0))}');
	Sys.println('connect to null threw: ${threw(() -> NativeSocket.socket_connect_ipv6(socket, null, 80))}');
	Sys.println('bind to null threw: ${threw(() -> NativeSocket.socket_bind_ipv6(socket, null, 0))}');
	NativeSocket.socket_close(socket);

	Sys.println('to_string of null: ${show(NativeSocket.host_to_string_ipv6(null))}');
}
