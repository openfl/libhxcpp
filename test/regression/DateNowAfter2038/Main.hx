@:buildXml('<files id="runtime"><compilerflag value="/FI${HXCPP}/test/regression/DateNowAfter2038/FutureClock.h" if="isMsvc"/></files><files id="haxe"><compilerflag value="/FI${HXCPP}/test/regression/DateNowAfter2038/FutureClock.h" if="isMsvc"/></files>')
@:cppFileCode('
static bool futureClockActive() {
#ifdef HXCPP_TEST_FUTURE_CLOCK
	return true;
#else
	return false;
#endif
}
')
class Main {
	static function main() {
		var exact = true;
		var year = true;
		var control = true;
		if (untyped __cpp__("futureClockActive()")) {
			var expected = 2208988800125.0;
			var date = Date.now();
			exact = date.getTime() == expected;
			year = date.getUTCFullYear() == 2040;
			control = Date.fromTime(expected).getUTCFullYear() == 2040;
			if (Sys.args().length > 0)
				Sys.println(haxe.Json.stringify({actual: date.getTime(), expected: expected, utcYear: date.getUTCFullYear()}));
		}
		Sys.println("Date.now preserves the simulated 2040 timestamp: " + exact);
		Sys.println("Date.now returns the simulated UTC year: " + year);
		Sys.println("explicit post-2038 dates remain correct: " + control);
	}
}
