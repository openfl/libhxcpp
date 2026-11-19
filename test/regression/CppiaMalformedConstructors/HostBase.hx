// A host class for the script's classes to extend. The script is compiled with
// its own copy, which the host's replaces when it loads
class HostBase {
	public var made:String;

	public function new(n:Int) {
		made = #if cppia 'script\'s HostBase $n' #else 'HostBase $n' #end;
	}
}
