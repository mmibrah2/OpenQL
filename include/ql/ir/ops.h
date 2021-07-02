/** \file
 * Defines basic access operations on the IR.
 */

#pragma once

#include "ql/utils/map.h"
#include "ql/ir/ir.h"

namespace ql {
namespace ir {

// Private template stuff.
namespace {

/**
 * Compares two named nodes by name.
 */
template <class T>
utils::Bool compare_by_name(const utils::One<T> &lhs, const utils::One<T> &rhs) {
    return lhs->name < rhs->name;
}

} // anonymous namespace

/**
 * Registers a data type.
 */
template <class T, typename... Args>
DataTypeLink add_type(const Ref &ir, Args... args) {

    // Construct a new data type object as requested.
    auto dtyp = utils::make<T>(std::forward<Args>(args)...).template as<DataType>();

    // Check its name. Note: some types may have additional parameters that are
    // not consistency-checked here.
    if (!std::regex_match(dtyp->name, IDENTIFIER_RE)) {
        throw utils::Exception(
            "invalid name for new data type: \"" + dtyp->name + "\" is not a valid identifier"
        );
    }

    // Insert it in the right position to maintain list order by name, while
    // doing a name uniqueness test at the same time.
    auto begin = ir->platform->data_types.get_vec().begin();
    auto end = ir->platform->data_types.get_vec().end();
    auto pos = std::lower_bound(begin, end, dtyp, compare_by_name<DataType>);
    if (pos != end && (*pos)->name == dtyp->name) {
        throw utils::Exception(
            "invalid name for new data type: \"" + dtyp->name + "\" is already in use"
        );
    }
    ir->platform->data_types.get_vec().insert(pos, dtyp);

    return dtyp;
}

/**
 * Returns the data type with the given name, or returns an empty link if the
 * type does not exist.
 */
DataTypeLink find_type(const Ref &ir, const utils::Str &name);

/**
 * Returns the data type of/returned by an expression.
 */
DataTypeLink get_type_of(const ExpressionRef &expr);

/**
 * Returns the maximum value that an integer of the given type may have.
 */
utils::Int get_max_int_for(const IntType &ityp);

/**
 * Returns the minimum value that an integer of the given type may have.
 */
utils::Int get_min_int_for(const IntType &ityp);

/**
 * Adds a physical object to the platform.
 */
ObjectLink add_physical_object(const Ref &ir, const utils::One<PhysicalObject> &obj);

/**
 * Returns the physical object with the given name, or returns an empty link if
 * the object does not exist.
 */
ObjectLink find_physical_object(const Ref &ir, const utils::Str &name);

/**
 * Adds an instruction type to the platform. The instruction_type object should
 * be fully generalized; template operands can be attached with the optional
 * additional argument (in which case the instruction specialization tree will
 * be generated appropriately).
 */
InstructionTypeLink add_instruction_type(
    const Ref &ir,
    const utils::One<InstructionType> &instruction_type,
    const utils::Any<Expression> &template_operands = {}
);

/**
 * Finds an instruction type based on its name and operand types. If
 * generate_overload_if_needed is set, and no instruction with the given name
 * and operand type set exists, then an overload is generated for the first
 * instruction type for which only the name matches, and that overload is
 * returned. If no matching instruction type is found or was created, an empty
 * link is returned.
 */
InstructionTypeLink find_instruction_type(
    const Ref &ir,
    const utils::Str &name,
    const utils::Vec<DataTypeLink> &types,
    utils::Bool generate_overload_if_needed = false
);

/**
 * Builds a new instruction node based on the given name and operand list. Its
 * behavior depends on name.
 *
 *  - If "set", a set instruction is created. Exactly two operands must be
 *    specified, of which the first is the LHS and the second is the RHS. The
 *    LHS must be a reference, and have a classical data type. The RHS must have
 *    exactly the same data type as the LHS.
 *  - If "wait", a wait instruction is created. The first operand must be a
 *    non-negative integer literal, representing the duration. The remainder of
 *    the operands are what's waited on, and must be references. If there is
 *    only one operand, the instruction is a full barrier (i.e. it effectively
 *    waits on all objects).
 *  - If "barrier", a zero-duration wait instruction is created. The operands
 *    are what's waited on, and must be references. If there are no operands,
 *    the instruction is a full barrier (i.e. it effectively waits on all
 *    objects).
 *  - Any other name is treated as a custom instruction, resolved via
 *    find_instruction_type(). The most specialized instruction type is used.
 *
 * If no condition is specified, the instruction will be unconditional (a
 * literal true node is generated for it). For wait instructions, the specified
 * condition *must* be null, as wait instructions are always unconditional.
 *
 * Note that goto and dummy instructions cannot be created via this interface.
 *
 * return_empty_on_failure disables the exception that would otherwise be thrown
 * if no matching instruction type is found, instead returning {}.
 *
 * The generate_overload_if_needed flag is a hack for the conversion process
 * from the old to new IR. See find_instruction_type().
 */
InstructionRef make_instruction(
    const Ref &ir,
    const utils::Str &name,
    const utils::Any<Expression> &operands,
    const ExpressionRef &condition = {},
    utils::Bool return_empty_on_failure = false,
    utils::Bool generate_overload_if_needed = false
);

/**
 * Shorthand for making a set instruction.
 */
InstructionRef make_set_instruction(
    const Ref &ir,
    const ExpressionRef &lhs,
    const ExpressionRef &rhs,
    const ExpressionRef &condition = {}
);

/**
 * Updates the given instruction node to use the most specialized instruction
 * type available. If the instruction is not a custom instruction or the
 * instruction is already fully specialized, this is no-op.
 */
void specialize_instruction(
    const InstructionRef &instruction
);

/**
 * Updates the given instruction node to use the most generalized instruction
 * type available. If the instruction is not a custom instruction or the
 * instruction is already fully generalized, this is no-op.
 *
 * This is useful in particular for changing instruction operands when mapping:
 * first generalize to get all the operands in the instruction node, then modify
 * the operands, and finally specialize the instruction again according to the
 * changed operands using specialize_instruction().
 */
void generalize_instruction(
    const InstructionRef &instruction
);

/**
 * Returns the most generalized variant of the given instruction type.
 */
InstructionTypeLink get_generalization(const InstructionTypeLink &spec);

/**
 * Returns the complete list of operands of an instruction. For custom
 * instructions this includes the template operands, and for set instructions
 * this returns the LHS and RHS as two operands. Other instruction types return
 * no operands. The condition (if any) is also not returned.
 */
Any<Expression> get_operands(const InstructionRef &instruction);

/**
 * Adds a decomposition rule. An instruction is generated for the decomposition
 * rule based on instruction_type and template_operands if one didn't already
 * exist. If one did already exist, only the decompositions field of
 * instruction_type is used to extend the decomposition rule list of the
 * existing instruction type.
 */
InstructionTypeLink add_decomposition_rule(
    const Ref &ir,
    const utils::One<InstructionType> &instruction_type,
    const utils::Any<Expression> &template_operands
);

/**
 * Adds a function type to the platform.
 */
FunctionTypeLink add_function_type(
    const Ref &ir,
    const utils::One<FunctionType> &function_type
);

/**
 * Finds a function type based on its name and operand types. If no matching
 * function type is found, an empty link is returned.
 */
FunctionTypeLink find_function_type(
    const Ref &ir,
    const utils::Str &name,
    const utils::Vec<DataTypeLink> &types
);

/**
 * Builds a new function call node based on the given name and operand list.
 */
utils::One<FunctionCall> make_function_call(
    const Ref &ir,
    const utils::Str &name,
    const utils::Any<Expression> &operands
);

/**
 * Returns the number of qubits in the main qubit register.
 */
utils::UInt get_num_qubits(const Ref &ir);

/**
 * Returns whether the given expression can be assigned or is a qubit (i.e.,
 * whether it can appear on the left-hand side of an assignment, or can be used
 * as an operand in classical write or qubit access mode).
 */
utils::Bool is_assignable_or_qubit(const ExpressionRef &expr);

/**
 * Makes an integer literal using the given or default integer type.
 */
utils::One<IntLiteral> make_int_lit(const Ref &ir, utils::Int i, const DataTypeLink &type = {});

/**
 * Makes an integer literal using the given or default integer type.
 */
utils::One<IntLiteral> make_uint_lit(const Ref &ir, utils::UInt i, const DataTypeLink &type = {});

/**
 * Makes an bit literal using the given or default bit type.
 */
utils::One<BitLiteral> make_bit_lit(const Ref &ir, utils::Bool b, const DataTypeLink &type = {});

/**
 * Makes a qubit reference to the main qubit register.
 */
utils::One<Reference> make_qubit_ref(const Ref &ir, utils::UInt idx);

/**
 * Makes a reference to the implicit measurement bit associated with a qubit in
 * the main qubit register.
 */
utils::One<Reference> make_bit_ref(const Ref &ir, utils::UInt idx);

/**
 * Makes a reference to the specified object using literal indices.
 */
utils::One<Reference> make_reference(
    const Ref &ir,
    const ObjectLink &obj,
    utils::Vec<utils::UInt> indices = {}
);

/**
 * Makes a temporary object with the given type.
 */
ObjectLink make_temporary(const Ref &ir, const DataTypeLink &data_type);

/**
 * Returns the duration of an instruction in quantum cycles. Note that this will
 * be zero for non-quantum instructions.
 */
utils::UInt get_duration_of_instruction(const InstructionRef &insn);

/**
 * Returns the duration of a block in quantum cycles. If the block contains
 * structured control-flow sub-blocks, these are counted as zero cycles.
 */
utils::UInt get_duration_of_block(const BlockBaseRef &block);

/**
 * Returns whether an instruction is a quantum gate, by returning the number of
 * qubits in its operand list.
 */
utils::UInt get_number_of_qubits_involved(const InstructionRef &insn);

/**
 * The associativity of an operator.
 */
enum class OperatorAssociativity {

