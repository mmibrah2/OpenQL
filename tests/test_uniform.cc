#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <cassert>

#include <time.h>

#include "ql/openql.h"
#include "ql/utils.h"

void
test_0( std::string scheduler)
{
    // create and set platform
    ql::quantum_platform starmon("starmon","test_cfg_none_s7.json");
    ql::set_platform(starmon);

    // create program
    ql::quantum_program prog(("test_0_" + scheduler), 7, starmon);
    ql::quantum_kernel k("kernel7",starmon);

    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 0,2);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 6,3);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 1,4);

    prog.add(k);

    ql::options::set("scheduler", scheduler);
    prog.compile( );
}

void
test_1( std::string scheduler)
{
    // create and set platform
    ql::quantum_platform starmon("starmon","test_cfg_none_s7.json");
    ql::set_platform(starmon);

    // create program
    ql::quantum_program prog(("test_1_" + scheduler), 7, starmon);
    ql::quantum_kernel k("kernel7",starmon);

    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 0,2);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 6,3);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 1,4);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 2,5);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 3,1);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 4,6);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 2,0);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 3,6);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 4,1);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 5,2);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 1,3);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 6,4);
    for (int j=0; j<7; j++)
        k.gate("x", j);

    prog.add(k);

    ql::options::set("scheduler", scheduler);
    prog.compile( );
}

void
test_2( std::string scheduler)
{
    // create and set platform
    ql::quantum_platform starmon("starmon","test_cfg_none_s7.json");
    ql::set_platform(starmon);

    // create program
    ql::quantum_program prog(("test_2_" + scheduler), 7, starmon);
    ql::quantum_kernel k("kernel7",starmon);

    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 0,2);
    k.gate("cnot", 6,3);
    k.gate("cnot", 1,4);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 2,5);
    k.gate("cnot", 3,1);
    k.gate("cnot", 4,6);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 2,0);
    k.gate("cnot", 3,6);
    k.gate("cnot", 4,1);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 5,2);
    k.gate("cnot", 1,3);
    k.gate("cnot", 6,4);
    for (int j=0; j<7; j++)
        k.gate("x", j);

    prog.add(k);

    ql::options::set("scheduler", scheduler);
    prog.compile( );
}

void
test_3( std::string scheduler)
{
    // create and set platform
    ql::quantum_platform starmon("starmon","test_cfg_none_s7.json");
    ql::set_platform(starmon);

    // create program
    ql::quantum_program prog(("test_3_" + scheduler), 7, starmon);
    ql::quantum_kernel k("kernel7",starmon);

    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 6,3);
    k.gate("cnot", 0,2);
    k.gate("cnot", 1,3);
    k.gate("cnot", 1,4);
    k.gate("cnot", 0,3);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 2,5);
    k.gate("cnot", 3,1);
    k.gate("cnot", 2,0);
    k.gate("cnot", 3,6);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 4,1);
    k.gate("cnot", 3,0);
    k.gate("cnot", 4,6);
    for (int j=0; j<7; j++)
        k.gate("x", j);
    k.gate("cnot", 3,5);
    k.gate("cnot", 5,2);
    k.gate("cnot", 6,4);
    k.gate("cnot", 5,3);
    for (int j=0; j<7; j++)
        k.gate("x", j);

    prog.add(k);

    ql::options::set("scheduler", scheduler);
    prog.compile( );
}

void
test_4( std::string scheduler)
{
    // create and set platform
    ql::quantum_platform starmon("starmon","test_cfg_none_s7.json");
    ql::set_platform(starmon);

    // create program
    ql::quantum_program prog(("test_4_" + scheduler), 7, starmon);
    ql::quantum_kernel k("kernel7",starmon);

    for (int j=0; j<7; j++)
        k.gate("x", j);

    k.gate("cnot", 0,2);
    k.gate("cnot", 0,3);
    k.gate("cnot", 1,3);
    k.gate("cnot", 1,4);
    k.gate("cnot", 2,0);
    k.gate("cnot", 2,5);
    k.gate("cnot", 3,0);
    k.gate("cnot", 3,1);
    k.gate("cnot", 3,5);
    k.gate("cnot", 3,6);
    k.gate("cnot", 4,1);
    k.gate("cnot", 4,6);
    k.gate("cnot", 5,2);
    k.gate("cnot", 5,3);
    k.gate("cnot", 6,3);
    k.gate("cnot", 6,4);

    prog.add(k);

    ql::options::set("scheduler", scheduler);
    prog.compile( );
}

int main(int argc, char ** argv)
{
    ql::utils::logger::set_log_level("LOG_DEBUG");

    test_0("ASAP");
    test_0("UNIFORM");
    test_1("ASAP");
    test_1("UNIFORM");
    test_2("ASAP");
    test_2("UNIFORM");
    test_3("ASAP");
    test_3("UNIFORM");
    test_4("ASAP");
    test_4("UNIFORM");

    return 0;
}
