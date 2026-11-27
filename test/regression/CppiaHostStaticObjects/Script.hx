class Script {
	static function main() {
		HostStatics.count = 0;
		var first = 0;
		var sum = 0;
		var expected = 0;
		for (i in 0...20000) {
			// Read through the host statics on every pass
			sum += HostStatics.items[0];
			expected += first;
			if (i % 5000 == 0) {
				// Young objects, stored where only the host's statics hold them
				HostStatics.obj = new HostNode(-i);
				var captured = i;
				HostStatics.fn = () -> captured;
				HostStatics.items = [i, i + 1];
				HostStatics.node = new HostNode(i);
				HostStatics.count++;
				first = i;
				cpp.vm.Gc.run(false);
			}
		}
		cpp.vm.Gc.run(false);
		cpp.vm.Gc.run(true);
		var scriptOk = (HostStatics.obj : HostNode).value == -15000 && HostStatics.fn() == 15000 && HostStatics.items[1] == 15001
			&& HostStatics.node.value == 15000 && sum == expected;
		Sys.println('script reads its stores: $scriptOk, host reads them: ${HostStatics.check()}');
	}
}
