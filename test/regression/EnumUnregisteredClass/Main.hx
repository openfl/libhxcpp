// An extern enum written by hand in C++, as Haxe's unit.issues.Issue10830
// does, never registers its class, so its __mClass stays null. 6.0.2's
// enum checks compared a value's class with that null and failed: the switch
// below threw "Invalid Cast". Such an enum takes any enum value, as before 6.0.2.
@:native('customnamespace::MyExternEnum')
extern enum MyExternEnum {
	First;
	Second(v:String);
}

enum Generated {
	One;
	Two;
}

enum Other {
	Three;
}

@:headerCode('
HX_DECLARE_CLASS1(customnamespace, MyExternEnum)

namespace customnamespace {
	class MyExternEnum_obj : public hx::EnumBase_obj {
	public:
		typedef MyExternEnum_obj OBJ_;

		enum Type {
			TFirst,
			TSecond
		};

		MyExternEnum_obj() = default;

		HX_DO_ENUM_RTTI;

		static bool __GetStatic(const String&, Dynamic&, hx::PropertyAccess);

		String GetEnumName() const { return HX_CSTRING("MyExternEnum"); }
		String __ToString() const { return HX_CSTRING("MyExternEnum.") + _hx_tag; }

		static MyExternEnum First();
		static Dynamic First_dyn();

		static MyExternEnum Second(String);
		static Dynamic Second_dyn();
	};
}
')
@:cppFileCode('
namespace customnamespace {
	MyExternEnum MyExternEnum_obj::First() {
		return hx::CreateEnum<MyExternEnum_obj>(HX_CSTRING("First"), Type::TFirst, 0);
	}

	MyExternEnum MyExternEnum_obj::Second(String v) {
		return hx::CreateEnum<MyExternEnum_obj>(HX_CSTRING("Second"), Type::TSecond, 1)->_hx_init(0, v);
	}

	bool MyExternEnum_obj::__GetStatic(const String& _inName, Dynamic& _outValue, hx::PropertyAccess _propAccess) {
		if (_inName == HX_CSTRING("First")) { _outValue = MyExternEnum_obj::First_dyn(); return true; }
		if (_inName == HX_CSTRING("Second")) { _outValue = MyExternEnum_obj::Second_dyn(); return true; }
		return hx::EnumBase_obj::__GetStatic(_inName, _outValue, _propAccess);
	}

	hx::Val MyExternEnum_obj::__Field(const String& _inName, hx::PropertyAccess _propAccess) {
		if (_inName == HX_CSTRING("First")) { return MyExternEnum_obj::First_dyn(); }
		if (_inName == HX_CSTRING("Second")) { return MyExternEnum_obj::Second_dyn(); }
		return hx::EnumBase_obj::__Field(_inName, _propAccess);
	}

	int MyExternEnum_obj::__FindIndex(String _inName) {
		if (_inName == HX_CSTRING("First")) { return Type::TFirst; }
		if (_inName == HX_CSTRING("Second")) { return Type::TSecond; }
		return hx::EnumBase_obj::__FindIndex(_inName);
	}

	int MyExternEnum_obj::__FindArgCount(String _inName) {
		if (_inName == HX_CSTRING("First")) { return 0; }
		if (_inName == HX_CSTRING("Second")) { return 1; }
		return hx::EnumBase_obj::__FindArgCount(_inName);
	}

	HX_DEFINE_CREATE_ENUM(MyExternEnum_obj)

	STATIC_HX_DEFINE_DYNAMIC_FUNC0(MyExternEnum_obj, First, return)

	STATIC_HX_DEFINE_DYNAMIC_FUNC1(MyExternEnum_obj, Second, return)

	hx::Class MyExternEnum_obj::__mClass;
}
')
class Main {
	static function create():MyExternEnum {
		return untyped __cpp__('::customnamespace::MyExternEnum_obj::Second(HX_CSTRING("Hello, World!"))');
	}

	static function main() {
		switch create() {
			case First:
				Sys.println("switch: First");
			case Second(v):
				Sys.println("switch: Second " + v);
		}

		var held:Dynamic = create();
		var back:MyExternEnum = cast held;
		Sys.println("cast from Dynamic: " + (back != null));

		try {
			throw create();
		} catch (e:MyExternEnum) {
			Sys.println("caught as MyExternEnum");
		} catch (e:Dynamic) {
			Sys.println("caught as Dynamic");
		}

		// Registered enums still tell each other apart
		try {
			throw One;
		} catch (e:Other) {
			Sys.println("Generated.One caught as Other");
		} catch (e:Generated) {
			Sys.println("Generated.One caught as Generated");
		}
	}
}
