version 1.0
# this file has been automatically generated by the OpenQL compiler please do not modify it manually.
qubits 16

.kernel_locals
    { x q[12] | x q[8] | x q[4] | x q[0] }
    { cnot q[12],q[13] | cnot q[8],q[9] | cnot q[4],q[5] | cnot q[0],q[1] }
    wait 4
    { x q[13] | x q[9] | x q[5] | x q[1] }
