class Main {
	static function main() {
		var ascii = Sys.args().length == 0 ? "abc" : Sys.args()[0];
		Sys.println("whole string clamped: " + (ascii.substr(0, 0x7fffffff) == ascii));
		Sys.println("positive start clamped: " + (ascii.substr(1, 0x7fffffff) == ascii.substr(1)));
		var wide = "αβγ";
		Sys.println("UTF-16 length clamped: " + (wide.substr(1, 0x7fffffff) == "βγ"));
	}
}
