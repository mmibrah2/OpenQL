version 1.0
# this file has been automatically generated by the OpenQL compiler please do not modify it manually.
qubits 7

.init_0
    prepz q[0]
    prepz q[1]
    wait 1

.do_somework_1_for24_start
    ldi r29, 3
    ldi r30, 1
    ldi r31, 0

.do_somework_1

.do_somework_1_1
    x q[0]
    h q[1]
    wait 1

.do_somework_1_for24_end
    add r31, r31, r30
    blt r31, r29, do

.do_measurement_2
    { measz q[0], b[0] | measz q[1], b[1] | measz q[2], b[2] | measz q[3], b[3] | measz q[4], b[4] | measz q[5], b[5] }
    wait 1
