import haxe.Json;
import sys.io.File;

using StringTools;

/**
   Checks and stamps the version a release is made from.

   The version is the one written in haxelib.json, used as it is: nothing is
   counted up. The top section of Changes.md has to be headed with the same
   version, so a release is always a deliberate commit with its notes.

   Usage, from the repository root:

      haxe -p tools/release --run Release check
         Validates the version and the Changes.md heading, then prints
         version=<version> and prerelease=<true|false>.

      haxe -p tools/release --run Release stamp
         Does what check does, then writes include/HxcppVersion.h and
         extraParams.hxml for the version.

      haxe -p tools/release --run Release notes <file>
         Writes the body of the top Changes.md section to <file>.
**/
class Release
{
   // The version format haxelib accepts: x.y.z with an optional alpha, beta,
   // preview or rc tag, which may be numbered.
   static final VERSION = ~/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(-(alpha|beta|preview|rc)(\.(0|[1-9]\d*))?)?$/;

   static final HEADING_RULE = ~/^-{3,}$/;

   public static function main()
   {
      try
      {
         switch Sys.args()
         {
            case [ "check" ]:
               report(checkedVersion());
            case [ "stamp" ]:
               final version = checkedVersion();
               stamp(version);
               report(version);
            case [ "notes", file ]:
               File.saveContent(file, topSection().body);
            case _:
               fail("Usage: Release check | stamp | notes <file>");
         }
      }
      catch (e:ReleaseError)
      {
         fail(e.message);
      }
   }

   static function checkedVersion():String
   {
      final version:String = Json.parse(File.getContent("haxelib.json")).version;
      if (version == null || !VERSION.match(version))
         throw new ReleaseError('haxelib.json\'s version "$version" is not one haxelib accepts (x.y.z, or x.y.z-alpha|beta|preview|rc[.n])');

      final heading = topSection().heading;
      final expected = 'libhxcpp $version';
      if (heading != expected)
         throw new ReleaseError('The top section of Changes.md is headed "$heading"; a release of $version needs "$expected"');

      return version;
   }

   static function isPrerelease(version:String)
   {
      return version.indexOf("-") >= 0;
   }

   static function report(version:String)
   {
      Sys.println('version=$version');
      Sys.println('prerelease=${ isPrerelease(version) }');
   }

   static function stamp(version:String)
   {
      File.saveContent("include/HxcppVersion.h", [
         '#ifndef HXCPP_VERSION',
         '#define HXCPP_VERSION "$version"',
         '#endif',
         '#ifndef LIBHXCPP_VERSION',
         '#define LIBHXCPP_VERSION "$version"',
         '#endif',
         ''
      ].join("\n"));

      // Builds that use the library get -D libhxcpp=<version>, so code can tell
      // it is on libhxcpp and compare with version("x.y.z"), and -D hxcpp, so code
      // that checks for hxcpp works the same whether it asked for hxcpp or libhxcpp.
      // A macro rather than -D lines: tools such as hxp take the line before a
      // library's -D line in `haxelib path` output as its path.
      File.saveContent("extraParams.hxml", [for (name in ["libhxcpp", "hxcpp"])
         '--macro haxe.macro.Compiler.define("$name","$version")\n'].join(""));
   }

   // The first section of Changes.md: a heading line underlined with dashes,
   // and everything up to the next such heading.
   static function topSection():{ heading:String, body:String }
   {
      final lines = File.getContent("Changes.md").replace("\r", "").split("\n");
      final headings = [ for (i in 1...lines.length) if (HEADING_RULE.match(lines[i]) && lines[i - 1].trim() != "") i - 1 ];
      if (headings.length == 0)
         throw new ReleaseError("Changes.md has no section headings");

      final start = headings[0];
      final end = headings.length > 1 ? headings[1] : lines.length;
      return {
         heading: lines[start].trim(),
         body: lines.slice(start + 2, end).join("\n").trim() + "\n"
      };
   }

   static function fail(message:String)
   {
      Sys.stderr().writeString('Release: $message\n');
      Sys.exit(1);
   }
}

class ReleaseError extends haxe.Exception {}