    /**
     * Left-associative, i.e. a # b # c === (a # b) # c.
     */
    LEFT,

    /**
     * Right-associative, i.e. a # b # c === a # (b # c).
     */
    RIGHT

};

/**
 * Operator information structure.
 */
struct OperatorInfo {

    /**
     * The precedence level for the operator. If the precedence of operator #
     * is higher than the precedence of operator %, a # b % c === (a # b) % c
     * and a % b # c === a % (b # c), regardless of the associativity of either.
     */
    utils::UInt precedence;

    /**
     * The associativity of the operator. Indicates whether a # b # c is
     * identical to (a # b) # c (= left) or to a # (b # c) (= right).
     */
    OperatorAssociativity associativity;

    /**
     * String to prefix before the operands.
     */
    const char *prefix;

    /**
     * String to insert between the first and second operand.
     */
    const char *infix;

    /**
     * String to insert between the second and third operand.
     */
    const char *infix2;

};

/**
 * Metadata for operators as they appear in cQASM (or just logically in
 * general). Used to avoid excessive parentheses when printing expressions.
 * The first element in the key pair is the function name, the second is the
 * number of operands.
 */
extern const utils::Map<utils::Pair<utils::Str, utils::UInt>, OperatorInfo> OPERATOR_INFO;

/**
 * Gives a one-line description of a node.
 */
void describe(const Node &node, std::ostream &ss);

/**
 * Gives a one-line description of a node.
 */
void describe(const utils::One<Node> &node, std::ostream &ss);

/**
 * Gives a one-line description of a node.
 */
utils::Str describe(const Node &node);

/**
 * Gives a one-line description of a node.
 */
utils::Str describe(const utils::One<Node> &node);

/**
 * A reference to an object (including index) or a null reference, for the
 * purpose of representing a data dependency. The null reference is used for
 * barriers without operands (i.e. barriers that must have a data dependency
 * with all other objects) and goto instructions: these instructions "write"
 * to the "null object", while all other instructions read from it. This just
 * wraps ir::Reference, in such a way that it can be used as the key for ordered
 * maps and sets, and such that equality is value-based.
 */
class UniqueReference {
public:

