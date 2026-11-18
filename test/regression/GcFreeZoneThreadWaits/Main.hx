import cpp.vm.Gc;
import sys.thread.Deque;
import sys.thread.Lock;
import sys.thread.Semaphore;
import sys.thread.Thread;

// Lock, Semaphore and Deque left a GC-free zone their caller had entered when
// they blocked: they entered a zone and left it, and the thread's zone state is
// a flag, not a depth. The caller's own exit then ended the process with
// "GCFree Zone mismatch". Mutex and Condition already kept the caller's zone.
// Everything is allocated before the zone is entered.
function inZone(name:String, wait:() -> Void) {
	Gc.enterGCFreeZone();
	wait();
	Gc.exitGCFreeZone();
	Sys.println(name + ": the caller's zone was kept");
}

function later(action:() -> Void) {
	Thread.create(() -> {
		Sys.sleep(0.05);
		action();
	});
}

function main() {
	var unreleased = new Lock();
	inZone("Lock.wait with a timeout", () -> unreleased.wait(0.01));
	var lock = new Lock();
	later(() -> lock.release());
	inZone("Lock.wait", () -> lock.wait());

	var empty = new Semaphore(0);
	inZone("Semaphore.tryAcquire with a timeout", () -> empty.tryAcquire(0.01));
	var semaphore = new Semaphore(0);
	later(() -> semaphore.release());
	inZone("Semaphore.acquire", () -> semaphore.acquire());

	var deque = new Deque<Int>();
	later(() -> deque.add(1));
	inZone("Deque.pop", () -> deque.pop(true));

	Gc.run(true);
	Sys.println("collection works afterwards");
}
