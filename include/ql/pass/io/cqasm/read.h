/** \file
 * Defines the cQASM reader pass.
 */

#pragma once

#include "ql/pmgr/pass_types.h"

namespace ql {
namespace pass {
namespace io {
namespace cqasm {
namespace read {

/**
 * Reads a cQASM file. Its content are added to program. The number of qubits,
 * cregs, and/or bregs allocated in the program are increased as needed (if
 * possible for the current platform). The gateset parameter should be loaded
 * from a gateset configuration file or be alternatively initialized. If empty
 * or unspecified, a default set is used, that mimics the behavior of the reader
 * before it became configurable.
 */
void from_file(
    const ir::ProgramRef &program,
    const utils::Str &cqasm_fname,
    const utils::Json &gateset={}
);

/**
 * Same as file(), be reads from a string instead.
 *
 * \see file()
 */
void from_string(
    const ir::ProgramRef &program,
    const utils::Str &cqasm_body,
    const utils::Json &gateset={}
);

/**
 * cQASM reader pass.
 */
class ReadCQasmPass : public pmgr::pass_types::ProgramTransformation {
protected:

    /**
     * Dumps docs for the cQASM reader.
     */
    void dump_docs(
        std::ostream &os,
        const utils::Str &line_prefix
    ) const override;

public:

    /**
     * Constructs a cQASM reader.
     */
    ReadCQasmPass(
        const utils::Ptr<const pmgr::PassFactory> &pass_factory,
        const utils::Str &instance_name,
        const utils::Str &type_name
    );

    /**
     * Runs the cQASM reader.
     */
    utils::Int run(
        const ir::ProgramRef &program,
        const pmgr::pass_types::Context &context
    ) const override;

};

/**
 * Shorthand for referring to the pass using namespace notation.
 */
using Pass = ReadCQasmPass;

} // namespace reader
} // namespace cqasm
} // namespace io
} // namespace pass
} // namespace ql
