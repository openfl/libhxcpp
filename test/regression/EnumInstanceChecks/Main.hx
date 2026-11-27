// A typed catch of one enum caught a value of any enum, and Std.isOfType(v, E),
// v is E and cast(v, E) took one too: a generated enum has no class id of its
// own, so hx::ObjectPtr's cast and TCanCast tested the id every enum value has.
// The expected output is what Haxe's own interpreter prints. openfl/libhxcpp#491
import haxe.ds.Either;
import haxe.ds.Option;

enum A {
	OnlyA;
}

enum B {
	OnlyB;
	WithValue(n:Int);
}

enum Holds<T> {
	Held(v:T);
}

class Thing {
	public function new() {}
}

class Main {
	static function aThenB(thrower:Void->Void):String {
		return try {
			thrower();
			"nothing";
		} catch (e:A) {
			"the A branch";
		} catch (e:B) {
			"the B branch";
		} catch (e:Dynamic) {
			"the Dynamic branch";
		}
	}

	static function bThenA(thrower:Void->Void):String {
		return try {
			thrower();
			"nothing";
		} catch (e:B) {
			"the B branch";
		} catch (e:A) {
			"the A branch";
		} catch (e:Dynamic) {
			"the Dynamic branch";
		}
	}

	static function onlyA(thrower:Void->Void):String {
		return try {
			try {
				thrower();
				"nothing";
			} catch (e:A) {
				"the A branch";
			}
		} catch (e:Dynamic) {
			"passed the A catch";
		}
	}

	static function aThenException(thrower:Void->Void):String {
		return try {
			thrower();
			"nothing";
		} catch (e:A) {
			"the A branch";
		} catch (e:haxe.Exception) {
			"the Exception branch with " + e.message;
		}
	}

	static function aThenClass(thrower:Void->Void):String {
		return try {
			thrower();
			"nothing";
		} catch (e:A) {
			"the A branch";
		} catch (e:Thing) {
			"the Thing branch";
		} catch (e:Dynamic) {
			"the Dynamic branch with " + Std.string(e);
		}
	}

	static function attempt(label:String, f:Void->Dynamic) {
		try {
			Sys.println(label + " = " + Std.string(f()));
		} catch (e:Dynamic) {
			Sys.println(label + " throws");
		}
	}

