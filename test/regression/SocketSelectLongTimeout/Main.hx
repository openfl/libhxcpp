import sys.net.Host;
import sys.net.Socket;
import sys.thread.Thread;

// Socket.select converted its timeout to an int of seconds, which overflowed
// past 2^31 s: a select with nothing ready returned at once instead of waiting.
// Infinity was one such timeout. Each select here waits for a connection that
// another thread makes after 0.3 s.
function main() {
	var server = new Socket();
	server.bind(new Host("127.0.0.1"), 0);
	server.listen(4);
	var port = server.host().port;
	for (timeout in [3e9, Math.POSITIVE_INFINITY]) {
		Thread.create(() -> {
			Sys.sleep(0.3);
			var client = new Socket();
			client.connect(new Host("127.0.0.1"), port);
			Sys.sleep(0.2);
			client.close();
		});
		var t0 = haxe.Timer.stamp();
		var ready = Socket.select([server], [], [], timeout).read.length;
		var waited = haxe.Timer.stamp() - t0 >= 0.25;
		Sys.println('select with a timeout of $timeout waited for the connection: ${waited && ready == 1}');
		server.accept().close();
	}
	var t0 = haxe.Timer.stamp();
	var ready = Socket.select([server], [], [], Math.NaN).read.length;
	Sys.println("select with a NaN timeout returned at once: " + (ready == 0 && haxe.Timer.stamp() - t0 < 0.25));
	server.close();
}
