import haxe.io.Bytes;
import haxe.io.BytesBuffer;
import haxe.io.BytesInput;
import sys.FileSystem;
import sys.io.File;

using StringTools;

/**
	Fuzz targets for the runtime's parsers and decoders (libhxcpp #257).

	  Fuzz <target> [libFuzzer options] [corpus dirs]   built with -D fuzz_libfuzzer
	  Fuzz <target> <files or dirs>                     otherwise: replays the inputs
	  Fuzz list

	FUZZ_TARGET in the environment names the target in place of the first
	argument, for libFuzzer modes that start the program again.

	Each target takes untrusted bytes. Input the target rejects may throw;
	anything else, a crash, a sanitizer report or a broken invariant (which
	aborts), is a bug. run.sh builds this with clang, libFuzzer, ASan and
	UBSan and runs each target; replay.sh runs the seed corpus and every
	input that once failed, with any compiler, as a regression test.
**/
#if fuzz_libfuzzer
// Clang links libFuzzer's own main with -fsanitize=fuzzer, so run.sh asks for
// fuzzer-no-link and names the library without main; cl.exe needs neither
@:buildXml('<target id="haxe"><lib name="${FUZZER_NO_MAIN}" if="FUZZER_NO_MAIN"/></target>')
@:cppFileCode('
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

extern "C" int LLVMFuzzerRunDriver(int *argc, char ***argv, int (*callback)(const uint8_t *, size_t));

// libFuzzer calls this from inside Fuzz.main, so the collector already scans
// this thread from main\'s frame down
static int hxFuzzOne(const uint8_t *data, size_t size)
{
   Array<unsigned char> bytes = Array_obj<unsigned char>::__new((int)size, (int)size);
   if (size)
      memcpy(bytes->GetBase(), data, size);
   ::Fuzz_obj::one(bytes);
   return 0;
}
')
#end
class Fuzz {
	static final targets:Map<String, Bytes->Void> = [
		"cppia" => cppia,
		"utf8" => utf8,
		"utf16" => utf16,
		"bytes" => bytes,
		"serializer" => serializer,
		"json" => json,
		"ereg" => ereg,
		"number" => number,
		"url" => url,
		"zlib" => zlib,
	];

	static var target:Bytes->Void;

	static function main() {
		var args = Sys.args();
		if (args.length == 1 && args[0] == "list") {
			var names = [for (name in targets.keys()) name];
			names.sort(Reflect.compare);
			Sys.println(names.join("\n"));
			return;
		}
		// libFuzzer starts the program again without the target for some modes,
		// such as -minimize_crash and -fork, so FUZZ_TARGET can name it instead
		var name = Sys.getEnv("FUZZ_TARGET");
		if (name == null || name == "")
			name = args.shift();
		if (name == null || !targets.exists(name)) {
			Sys.stderr().writeString("usage: Fuzz list | Fuzz <target> [inputs or libFuzzer options]\n");
			Sys.exit(2);
		}
		target = targets[name];
		#if fuzz_libfuzzer
		var args = [Sys.programPath()].concat(args);
		untyped __cpp__('
			std::vector<char *> cargs;
			for(int i=0; i<{0}->length; i++)
				cargs.push_back(strdup({0}[i].utf8_str()));
			cargs.push_back(0);
			int argc = (int)cargs.size() - 1;
			char **argv = cargs.data();
			exit(LLVMFuzzerRunDriver(&argc, &argv, hxFuzzOne))', args);
		#else
		var count = 0;
		for (path in args)
			for (file in inputs(path)) {
				target(File.getBytes(file));
				count++;
			}
		Sys.println('$name: $count inputs');
		#end
	}

	@:keep public static function one(data:haxe.io.BytesData) {
		target(Bytes.ofData(data));
	}

	static function inputs(path:String):Array<String> {
		if (!FileSystem.isDirectory(path))
			return [path];
		var names = FileSystem.readDirectory(path);
		names.sort(Reflect.compare);
		var files = [];
		for (name in names)
			if (!name.startsWith("."))
				files = files.concat(inputs(path + "/" + name));
		return files;
	}

	// --- Targets ---------------------------------------------------------

	/**
		A malformed .cppia must fail to load with an error, never crash
		(SECURITY.md). A module that loads is released, so its classes and
		resources do not stay registered for the next input.
	**/
	static function cppia(b:Bytes) {
		var module = attempt(() -> cpp.cppia.Module.fromData(b.getData()));
		if (module != null)
			untyped module.release();
	}

	/**
		String from UTF-8 bytes, back to bytes, and the String functions over
		the result. cpp.encoding is only built at API level 500, for Haxe 5.
	**/
	static function utf8(b:Bytes) {
		var s = b.getString(0, b.length);
		var units = utf16Units(b);
		if (units != null) {
			// Valid UTF-8 must decode to exactly its UTF-16 code units and encode back unchanged
			if (s.length != units.length)
				fail('decoded ${units.length} UTF-16 units to a string of length ${s.length}');
			for (i in 0...units.length)
				if (s.charCodeAt(i) != units[i])
					fail('unit $i decoded as ${s.charCodeAt(i)}, not ${units[i]}');
			if (Bytes.ofString(s).compare(b) != 0)
				fail("valid UTF-8 changed on the way back to bytes");
		}
		exercise(s);
	}

	/** Arbitrary UTF-16, lone surrogates included, through the String functions and back. **/
	static function utf16(b:Bytes) {
		var buf = new StringBuf();
		var units = [];
		var i = 0;
		while (i + 1 < b.length) {
			var u = b.get(i) | (b.get(i + 1) << 8);
			units.push(u);
			i += 2;
		}
		var s = attempt(() -> {
			for (u in units)
				buf.addChar(u);
			buf.toString();
		});
		if (s != null) {
			exercise(s);
			if (!hasLoneSurrogate(units)) {
				var back = Bytes.ofString(s).toString();
				if (back != s)
					fail("UTF-16 without lone surrogates changed through UTF-8");
			}
		}
	}

	/**
		Bytes reads, writes and conversions, checked against a plain array
		of the same bytes. Each operation is one byte of opcode then its
		operands. In range, every result must match the array. Positions and
		lengths may also be out of range: hxcpp's Bytes.set then grows the
		data or drops the write, and the std checks bounds for the rest, so
		those operations only have to be memory safe, which ASan checks; the
		array then takes what the bytes hold.
	**/
	static function bytes(b:Bytes) {
		var r = new Reader(b);
		var size = r.byte();
		var data = Bytes.alloc(size);
		var model = [for (i in 0...size) 0];
		for (i in 0...size) {
			data.set(i, i * 7);
			model[i] = (i * 7) & 0xff;
		}
		function inRange(pos:Int, len:Int)
			return pos >= 0 && len >= 0 && pos + len <= size;
		function le(pos:Int, len:Int):Int {
			var v = 0;
			for (k in 0...len)
				v |= model[pos + k] << (8 * k);
			return v;
		}
		while (!r.done()) {
			var op = r.byte() % 16;
			var pos = r.pos();
			var checked = true;
			switch (op) {
				case 0:
					var v = r.int();
					attempt(() -> data.setInt32(pos, v));
					if (inRange(pos, 4))
						for (k in 0...4)
							model[pos + k] = (v >>> (8 * k)) & 0xff;
					else
						checked = false;
				case 1:
					var got:Null<Int> = attempt(() -> data.getInt32(pos));
					if (inRange(pos, 4) && got != le(pos, 4))
						fail('getInt32 at $pos gave $got, expected ${le(pos, 4)}');
				case 2:
					var v = r.int() & 0xffff;
					attempt(() -> data.setUInt16(pos, v));
					if (inRange(pos, 2)) {
						model[pos] = v & 0xff;
						model[pos + 1] = v >> 8;
					} else
						checked = false;
				case 3:
					var got:Null<Int> = attempt(() -> data.getUInt16(pos));
					if (inRange(pos, 2) && got != le(pos, 2))
						fail('getUInt16 at $pos gave $got, expected ${le(pos, 2)}');
				case 4:
					var v = r.double();
					attempt(() -> data.setDouble(pos, v));
					if (inRange(pos, 8)) {
						var got = data.getDouble(pos);
						if (!(got == v || (Math.isNaN(got) && Math.isNaN(v))))
							fail('setDouble then getDouble at $pos gave $got for $v');
					}
					checked = false;
				case 5:
					var v = r.int();
					attempt(() -> data.setFloat(pos, v / 3));
					attempt(() -> data.getFloat(pos));
					checked = false;
				case 6:
					var hi = r.int();
					var lo = r.int();
					attempt(() -> data.setInt64(pos, haxe.Int64.make(hi, lo)));
					if (inRange(pos, 8)) {
						var got = data.getInt64(pos);
						if (got.high != hi || got.low != lo)
							fail('setInt64 then getInt64 at $pos gave ${got.high}:${got.low} for $hi:$lo');
					}
					checked = false;
				case 7:
					var len = r.pos();
					var sub = attempt(() -> data.sub(pos, len));
					if (inRange(pos, len)) {
						if (sub == null || sub.length != len)
							fail('sub($pos, $len) failed');
						for (k in 0...len)
							if (sub.get(k) != model[pos + k])
								fail('sub($pos, $len) byte $k differs');
					}
				case 8:
					var src = r.pos();
					var len = r.pos();
					attempt(() -> data.blit(pos, data, src, len));
					if (inRange(pos, len) && inRange(src, len)) {
						var copy = model.slice(src, src + len);
						for (k in 0...len)
							model[pos + k] = copy[k];
					} else
						checked = false;
				case 9:
					var len = r.pos();
					var v = r.byte();
					attempt(() -> data.fill(pos, len, v));
					if (inRange(pos, len))
						for (k in 0...len)
							model[pos + k] = v;
					else
						checked = false;
				case 10:
					var len = r.pos();
					var s:String = attempt(() -> data.getString(pos, len));
					if (inRange(pos, len) && s == null)
						fail('getString($pos, $len) threw');
					if (s != null)
						exercise(s);
				case 11:
					var hex = data.toHex();
					// The std compares the whole data on hxcpp, past length if a set grew it
					if (hex.length != size * 2 || (data.getData().length == size && Bytes.ofHex(hex).compare(data) != 0))
						fail('toHex then ofHex changed the bytes: $hex, ${data.getData().length} bytes of data for $size');
				case 12:
					var len = r.pos();
					var hex = attempt(() -> data.getString(pos, len));
					if (hex != null)
						attempt(() -> Bytes.ofHex(hex));
				case 13:
					var other = Bytes.alloc(r.byte());
					for (k in 0...other.length)
						other.set(k, r.byte());
					var c = data.compare(other);
					var expected = 0;
					for (k in 0...Std.int(Math.min(size, other.length)))
						if (model[k] != other.get(k)) {
							expected = model[k] - other.get(k);
							break;
						}
					if (expected == 0)
						expected = size - other.length;
					if (data.getData().length == size && ((c < 0) != (expected < 0) || (c == 0) != (expected == 0)))
						fail('compare gave $c, expected the sign of $expected');
				case 14:
					var input = new BytesInput(data, 0, size);
					attempt(() -> {
						input.position = pos;
						input.readLine();
						input.readInt32();
						input.readString(r.byte());
						input.readUntil(r.byte());
						input.readDouble();
						input.readFloat();
					});
				case 15:
					var buf = new BytesBuffer();
					attempt(() -> {
						buf.addByte(r.byte());
						buf.addInt32(r.int());
						buf.addDouble(r.double());
						buf.addBytes(data, pos, r.pos());
						buf.addString(data.toString());
					});
					buf.getBytes();
			}
			if (checked) {
				for (i in 0...size)
					if (data.get(i) != model[i])
						fail('after operation $op: byte $i is ${data.get(i)}, expected ${model[i]}');
			} else
				for (i in 0...size)
					model[i] = data.get(i);
		}
	}

	/**
		Unserialize, then serialize and unserialize twice more: the last two
		values must be the same. The first may differ, since some text, such
		as a NaN date or invalid UTF-8, settles after one round. Values are
		compared through canon(), since fields come out in hash order.
	**/
	static function serializer(b:Bytes) {
		if (nesting(b, "aolbqMcwj", "hg") > MAX_NESTING)
			return;
		// A reference can make a List that holds itself in a few bytes, and the
		// std's List.toString has no guard against one: the Unserializer turns
		// a class or enum name it was given into a String, so such a List as a
		// name prints until the stack overflows, on every target
		if (b.toString().indexOf("l") >= 0 && b.toString().indexOf("r") >= 0)
			return;
		// The std's Unserializer takes a negative string or bytes length
		// ("y-10:") as a step back, and parses the same text again until the
		// stack overflows, on every target
		if (~/[ys]-/.match(b.toString()))
			return;
		var v1 = attempt(() -> haxe.Unserializer.run(b.toString()));
		var s1:String = v1 == null ? null : attempt(() -> serialize(v1));
		var v2 = s1 == null ? null : attempt(() -> haxe.Unserializer.run(s1));
		var s2:String = v2 == null ? null : attempt(() -> serialize(v2));
		var v3 = s2 == null ? null : attempt(() -> haxe.Unserializer.run(s2));
		if (v3 != null && differ(v2, v3))
			fail('serialized $s1 then $s2');
	}

	/**
		With the cache, so a value that holds itself comes out as a reference:
		the input's references can make one in a few bytes ("oy1:ar0g"), and
		without the cache the std's Serializer recurses into it until the
		stack overflows, on every target.
	**/
	static function serialize(v:Dynamic):String {
		var s = new haxe.Serializer();
		s.useCache = true;
		s.serialize(v);
		return s.toString();
	}

	/** Parse and print three times: the last two parsed values must be the same, as for serializer. **/
	static function json(b:Bytes) {
		if (nesting(b, "[{", "]}") > MAX_NESTING)
			return;
		var s1:String = attempt(() -> haxe.Json.stringify(haxe.Json.parse(b.toString())));
		if (s1 == null)
			return;
		// The std prints U+0000 as it is and its parser takes it for the end of
		// the text on cpp, so a printed text may not read back; that is left out
		var v1 = attempt(() -> haxe.Json.parse(s1));
		if (v1 == null)
			return;
		var s2 = haxe.Json.stringify(v1);
		if (differ(v1, haxe.Json.parse(s2)))
			fail('printed $s1 then $s2');
	}

	/**
		A pattern and a subject: one byte of options, then the pattern up to
		a zero byte, then the subject. Every EReg method on the result, with
		positions that may be out of range, once a match over the whole
		subject has given an answer.
	**/
	static function ereg(b:Bytes) {
		if (b.length < 1)
			return;
		var flags = b.get(0);
		var opts = "";
		for (i => f in ["i", "g", "m", "s", "u"])
			if (flags & (1 << i) != 0)
				opts += f;
		var rest = b.sub(1, b.length - 1);
		var zero = 0;
		while (zero < rest.length && rest.get(zero) != 0)
			zero++;
		var pattern = rest.getString(0, zero);
		var subject = zero < rest.length ? rest.getString(zero + 1, rest.length - zero - 1) : "";
		var r:EReg = attempt(() -> new EReg(pattern, opts));
		if (r == null)
			return;
		var matched:Null<Bool> = attempt(() -> r.match(subject));
		// PCRE2 gives up on a match after its match limit, 10 million steps
		// from one start, and the EReg throws. A pattern such as "(\D+)*x"
		// backtracks through every way to split the subject, so each call over
		// the subject below would run to the limit again: five of them took
		// 12 seconds under the sanitizers
		if (matched == null)
			return;
		if (matched) {
			var p = r.matchedPos();
			if (p.pos < 0 || p.len < 0 || p.pos + p.len > subject.length)
				fail('matchedPos ${p.pos},${p.len} is outside a subject of length ${subject.length}');
			if (r.matched(0) != subject.substr(p.pos, p.len))
				fail('matched(0) "${r.matched(0)}" is not the subject at ${p.pos},${p.len}');
			if (r.matchedLeft() + r.matched(0) + r.matchedRight() != subject)
				fail("matchedLeft, matched(0) and matchedRight do not make the subject");
			for (i in 0...12)
				attempt(() -> r.matched(i));
		}
		var pos = flags >> 5;
		attempt(() -> r.matchSub(subject, pos - 2, (flags & 7) - 2));
		attempt(() -> r.matchSub(subject, pos));
		attempt(() -> r.split(subject));
		attempt(() -> r.replace(subject, "<$1|$0|$$|$9>"));
		attempt(() -> r.map(subject, e -> e.matched(0) + "|"));
		attempt(() -> EReg.escape(subject));
	}

	/** Std.parseInt and Std.parseFloat, whose results must read back from Std.string. **/
	static function number(b:Bytes) {
		var s = b.toString();
		var i = Std.parseInt(s);
		if (i != null && Std.parseInt(Std.string(i)) != i)
			fail('parseInt gave $i, which does not read back');
		var f = Std.parseFloat(s);
		if (Math.isFinite(f) && Std.parseFloat(Std.string(f)) != f)
			fail('parseFloat gave $f (${Std.string(f)}), which does not read back');
		Std.string(f);
		Std.int(f);
	}

	/** urlDecode of anything, and urlEncode then urlDecode giving the text back. **/
	static function url(b:Bytes) {
		var s = b.toString();
		var decoded = attempt(() -> StringTools.urlDecode(s));
		if (decoded != null)
			exercise(decoded);
		if (utf16Units(b) != null) {
			var back = StringTools.urlDecode(StringTools.urlEncode(s));
			if (back != s)
				fail('urlEncode then urlDecode gave "$back" for "$s"');
		}
	}

	/** Inflate anything, in one go and in small steps, and deflate then inflate the input back. **/
	static function zlib(b:Bytes) {
		attempt(() -> haxe.zip.Uncompress.run(b, 7));
		attempt(() -> {
			var u = new haxe.zip.Uncompress(b.length > 0 && b.get(0) & 1 == 1 ? -15 : 15);
			var out = Bytes.alloc(17);
			var pos = 0;
			for (step in 0...4096) {
				var r = u.execute(b, pos, out, step % 5);
				pos += r.read;
				if (r.done || (r.read == 0 && r.write == 0))
					break;
			}
			u.close();
		});
		var packed = haxe.zip.Compress.run(b, b.length > 0 ? b.get(0) % 10 : 6);
		if (haxe.zip.Uncompress.run(packed).compare(b) != 0)
			fail("deflate then inflate changed the bytes");
	}

	// --- Helpers ---------------------------------------------------------

	/**
		The std's Unserializer, Serializer and Json recurse once per level of
		nesting, and a few thousand levels overflow a 1 MB main thread stack on
		any target, so deeper input is left out rather than reported.
	**/
	static inline var MAX_NESTING = 200;

	/** How deep the openers nest, counting every one, even inside strings. **/
	static function nesting(b:Bytes, openers:String, closers:String):Int {
		var depth = 0;
		var max = 0;
		for (i in 0...b.length) {
			var c = b.get(i);
			if (openers.indexOf(String.fromCharCode(c)) >= 0 && ++depth > max)
				max = depth;
			else if (closers.indexOf(String.fromCharCode(c)) >= 0 && depth > 0)
				depth--;
		}
		return max;
	}

	/** Runs f and returns its result, or null if it threw. **/
	static function attempt<T>(f:() -> T):Null<T> {
		try {
			return f();
		} catch (e:Dynamic) {
			return null;
		}
	}

	/** The String functions that walk or copy the text. **/
	static function exercise(s:String) {
		var n = s.length;
		for (i in 0...n)
			s.charCodeAt(i);
		s.charCodeAt(-1);
		s.charCodeAt(n);
		s.toUpperCase();
		s.toLowerCase();
		var half = s.substr(n >> 1);
		if (s.indexOf(half) < 0 || s.lastIndexOf(half) < 0)
			fail("a string does not contain its own second half");
		s.split(half.substr(0, 1));
		s.substring(n, -1);
		(s + half).length;
		StringTools.urlEncode(s);
		StringTools.htmlEscape(s, true);
		StringTools.trim(s);
		var m = new Map<String, Int>();
		m.set(s, 1);
		if (m.get(s) != 1)
			fail("a string is not found as a map key");
		Bytes.ofString(s);
		haxe.crypto.Md5.encode(s);
	}

	/**
		Whether two values have different texts from canon(). A value that
		takes more than CANON_WORK steps is not compared: the serializer's
		references can share a value at every level, as "ay1:aoy0:r0gr0h"
		does, and the depth cut-off alone leaves 2^32 steps.
	**/
	static function differ(a:Dynamic, b:Dynamic):Bool {
		try {
			canonLeft = CANON_WORK;
			var ta = canon(a);
			canonLeft = CANON_WORK;
			return ta != canon(b);
		} catch (e:CanonTooBig) {
			return false;
		}
	}

	static inline var CANON_WORK = 100000;
	static var canonLeft = 0;

	/**
		A text for a value that does not depend on the order fields and map
		keys come out in. Shared and cyclic values from the serializer's
		references are cut off below a depth.
	**/
	static function canon(v:Dynamic, depth = 0):String {
		if (--canonLeft < 0)
			throw new CanonTooBig();
		if (depth > 32)
			return "...";
		function sorted(items:Array<String>) {
			items.sort(Reflect.compare);
			return items.join(",");
		}
		return switch (Type.typeof(v)) {
			case TNull: "null";
			case TInt | TBool: Std.string(v);
			case TFloat: Math.isNaN(v) ? "NaN" : Std.string(v);
			case TObject:
				"{" + sorted([for (f in Reflect.fields(v)) haxe.Json.stringify(f) + ":" + canon(Reflect.field(v, f), depth + 1)]) + "}";
			case TEnum(e):
				Type.getEnumName(e) + "." + Type.enumConstructor(v) + "(" + [for (p in Type.enumParameters(v)) canon(p, depth + 1)].join(",") + ")";
			case TClass(String): haxe.Json.stringify(v);
			case TClass(Array): "[" + [for (x in (v : Array<Dynamic>)) canon(x, depth + 1)].join(",") + "]";
			// The std's cpp Date keeps seconds, so a fraction of a millisecond drifts
			case TClass(Date): "Date(" + Math.round((v : Date).getTime()) + ")";
			case TClass(haxe.io.Bytes): "Bytes(" + (v : Bytes).toHex() + ")";
			case TClass(List): "List(" + [for (x in (v : List<Dynamic>)) canon(x, depth + 1)].join(",") + ")";
			case TClass(c) if (Std.isOfType(v, haxe.Constraints.IMap)):
				var m:haxe.Constraints.IMap<Dynamic, Dynamic> = v;
				Type.getClassName(c) + "{" + sorted([for (k in m.keys()) canon(k, depth + 1) + "=>" + canon(m.get(k), depth + 1)]) + "}";
			case TClass(c):
				Type.getClassName(c) + "{" + sorted([for (f in Reflect.fields(v)) f + ":" + canon(Reflect.field(v, f), depth + 1)]) + "}";
			case TFunction: "function";
			case TUnknown: "unknown";
		}
	}

	/** The UTF-16 code units of valid UTF-8, or null if it is not valid. **/
	static function utf16Units(b:Bytes):Null<Array<Int>> {
		var units = [];
		var i = 0;
		var n = b.length;
		while (i < n) {
			var c = b.get(i);
			var len, cp, min;
			if (c < 0x80) {
				units.push(c);
				i++;
				continue;
			} else if ((c & 0xE0) == 0xC0) {
				len = 2;
				cp = c & 0x1F;
				min = 0x80;
			} else if ((c & 0xF0) == 0xE0) {
				len = 3;
				cp = c & 0x0F;
				min = 0x800;
			} else if ((c & 0xF8) == 0xF0) {
				len = 4;
				cp = c & 0x07;
				min = 0x10000;
			} else
				return null;
			if (i + len > n)
				return null;
			for (k in 1...len) {
				var d = b.get(i + k);
				if ((d & 0xC0) != 0x80)
					return null;
				cp = (cp << 6) | (d & 0x3F);
			}
			if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
				return null;
			if (cp >= 0x10000) {
				units.push(0xD800 + ((cp - 0x10000) >> 10));
				units.push(0xDC00 + ((cp - 0x10000) & 0x3FF));
			} else
				units.push(cp);
			i += len;
		}
		return units;
	}

	static function hasLoneSurrogate(units:Array<Int>):Bool {
		var i = 0;
		while (i < units.length) {
			var u = units[i];
			if (u >= 0xD800 && u <= 0xDBFF) {
				if (i + 1 >= units.length || units[i + 1] < 0xDC00 || units[i + 1] > 0xDFFF)
					return true;
				i += 2;
			} else if (u >= 0xDC00 && u <= 0xDFFF)
				return true;
			else
				i++;
		}
		return false;
	}

	/** A broken invariant: report it and abort, which libFuzzer and the replay both count as a failure. **/
	static function fail(message:String) {
		Sys.stderr().writeString('Fuzz: $message\n');
		Sys.stderr().flush();
		untyped __cpp__('abort()');
	}
}

/** Reads operands from the fuzz input; past its end every read gives 0. **/
private class Reader {
	final b:Bytes;
	var i = 0;

	public function new(b:Bytes) {
		this.b = b;
	}

	public function done()
		return i >= b.length;

	public function byte():Int
		return i < b.length ? b.get(i++) : 0;

	/** A position or length, mostly small, sometimes out of range or negative. **/
	public function pos():Int {
		var v = byte();
		return v < 0xf0 ? v : (v - 0xf8) * 1000;
	}

	public function int():Int
		return byte() | (byte() << 8) | (byte() << 16) | (byte() << 24);

	public function double():Float {
		var v = Bytes.alloc(8);
		for (k in 0...8)
			v.set(k, byte());
		return v.getDouble(0);
	}
}

/** Thrown by Fuzz.canon for a value too big to compare. **/
private class CanonTooBig {
	public function new() {}
}
