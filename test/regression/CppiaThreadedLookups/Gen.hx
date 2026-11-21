import haxe.macro.Context;
import haxe.macro.Expr;

// Defines the script's classes C0 to C<n-1>, each with a constructor
class Gen {
	public static function classes(n:Int) {
		var pos = Context.currentPos();
		for (i in 0...n) {
			var ctor:Field = {name: "new", access: [APublic], kind: FFun({args: [], expr: macro {}}), pos: pos};
			Context.defineType({pack: [], name: 'C$i', pos: pos, kind: TDClass(), fields: [ctor], meta: [{name: ":keep", pos: pos}]});
		}
	}
}
