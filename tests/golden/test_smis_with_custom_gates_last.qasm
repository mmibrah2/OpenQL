version 1.0
# this file has been automatically generated by the OpenQL compiler please do not modify it manually.
qubits 7

.aKernel
    { prepz q[0] | y q[1] | z q[5] | ym90 q[3] }
    wait 1
    x q[0]
    wait 1
    rx90 q[0]
    wait 1
    cz q[0],q[3]
    wait 3
    { ry90 q[3] | measure q[0] }
    wait 1
