/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once


#include <libsolidity/formal/EncodingContext.h>
#include <libsolidity/formal/SymbolicVariables.h>
#include <libsolidity/formal/VariableUsage.h>

#include <libsolidity/ast/ASTVisitor.h>
#include <libsolidity/interface/ReadFile.h>
#include <liblangutil/ErrorReporter.h>
#include <liblangutil/Scanner.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace langutil
{
class ErrorReporter;
struct SourceLocation;
}

namespace dev
{
namespace solidity
{

class SMTEncoder: public ASTConstVisitor
{
public:
	SMTEncoder(smt::EncodingContext& _context);

	/// @returns the leftmost identifier in a multi-d IndexAccess.
	static Expression const* leftmostBase(IndexAccess const& _indexAccess);

protected:
	/// AST visitors.
	//@{
	bool visit(ContractDefinition const& _node) override;
	void endVisit(ContractDefinition const& _node) override;
	void endVisit(VariableDeclaration const& _node) override;
	bool visit(ModifierDefinition const& _node) override;
	bool visit(FunctionDefinition const& _node) override;
	void endVisit(FunctionDefinition const& _node) override;
	bool visit(PlaceholderStatement const& _node) override;
	bool visit(IfStatement const& _node) override;
	void endVisit(VariableDeclarationStatement const& _node) override;
	void endVisit(Assignment const& _node) override;
	void endVisit(TupleExpression const& _node) override;
	bool visit(UnaryOperation const& _node) override;
	void endVisit(UnaryOperation const& _node) override;
	bool visit(BinaryOperation const& _node) override;
	void endVisit(BinaryOperation const& _node) override;
	void endVisit(FunctionCall const& _node) override;
	void endVisit(Identifier const& _node) override;
	void endVisit(Literal const& _node) override;
	void endVisit(Return const& _node) override;
	bool visit(MemberAccess const& _node) override;
	void endVisit(IndexAccess const& _node) override;
	bool visit(InlineAssembly const& _node) override;
	//@}

	/// Helpers for specific visitors.
	//@{
	void initFunction(FunctionDefinition const& _function);
	/// Do not visit subtree if node is a RationalNumber.
	/// Symbolic _expr is the rational literal.
	bool shortcutRationalNumber(Expression const& _expr);
	void arithmeticOperation(BinaryOperation const& _op);
	/// @returns _op(_left, _right) with and without modular arithmetic.
	/// Used by the function above, compound assignments and
	/// unary increment/decrement.
	virtual std::pair<smt::Expression, smt::Expression> arithmeticOperation(
		Token _op,
		smt::Expression const& _left,
		smt::Expression const& _right,
		TypePointer const& _commonType,
		Expression const& _expression
	);
	/// Division expression in the given type. Requires special treatment because
	/// of rounding for signed division.
	smt::Expression division(smt::Expression _left, smt::Expression _right, IntegerType const& _type);
	void compareOperation(BinaryOperation const& _op);
	void booleanOperation(BinaryOperation const& _op);

	void visitRequire(FunctionCall const& _funCall);
	void visitGasLeft(FunctionCall const& _funCall);
	void visitTypeConversion(FunctionCall const& _funCall);
	void visitFunctionIdentifier(Identifier const& _identifier);

	/// Encodes a modifier or function body according to the modifier
	/// visit depth.
	void visitFunctionOrModifier();

	/// Handles the side effects of assignment
	/// to variable of some SMT array type
	/// while aliasing is not supported.
	void arrayAssignment();
	/// Handles assignment to SMT array index.
	void arrayIndexAssignment(Expression const& _expr, smt::Expression const& _rightHandSide);

	void assignment(VariableDeclaration const& _variable, Expression const& _value);
	/// Handles assignments to variables of different types.
	void assignment(VariableDeclaration const& _variable, smt::Expression const& _value);
	/// Handles assignments between generic expressions.
	/// Will also be used for assignments of tuple components.
	void assignment(
		Expression const& _left,
		std::vector<smt::Expression> const& _right,
		TypePointer const& _type,
		langutil::SourceLocation const& _location
	);
	/// Computes the right hand side of a compound assignment.
	smt::Expression compoundAssignment(Assignment const& _assignment);
	//@}

	/// Control flow and SSA.
	//@{
	/// Maps a variable to an SSA index.
	using VariableIndices = std::unordered_map<VariableDeclaration const*, int>;

	/// Visits the branch given by the statement, pushes and pops the current path conditions.
	/// @param _condition if present, asserts that this condition is true within the branch.
	/// @returns the variable indices after visiting the branch.
	VariableIndices visitBranch(ASTNode const* _statement, smt::Expression const* _condition = nullptr);
	VariableIndices visitBranch(ASTNode const* _statement, smt::Expression _condition);