    /**
     * The wrapped reference.
     */
    Reference reference;

    /**
     * Clones this wrapper (and its underlying reference object).
     */
    UniqueReference clone() const;

    /**
     * Dereference operator (shorthand).
     */
    const Reference &operator*() const;

    /**
     * Dereference operator (shorthand).
     */
    Reference &operator*();

    /**
     * Dereference operator (shorthand).
     */
    const Reference *operator->() const;

    /**
     * Dereference operator (shorthand).
     */
    Reference *operator->();

    /**
     * Value-based less-than operator to allow this to be used as a key to
     * a map.
     */
    utils::Bool operator<(const UniqueReference &rhs) const;

    /**
     * Value-based equality operator.
     */
    utils::Bool operator==(const UniqueReference &rhs) const;

};

/**
 * Container for gathering and representing the list of object accesses for
 * instructions and expressions.
 */
class ObjectAccesses {
public:

    /**
     * An object access, as used for representing data dependencies.
     */
    using Access = utils::Pair<UniqueReference, prim::AccessMode>;

    /**
     * Shorthand for the data dependency list container.
     */
    using Accesses = utils::Map<UniqueReference, prim::AccessMode>;

private:

    /**
     * Reference to the root of the IR.
     */
    Ref ir;

    /**
     * The actual dependency list.
     */
    Accesses accesses;

public:

