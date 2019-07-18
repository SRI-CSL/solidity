pragma solidity >=0.5.0;

contract A {
    int public x;

    constructor(int _x) public { x = _x; }
}

contract BaseConstructorAndModifiers is A {
    modifier m {
        x++;
        _;
    }

    // Will call A() first, then the two modifiers
    constructor() m A(1) m public {
        assert(x == 3);
    }

    function() external payable { } // Needed for detecting as a truffle test case
}