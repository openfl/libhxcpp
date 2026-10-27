import haxe.Resource;
import sys.net.Host;
import sys.ssl.Certificate;
import sys.ssl.Key;
import sys.ssl.Socket;
import sys.thread.Lock;
import sys.thread.Thread;

// A TLS server and client in one process, the client verifying the server's
// certificate against a CA of its own. ca.pem (RSA 2048) signs server.pem
// (P-256, for localhost and 127.0.0.1); other-ca.pem signs nothing here. The
// server sends TLS 1.3 session tickets after the handshake, which the
// client's first read has to get past.

// Serves one connection, echoing a line in upper case. The handshake fails
// on the server too when the client refuses the certificate, which is not
// this test's to report.
function serve():{port:Int, done:Lock} {
	final listener = new Socket();
	listener.verifyCert = false;
	listener.setCertificate(Certificate.fromString(Resource.getString("server")), Key.readPEM(Resource.getString("server-key"), false));
	listener.bind(new Host("127.0.0.1"), 0);
	listener.listen(1);
	final done = new Lock();
	Thread.create(() -> {
		final peer = listener.accept();
		try {
			peer.handshake();
			final line = peer.input.readLine();
			peer.output.writeString(line.toUpperCase() + "\n");
			peer.output.flush();
		} catch (e:Dynamic) {}
		peer.close();
		listener.close();
		done.release();
	});
	return {port: listener.host().port, done: done};
}

function client(ca:String, hostname:String, port:Int):Socket {
	final socket = new Socket();
	socket.verifyCert = true;
	socket.setCA(Certificate.fromString(Resource.getString(ca)));
	socket.setHostname(hostname);
	socket.connect(new Host("127.0.0.1"), port);
	return socket;
}

function verified() {
	final server = serve();
	final socket = client("ca", "localhost", server.port);
	socket.output.writeString("hello\n");
	socket.output.flush();
	Sys.println('verified: ${socket.input.readLine()}');
	final cert = socket.peerCertificate();
	Sys.println('subject: CN=${cert.commonName} O=${cert.subject("O")}');
	Sys.println('issuer: CN=${cert.issuer("CN")} O=${cert.issuer("O")} C=${cert.issuer("C")}');
	Sys.println('first alt name: ${cert.altNames[0]} of ${cert.altNames.length}');
	Sys.println('valid from ${cert.notBefore.getUTCFullYear()} to ${cert.notAfter.getUTCFullYear()}');
	socket.close();
	server.done.wait();
}

function refused(what:String, ca:String, hostname:String) {
	final server = serve();
	try {
		client(ca, hostname, server.port).close();
		Sys.println('$what: accepted');
	} catch (e:Dynamic) {
		Sys.println('$what: refused');
	}
	server.done.wait();
}

function main() {
	verified();
	refused("wrong hostname", "ca", "example.com");
	refused("untrusted CA", "other-ca", "localhost");
}