    /**
     * Configuration tweak that disables X/Y/Z commutation for single-qubit
     * gates (i.e., instructions with a single-qubit operand). Modifying this
     * only affects the behavior of subsequent add_*() calls; it doesn't affect
     * previously added dependencies.
     */
    utils::Bool disable_single_qubit_commutation = false;

    /**
     * Configuration tweak that disables X/Y/Z commutation for multi-qubit
     * gates (i.e., an instruction with a multi-qubit operand). Modifying this
     * only affects the behavior of subsequent add_*() calls; it doesn't affect
     * previously added dependencies.
     */
    utils::Bool disable_multi_qubit_commutation = false;

    /**
     * Constructs an object reference gatherer.
     */
    ObjectAccesses(const Ref &ir);

    /**
     * Returns the contained list of object accesses.
     */
    const Accesses &get() const;

    /**
     * Adds a single object access. Literal access mode is upgraded to read
     * mode, as it makes no sense to access an object in literal mode (this
     * should never happen for consistent IRs though, unless this is explicitly
     * called this way). Measure access mode is upgraded to a write access to
     * both the qubit and the implicit bit associated with it. If there was
     * already an access for the object, the access mode is combined: if they
     * match the mode is maintained, otherwise the mode is changed to write.
     */
    void add_access(
        prim::AccessMode mode,
        const UniqueReference &reference
    );

    /**
     * Adds dependencies on whatever is used by a complete expression.
     */
    void add_expression(
        prim::AccessMode mode,
        const ExpressionRef &expr
    );

    /**
     * Adds dependencies on the operands of a function or instruction.
     */
    void add_operands(
        const utils::Any<OperandType> &prototype,
        const utils::Any<Expression> &operands
    );

    /**
     * Adds dependencies for a complete statement.
     */
    void add_statement(const StatementRef &stmt);

    /**
     * Adds dependencies for a whole (sub)block of statements.
     */
    void add_block(const SubBlockRef &block);

    /**
     * Clears the dependency list, allowing the object to be reused.
     */
    void reset();

};

/**
 * Visitor that rewrites object references to implement (re)mapping.
 *
 * FIXME: this fundamentally can't handle remapping elements of non-scalar
 *  stuff. So it's probably not good enough.
 */
class ReferenceRemapper : RecursiveVisitor {
public:

    /**
     * Shorthand for the object link map type.
     */
    using Map = utils::Map<ObjectLink, ObjectLink>;

    /**
     * The object link map.
     */
    Map map;

    /**
     * Constructs a remapper.
     */
    explicit ReferenceRemapper(Map &&map = {});

    /**
     * Constructs a remapper.
     */
    explicit ReferenceRemapper(const Map &map);

    /**
     * The visit function that actually implements the remapping.
     */
    void visit_reference(Reference &node) override;

};

} // namespace ir
} // namespace ql
