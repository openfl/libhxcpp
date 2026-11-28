import haxe.io.Path;
import sys.FileSystem;
import sys.io.File;
import sys.ssl.Certificate;

// Certificate.loadFile and loadPath throw when a certificate does not parse,
// and mbedTLS reports how many did not as a positive count. That count was
// read as an mbedTLS error code, so one bad certificate threw
// "ERROR - Generic error" and two a BIGNUM file error. They say how many now.
final corrupt = "-----BEGIN CERTIFICATE-----\nMIIBAAAAAAAAAAAA\n-----END CERTIFICATE-----\n";

function message(load:() -> Certificate):String {
	try {
		load();
		return "loaded";
	} catch (e:Dynamic) {
		return Std.string(e);
	}
}

function claim(text:String, value:Bool)
	Sys.println('$text: $value');

function main() {
	final good = haxe.Resource.getString("cert");
	final dir = Path.join([Path.directory(Sys.programPath()), "certs"]);
	if (!FileSystem.exists(dir))
		FileSystem.createDirectory(dir);
	final goodFile = Path.join([dir, "good.pem"]);
	final oneBad = Path.join([dir, "one-bad.pem"]);
	final twoBad = Path.join([dir, "two-bad.pem"]);
	File.saveContent(goodFile, good);
	File.saveContent(oneBad, good + corrupt);
	File.saveContent(twoBad, corrupt + good + corrupt);

	claim("a file of good certificates loads", message(() -> Certificate.loadFile(goodFile)) == "loaded");
	claim("one bad certificate in a file is counted",
		message(() -> Certificate.loadFile(oneBad)) == 'Could not parse 1 certificate in $oneBad');
	claim("two bad certificates in a file are counted",
		message(() -> Certificate.loadFile(twoBad)) == 'Could not parse 2 certificates in $twoBad');

	final mixed = Path.join([dir, "mixed"]);
	if (!FileSystem.exists(mixed))
		FileSystem.createDirectory(mixed);
	File.saveContent(Path.join([mixed, "good.pem"]), good);
	File.saveContent(Path.join([mixed, "notes.txt"]), "not a certificate\n");
	claim("a file with no certificate in a directory is counted",
		message(() -> Certificate.loadPath(mixed)) == 'Could not parse 1 certificate or file in $mixed');

	final missing = Path.join([dir, "missing.pem"]);
	final noFile = message(() -> Certificate.loadFile(missing));
	claim("a missing file still gives mbedTLS's error", noFile != "loaded" && noFile.indexOf("Could not parse") < 0);
}
