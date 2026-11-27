package;

/** These classes are compiled only to CPPIA, never into the native host. **/
class Script {
	static function main() {
		CppiaBridge.workload = switch (CppiaBridge.name) {
			case "int_loop": new IntLoop();
			case "float_array": new FloatArray();
			case "alloc_objects": new AllocObjects();
			case "virtual_call": new VirtualCall();
			case "closure_call": new ClosureCall();
			case "native_call": new NativeCall();
			case "native_callback": new NativeCallback();
			case "host_statics": new HostStatics();
			default: throw 'Unknown CPPIA workload ${CppiaBridge.name}';
		};
	}
}

class IntLoop implements CppiaWorkload {
	public function new() {}

	public function run():Int {
		var value = 17;
		var sum = 0;
		for (i in 0...500000) {
			value = (value * 13 + i) & 65535;
			if ((value & 1) == 0)
				sum ^= value;
			else
				sum ^= value << 1;
		}
		return sum;
	}
}

class FloatArray implements CppiaWorkload {
	public function new() {}

	public function run():Int {
		var values = [for (i in 0...512) i * 0.5];
		var sum = 0;
		for (i in 0...200000) {
			var slot = i & 511;
			var value = values[slot] * 0.5 + (i & 255) * 0.25;
			values[slot] = value;
			sum += Std.int(value * 16);
		}
		return sum;
	}
}

class ScriptObject {
	public var value:Int;
	public var next:ScriptObject;

	public function new(value:Int, next:ScriptObject) {
		this.value = value;
		this.next = next;
	}
}

class AllocObjects implements CppiaWorkload {
	public function new() {}

	public function run():Int {
		var live:Array<ScriptObject> = [];
		live.resize(1024);
		var sum = 0;
		for (i in 0...100000) {
			var slot = i & 1023;
			// Keep a bounded live graph as well as short-lived script instances.
			var obj = new ScriptObject(i, live[slot] == null ? null : live[slot].next);
			if ((i & 3) == 0)
				obj.next = new ScriptObject(i ^ 0x1234, null);
			live[slot] = obj;
			sum += obj.value & 1023;
		}
		for (obj in live) {
			sum += obj.value;
			if (obj.next != null)
				sum += obj.next.value;
		}
		return sum;
	}
}

class ScriptOperation {
	public function new() {}
	public function apply(value:Int):Int {
		return value + 3;
	}
}

class OtherOperation extends ScriptOperation {
	override public function apply(value:Int):Int {
		return value ^ 0x1357;
	}
}

class VirtualCall implements CppiaWorkload {
	final operations:Array<ScriptOperation> = [new ScriptOperation(), new OtherOperation()];
	public function new() {}

	public function run():Int {
		var sum = 0;
		for (i in 0...200000)
			sum += operations[i & 1].apply(i & 1023);
		return sum;
	}
}

class ClosureCall implements CppiaWorkload {
	public function new() {}

	public function run():Int {
		var bias = 7;
		var callback = function(value:Int):Int {
			bias = (bias + 3) & 1023;
			return (value & 1023) + bias;
		};
		var sum = 0;
		for (i in 0...200000)
			sum += callback(i);
		return sum;
	}
}

class NativeCall implements CppiaWorkload {
	public function new() {}

	public function run():Int {
		var sum = 0;
		for (i in 0...200000)
			sum += CppiaBridge.step(i & 1023);
		return sum;
	}
}

class NativeCallback implements CppiaWorkload {
	public function new() {}

	public function run():Int {
		var bias = 7;
		return CppiaBridge.repeat(200000, function(value:Int):Int {
			bias = (bias + 3) & 1023;
			return (value & 1023) + bias;
		});
	}
}

class HostStatics implements CppiaWorkload {
	public function new() {}

	public function run():Int {
		var sum = 0;
		for (i in 0...200000) {
			// A read and a write of host static objects each pass
			var values = CppiaBridge.values;
			sum += values[i & 15];
			CppiaBridge.last = values;
		}
		return sum;
	}
}
