import sys.io.Process;
import sys.io.File;
import sys.FileSystem;
import sys.thread.Lock;
import sys.thread.Thread;

using StringTools;

// With -D emscripten each test runs under node, through this script, which
// stops it after the timeout: a stack overflow there can hang a program
// instead of crashing it. Exits with 124 on a timeout.
final nodeWithTimeout = "const r = require('child_process').spawnSync(process.execPath, [process.argv[1]], {stdio: 'inherit', timeout: +process.argv[2]});"
	+ "if (r.error || r.signal) { console.error('timed out'); process.exit(124); }"
	+ "process.exit(r.status);";
final emscriptenTimeoutSeconds = 120;

// What the last test wrote to stderr, shown if it fails
var lastStderr = "";

function printStderr() {
	if (lastStderr == "") {
		return;
	}
	// The end of it, where a crash or a sanitizer leaves its report
	final limit = 8000;
	Sys.println("Stderr:");
	Sys.println(lastStderr.length > limit ? "..." + lastStderr.substr(lastStderr.length - limit) : lastStderr);
}

function runOutput(test:String, emscripten:Bool):String {
	final slash = Sys.systemName() == "Windows" ? "\\" : "/";
	final proc = emscripten
		? new Process("node", ["-e", nodeWithTimeout, '$test/bin/Main.js', Std.string(emscriptenTimeoutSeconds * 1000)])
		: new Process([test, "bin", 'Main'].join(slash));
	// Read while stdout is, so a test that fills the stderr pipe does not stop
	final stderrRead = new Lock();
	var stderr = "";
	Thread.create(() -> {
		stderr = proc.stderr.readAll().toString();
		stderrRead.release();
	});
	final stdout = proc.stdout.readAll().toString();
	final code = proc.exitCode();
	stderrRead.wait();
	// Each test's pipes stayed open here, and every later test inherited them:
	//  after some 290 tests, SocketSelectHighDescriptors' sockets were past
	//  macOS's FD_SETSIZE of 1024, and its select failed
	proc.close();
	lastStderr = stderr.replace("\r\n", "\n");

	if (emscripten && code == 124) {
		throw 'timed out after $emscriptenTimeoutSeconds s';
	}
	if (code != 0) {
		throw 'return code was $code';
	}

	return stdout.replace("\r\n", "\n");
}

function isEmscripten(args:Array<String>) {
	for (i in 0...args.length) {
		if (args[i] == "-Demscripten" || ((args[i] == "-D" || args[i] == "--define") && args[i + 1] == "emscripten")) {
			return true;
		}
	}
	return false;
}

// emscripten-skip.txt: one "Test: reason" per line, for the tests that need
// what a program under node does not have
function emscriptenSkips():Map<String, String> {
	final skips = new Map<String, String>();
	for (line in File.getContent("emscripten-skip.txt").split("\n")) {
		line = line.trim();
		if (line == "" || line.startsWith("#")) {
			continue;
		}
		final colon = line.indexOf(":");
		skips[line.substr(0, colon).trim()] = line.substr(colon + 1).trim();
	}
	return skips;
}

function main() {
	var successes = 0;
	var total = 0;

	var args = Sys.args();
	// Comma-separated tests that the Haxe in use cannot build, such as one its
	// compiler rejects
	final skipList = Sys.getEnv("REGRESSION_SKIP");
	final skip = skipList == null ? [] : skipList.split(",");

	final emscripten = isEmscripten(args);
	final emscriptenSkip = emscripten ? emscriptenSkips() : new Map<String, String>();
	if (emscripten) {
		// Tests read their own files, and cppia hosts load their scripts
		args = args.concat(["-D", "HXCPP_LINK_NODERAWFS"]);
	}

	for (test in FileSystem.readDirectory(".")) {
		if (!FileSystem.isDirectory(test)) {
			continue;
		}
		if (skip.contains(test)) {
			Sys.println('Skipping $test');
			continue;
		}
		if (emscriptenSkip.exists(test)) {
			Sys.println('Skipping $test on Emscripten: ${emscriptenSkip[test]}');
			continue;
		}

		total++;

		// From inside the test, not with -C: Haxe 5.0.0-preview.1 does not keep the
		// -C directory for the sections after --next, which the cppia tests use
		final root = Sys.getCwd();
		Sys.setCwd(test);
		final buildExitCode = Sys.command("haxe", ["build.hxml"].concat(args));
		Sys.setCwd(root);
		if (buildExitCode != 0) {
			Sys.println('Failed to build test $test. Exit code: $buildExitCode');
			continue;
		}

		final expectedStdout = File.getContent('$test/stdout.txt').replace("\r\n", "\n");
		lastStderr = "";
		final actualStdout = try {
			runOutput(test, emscripten);
		} catch (e) {
			Sys.println('Test $test failed: $e');
			printStderr();
			continue;
		};

		if (actualStdout != expectedStdout) {
			Sys.println('Test $test failed: Output did not match');

			Sys.println("Expected stdout:");
			Sys.println(expectedStdout);
			Sys.println("Actual stdout:");
			Sys.println(actualStdout);
			printStderr();
			continue;
		}

		successes++;
	}

	Sys.println('Regression tests complete. Successes: $successes / $total');

	if (successes < total) {
		Sys.exit(1);
	}
}
