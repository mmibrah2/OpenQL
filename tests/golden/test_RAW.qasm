.aKernel
   prepz q0 |    prepz q1 |    prepz q2 |    prepz q3
   cnot q0, q1
   nop
   nop
   nop
   cnot q1, q2 |    measure q0
   nop
   nop
   nop
   measure q1