	/// Given two different branches and the touched variables,
	/// merge the touched variables into after-branch ite variables
	/// using the branch condition as guard.
	void mergeVariables(std::set<VariableDeclaration const*> const& _variables, smt::Expression const& _condition, VariableIndices const& _indicesEndTrue, VariableIndices const& _indicesEndFalse);

	/// Returns the conjunction of all path conditions or True if empty
	smt::Expression currentPathConditions();
	/// Adds a new path condition
	void pushPathCondition(smt::Expression const& _e);
	/// Remove the last path condition
	void popPathCondition();

	using CallStackEntry = std::pair<CallableDeclaration const*, ASTNode const*>;
	std::vector<CallStackEntry> callStack() { return m_callStack; }
	/// Adds (_definition, _node) to the callstack.
	void pushCallStack(CallStackEntry _entry);
	/// Copies and pops the last called node.
	CallStackEntry popCallStack();
	/// Returns the current callstack. Used for models.
	static langutil::SecondarySourceLocation callStackMessage(std::vector<CallStackEntry> const& _callStack);

	/// Returns true if the current function was not visited by
	/// a function call.
	bool isRootFunction();
	/// Returns true if _funDef was already visited.
	bool visitedFunction(FunctionDefinition const* _funDef);

	/// Copy the SSA indices of m_variables.
	VariableIndices copyVariableIndices();
	/// Resets the variable indices.
	void resetVariableIndices(VariableIndices const& _indices);
	//@}

	/// Symbolic expression helpers.
	//@{
	void initializeLocalVariables(FunctionDefinition const& _function);
	void initializeFunctionCallParameters(CallableDeclaration const& _function, std::vector<smt::Expression> const& _callArgs);

	/// @returns an expression denoting the value of the variable declared in @a _decl
	/// at the current point.
	smt::Expression currentValue(VariableDeclaration const& _decl);
	/// @returns an expression denoting the value of the variable declared in @a _decl
	/// at the given index. Does not ensure that this index exists.
	smt::Expression valueAtIndex(VariableDeclaration const& _decl, int _index);

	/// Tries to create an uninitialized variable and returns true on success.
	bool createVariable(VariableDeclaration const& _varDecl);
	/// Returns the expression corresponding to the AST node. Throws if the expression does not exist.
	smt::Expression expr(Expression const& _e);
	/// Creates the expression (value can be arbitrary)
	void createExpr(Expression const& _e);
	/// Creates the expression and sets its value.
	void defineExpr(Expression const& _e, smt::Expression _value);
	/// Defines a new global variable or function.
	void defineGlobalVariable(std::string const& _name, Expression const& _expr, bool _increaseIndex = false);

	void resetStateVariables();
	//@}

	/// Misc helpers.
	//@{
	/// @returns the type without storage pointer information if it has it.
	TypePointer typeWithoutPointer(TypePointer const& _type);
	/// @returns variables that are touched in _node's subtree.
	std::set<VariableDeclaration const*> touchedVariables(ASTNode const& _node);

	/// @returns the VariableDeclaration referenced by an Identifier or nullptr.
	VariableDeclaration const* identifierToVariable(Expression const& _expr);
	/// Used for better warning messages.
	std::string extraComment();
	//@}

	// Used to compute variables that are touched in a AST subtree.
	smt::VariableUsage m_variableUsage;

	// Use for better warning messages.
	bool m_arrayAssignmentHappened = false;
	// True if the "No SMT solver available" warning was already created.
	bool m_noSolverWarning = false;

	/// Stores the instances of an Uninterpreted Function applied to arguments.
	/// These may be direct application of UFs or Array index access.
	/// Used to retrieve models.
	std::set<Expression const*> m_uninterpretedTerms;

	/// The current branch as a symbolic expression.
	std::vector<smt::Expression> m_pathConditions;

	/// Depth of visit to modifiers.
	/// When m_modifierDepth == #modifiers the function can be visited
	/// when placeholder is visited.
	/// Needs to be a stack because of function calls.
	std::vector<int> m_modifierDepthStack;

	std::vector<CallStackEntry> m_callStack;

	/// Local ErrorReporter.
	/// The warnings are appended to the reference that comes
	/// from CompilerStack in the model checker.
	langutil::ErrorReporter m_errorReporter;
	langutil::ErrorList m_smtErrors;

	/// Used to retrieve the piece of code an expression refers to,
	/// shown in models.
	std::shared_ptr<langutil::Scanner> m_scanner;

	/// Stores the context of the encoding.
	smt::EncodingContext& m_context;
};

}
}