	static function main() {
		Sys.println("catches");
		Sys.println("  throw B.OnlyB, A then B: " + aThenB(() -> throw B.OnlyB));
		Sys.println("  throw B.WithValue(1), A then B: " + aThenB(() -> throw B.WithValue(1)));
		Sys.println("  throw A.OnlyA, A then B: " + aThenB(() -> throw A.OnlyA));
		Sys.println("  throw A.OnlyA, B then A: " + bThenA(() -> throw A.OnlyA));
		Sys.println("  throw B.OnlyB, B then A: " + bThenA(() -> throw B.OnlyB));
		Sys.println("  throw B.OnlyB, A alone: " + onlyA(() -> throw B.OnlyB));
		Sys.println("  throw A.OnlyA, A alone: " + onlyA(() -> throw A.OnlyA));
		Sys.println("  throw a String, A then B: " + aThenB(() -> throw "text"));
		Sys.println("  throw Some(1), A then B: " + aThenB(() -> throw Some(1)));
		Sys.println("  throw Held(1), A then B: " + aThenB(() -> throw Held(1)));
		var held:Dynamic = B.WithValue(2);
		Sys.println("  throw a Dynamic B, A then B: " + aThenB(() -> throw held));
		Sys.println("  throw B.OnlyB, A then Exception: " + aThenException(() -> throw B.OnlyB));
		Sys.println("  throw A.OnlyA, A then Exception: " + aThenException(() -> throw A.OnlyA));
		Sys.println("  throw B.OnlyB, A then Thing: " + aThenClass(() -> throw B.OnlyB));
		Sys.println("  throw a Thing, A then Thing: " + aThenClass(() -> throw new Thing()));

		Sys.println("Std.isOfType");
		Sys.println("  B.OnlyB, A: " + Std.isOfType(B.OnlyB, A));
		Sys.println("  B.OnlyB, B: " + Std.isOfType(B.OnlyB, B));
		Sys.println("  B.WithValue(1), A: " + Std.isOfType(B.WithValue(1), A));
		Sys.println("  B.WithValue(1), B: " + Std.isOfType(B.WithValue(1), B));
		Sys.println("  A.OnlyA, A: " + Std.isOfType(A.OnlyA, A));
		Sys.println("  A.OnlyA, B: " + Std.isOfType(A.OnlyA, B));
		Sys.println("  Dynamic B, A: " + Std.isOfType(held, A));
		Sys.println("  Dynamic B, B: " + Std.isOfType(held, B));
		Sys.println("  a String, A: " + Std.isOfType("text", A));
		Sys.println("  a Thing, A: " + Std.isOfType(new Thing(), A));
		Sys.println("  A.OnlyA, Thing: " + Std.isOfType(A.OnlyA, Thing));
		Sys.println("  null, A: " + Std.isOfType(null, A));

		Sys.println("enums with type parameters");
		var some:Option<Int> = Some(1);
		var none:Option<String> = None;
		var left:Either<Int, String> = Left(1);
		Sys.println("  Some(1), Option: " + Std.isOfType(some, Option));
		Sys.println("  None, Option: " + Std.isOfType(none, Option));
		Sys.println("  Some(1), Either: " + Std.isOfType(some, Either));
		Sys.println("  Left(1), Either: " + Std.isOfType(left, Either));
		Sys.println("  Left(1), Option: " + Std.isOfType(left, Option));
		Sys.println("  Some(1), A: " + Std.isOfType(some, A));
		Sys.println("  A.OnlyA, Option: " + Std.isOfType(A.OnlyA, Option));
		Sys.println("  Held(1), Holds: " + Std.isOfType(Held(1), Holds));
		Sys.println("  Held(1), B: " + Std.isOfType(Held(1), B));

		Sys.println("is");
		Sys.println("  B.OnlyB is A: " + (B.OnlyB is A));
		Sys.println("  B.OnlyB is B: " + (B.OnlyB is B));
		Sys.println("  Dynamic B is A: " + (held is A));
		Sys.println("  Dynamic B is B: " + (held is B));
		Sys.println("  Some(1) is Option: " + (some is Option));
		Sys.println("  Some(1) is A: " + (some is A));

		Sys.println("safe casts");
		var onlyB:Dynamic = B.OnlyB;
		var onlyA:Dynamic = A.OnlyA;
		var seven:Dynamic = 7;
		var thing:Dynamic = new Thing();
		attempt("  B.OnlyB to A", () -> cast(onlyB, A));
		attempt("  B.OnlyB to B", () -> cast(onlyB, B));
		attempt("  A.OnlyA to A", () -> cast(onlyA, A));
		attempt("  A.OnlyA to B", () -> cast(onlyA, B));
		attempt("  Dynamic B.WithValue(2) to A", () -> cast(held, A));
		attempt("  Dynamic B.WithValue(2) to B", () -> cast(held, B));
		attempt("  Some(1) to Option", () -> cast(some, Option<Dynamic>));
		attempt("  Some(1) to A", () -> cast(some, A));
		attempt("  7 to A", () -> cast(seven, A));
		attempt("  a Thing to A", () -> cast(thing, A));
		attempt("  A.OnlyA to Thing", () -> cast(onlyA, Thing));

		Sys.println("Enum and Type.getEnum");
		Sys.println("  B.OnlyB, Enum: " + Std.isOfType(B.OnlyB, Enum));
		Sys.println("  B, Enum: " + Std.isOfType(B, Enum));
		Sys.println("  Option, Enum: " + Std.isOfType(Option, Enum));
		Sys.println("  getEnum(B.OnlyB) == B: " + (Type.getEnum(B.OnlyB) == B));
		Sys.println("  getEnum(B.OnlyB) == A: " + (Type.getEnum(B.OnlyB) == cast A));
		Sys.println("  getEnum(Some(1)): " + Type.getEnumName(Type.getEnum(some)));
		Sys.println("  getEnum(Held(1)): " + Type.getEnumName(Type.getEnum(Held(1))));
	}
}
