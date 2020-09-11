// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.7.0;

contract SpecialMembers {
    int x;
    function() external payable {
        uint now1 = now;
        uint bn1 = block.number;
        x++;
        uint now2 = now;
        uint bn2 = block.number;

        assert(now1 == now2);
        assert(bn1 == bn2);
    }
}