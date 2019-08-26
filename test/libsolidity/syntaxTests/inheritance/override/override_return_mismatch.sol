contract A {
	int public testvar;
	function foo() internal returns (uint256);
}
contract B {
	function foo() internal returns (uint8);
	function test() internal returns (uint256);
}
contract X is A, B {
	int public override testvar;
	function test() internal override returns (uint256);
}
// ----
// TypeError: (182-288): Ambiguous base function foo() must be overridden.
