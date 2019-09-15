pragma solidity >=0.5.0;

contract Partial {
    uint x;
    uint y;

    /// @notice modifies x
    /// @notice postcondition x == __verifier_old_uint(x) + 1
    function unsupported() internal {
        assembly {
            let t := sload(x_slot)
            t := add(t, 1)
            sstore(x_slot, t)
        }
    }

    // Last postcondition should fail
    /// @notice modifies x
    /// @notice modifies y
    /// @notice modifies address(this).balance
    /// @notice postcondition x != y
    function() external payable {
        x = 1;
        y = 2;
        unsupported();
        assert(x == y); // Should hold
    }
}
