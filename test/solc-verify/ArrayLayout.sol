pragma solidity >=0.5.0;

contract ArrayLayout {
  
  struct S {
    int x;
  }

  S[] m_a;

  function() external payable {
    S[] memory a;
    S memory f = S(1);
 
    a = new S[](2);
    a[0] = f;
    a[1] = f;
    // Both array elements are memory references
    assert(a[0].x == a[1].x);
 
    a[0].x = 2;
    // Since they are both references, they are still same
    assert(a[0].x == a[1].x);

    m_a.push(S(1));
    m_a.push(m_a[0]);
    // Storage arrays are no-alias ATDs, no references, copying
    assert(m_a[0].x == m_a[1].x);
    
    m_a[0].x = 2;
    // We change copies, so differenct
    assert(m_a[0].x != m_a[1].x);

    S storage a0 = m_a[0];
    S storage a1 = m_a[1];
    a0 = a1; // This assignment is just reference assignment
    a0.x = 3;
    assert(m_a[0].x == 2);
    assert(m_a[1].x == 3);
  }
}
