// Compiled into the host. The script has its own copy, which cppia ignores
// because the host has these enums, so script code uses the host's.
enum HostA {
	HostOnlyA;
}

enum HostB {
	HostOnlyB;
	HostWith(n:Int);
}
