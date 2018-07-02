#include <algorithm>
#include <boost/algorithm/string/predicate.hpp>
#include <libsolidity/ast/ASTBoogieExpressionConverter.h>
#include <libsolidity/ast/ASTBoogieUtils.h>
#include <libsolidity/ast/BoogieAst.h>
#include <libsolidity/interface/Exceptions.h>
#include <utility>

using namespace std;
using namespace dev;
using namespace dev::solidity;

namespace dev
{
namespace solidity
{

ASTBoogieExpressionConverter::ASTBoogieExpressionConverter()
{
	currentExpr = nullptr;
	currentAddress = nullptr;
	isGetter = false;
}

ASTBoogieExpressionConverter::Result ASTBoogieExpressionConverter::convert(const Expression& _node)
{
	currentExpr = nullptr;
	currentAddress = nullptr;
	isGetter = false;
	newStatements.clear();
	newDecls.clear();

	_node.accept(*this);

	return Result(currentExpr, newStatements, newDecls);
}

bool ASTBoogieExpressionConverter::visit(Conditional const& _node)
{
	// Get condition recursively
	_node.condition().accept(*this);
	const smack::Expr* cond = currentExpr;

	// Get true expression recursively
	_node.trueExpression().accept(*this);
	const smack::Expr* trueExpr = currentExpr;

	// Get false expression recursively
	_node.falseExpression().accept(*this);
	const smack::Expr* falseExpr = currentExpr;

	currentExpr = smack::Expr::cond(cond, trueExpr, falseExpr);
	return false;
}

bool ASTBoogieExpressionConverter::visit(Assignment const& _node)
{
	// Check if LHS is an identifier referencing a variable
	if (Identifier const* lhsId = dynamic_cast<Identifier const*>(&_node.leftHandSide()))
	{
		if (const VariableDeclaration* varDecl = dynamic_cast<const VariableDeclaration*>(lhsId->annotation().referencedDeclaration))
		{
			// Get lhs recursively (required for +=, *=, etc.)
			_node.leftHandSide().accept(*this);
			const smack::Expr* lhs = currentExpr;

			// Get rhs recursively
			_node.rightHandSide().accept(*this);
			const smack::Expr* rhs = currentExpr;

			// Transform rhs based on the operator, e.g., a += b becomes a := a + b
			switch (_node.assignmentOperator()) {
			case Token::Assign: break; // rhs already contains the result
			case Token::AssignAdd: rhs = smack::Expr::plus(lhs, rhs); break;
			case Token::AssignSub: rhs = smack::Expr::minus(lhs, rhs); break;
			case Token::AssignMul: rhs = smack::Expr::times(lhs, rhs); break;
			case Token::AssignDiv:
				if (ASTBoogieUtils::mapType(_node.annotation().type, _node) == "int") rhs = smack::Expr::intdiv(lhs, rhs);
				else rhs = smack::Expr::div(lhs, rhs);
				break;
			case Token::AssignMod: rhs = smack::Expr::mod(lhs, rhs); break;

			default: BOOST_THROW_EXCEPTION(CompilerError() <<
					errinfo_comment(string("Unsupported assignment operator ") + Token::toString(_node.assignmentOperator())) <<
					errinfo_sourceLocation(_node.location()));
			}

			// State variables are represented by maps, so x = 5 becomes
			// an assignment and a map update like x := x[this := 5]
			if (varDecl->isStateVariable())
			{
				newStatements.push_back(smack::Stmt::assign(
						smack::Expr::id(ASTBoogieUtils::mapDeclName(*varDecl)),
						smack::Expr::upd(
								smack::Expr::id(ASTBoogieUtils::mapDeclName(*varDecl)),
								smack::Expr::id(ASTBoogieUtils::BOOGIE_THIS),
								rhs)));
			}
			else // Non-state variables can be simply assigned
			{
				newStatements.push_back(smack::Stmt::assign(lhs, rhs));
			}
			return false;
		}
	}

	BOOST_THROW_EXCEPTION(CompilerError() <<
			errinfo_comment("Only variable references are supported as LHS in assignment") <<
			errinfo_sourceLocation(_node.location()));
	return false;
}

bool ASTBoogieExpressionConverter::visit(TupleExpression const& _node)
{
	if (_node.components().size() == 1)
	{
		_node.components()[0]->accept(*this);
	}
	else
	{
		BOOST_THROW_EXCEPTION(CompilerError() <<
				errinfo_comment("Tuples are not supported yet") <<
				errinfo_sourceLocation(_node.location()));
	}
	return false;
}

bool ASTBoogieExpressionConverter::visit(UnaryOperation const& _node)
{
	// Get operand recursively
	_node.subExpression().accept(*this);
	const smack::Expr* subExpr = currentExpr;

	switch (_node.getOperator()) {
	case Token::Not: currentExpr = smack::Expr::not_(subExpr); break;
	case Token::Sub: currentExpr = smack::Expr::neg(subExpr); break;

	default: BOOST_THROW_EXCEPTION(CompilerError() <<
			errinfo_comment(string("Unsupported operator ") + Token::toString(_node.getOperator())) <<
			errinfo_sourceLocation(_node.location()));
	}

	return false;
}

bool ASTBoogieExpressionConverter::visit(BinaryOperation const& _node)
{
	// Get lhs recursively
	_node.leftExpression().accept(*this);
	const smack::Expr* lhs = currentExpr;

	// Get rhs recursively
	_node.rightExpression().accept(*this);
	const smack::Expr* rhs = currentExpr;

	switch(_node.getOperator())
	{
	case Token::And: currentExpr = smack::Expr::and_(lhs, rhs); break;
	case Token::Or: currentExpr = smack::Expr::or_(lhs, rhs); break;

	case Token::Add: currentExpr = smack::Expr::plus(lhs, rhs); break;
	case Token::Sub: currentExpr = smack::Expr::minus(lhs, rhs); break;
	case Token::Mul: currentExpr = smack::Expr::times(lhs, rhs); break;
	case Token::Div: // Boogie has different division operators for integers and reals
		if (ASTBoogieUtils::mapType(_node.annotation().type, _node) == "int") currentExpr = smack::Expr::intdiv(lhs, rhs);
		else currentExpr = smack::Expr::div(lhs, rhs);
		break;
	case Token::Mod: currentExpr = smack::Expr::mod(lhs, rhs); break;

	case Token::Equal: currentExpr = smack::Expr::eq(lhs, rhs); break;
	case Token::NotEqual: currentExpr = smack::Expr::neq(lhs, rhs); break;
	case Token::LessThan: currentExpr = smack::Expr::lt(lhs, rhs); break;
	case Token::GreaterThan: currentExpr = smack::Expr::gt(lhs, rhs); break;
	case Token::LessThanOrEqual: currentExpr = smack::Expr::lte(lhs, rhs); break;
	case Token::GreaterThanOrEqual: currentExpr = smack::Expr::gte(lhs, rhs); break;

	case Token::Exp:
		// Solidity supports exp only for integers whereas Boogie only
		// supports it for reals. Could be worked around with casts.
	default: BOOST_THROW_EXCEPTION(CompilerError() <<
			errinfo_comment(string("Unsupported operator ") + Token::toString(_node.getOperator())) <<
			errinfo_sourceLocation(_node.location()));
	}

	return false;
}

bool ASTBoogieExpressionConverter::visit(FunctionCall const& _node)
{
	if (_node.annotation().kind == FunctionCallKind::TypeConversion)
	{
		// TODO: type conversions are currently omitted, but might be needed
		// for basic types such as int
		(*_node.arguments().begin())->accept(*this);
		return false;
	}
	// Function calls in Boogie are statements and cannot be part of
	// expressions, therefore each function call is given a fresh variable
	// for its return value and is mapped to a call statement

	// Get the name of the called function and the address on which it is called
	// By default, it is 'this', but member access expressions can override it
	currentExpr = nullptr;
	currentAddress = smack::Expr::id(ASTBoogieUtils::BOOGIE_THIS);
	isGetter = false;
	_node.expression().accept(*this);
	string funcName;
	if (smack::VarExpr const * v = dynamic_cast<smack::VarExpr const*>(currentExpr))
	{
		funcName = v->name();
	}
	else
	{
		BOOST_THROW_EXCEPTION(CompilerError() <<
				errinfo_comment(string("Only identifiers are supported as function calls")) <<
				errinfo_sourceLocation(_node.location()));
	}

	if (isGetter && _node.arguments().size() > 0)
	{
		BOOST_THROW_EXCEPTION(InternalCompilerError() <<
						errinfo_comment("Getter should not have arguments") <<
						errinfo_sourceLocation(_node.location()));
	}

	// Get arguments recursively
	list<const smack::Expr*> args;
	// Pass extra arguments
	args.push_back(currentAddress); // this
	args.push_back(smack::Expr::id(ASTBoogieUtils::BOOGIE_THIS)); // msg.sender
	// Add normal arguments
	for (auto arg : _node.arguments())
	{
		currentExpr = nullptr;
		arg->accept(*this);
		args.push_back(currentExpr);
	}

	// Assert is a separate statement in Boogie (instead of a function call)
	if (funcName == ASTBoogieUtils::SOLIDITY_ASSERT)
	{
		// The parameter of assert is the first normal argument
		list<const smack::Expr*>::iterator it = args.begin();
		std::advance(it, 2);
		currentExpr = *it;
		newStatements.push_back(smack::Stmt::assert_(currentExpr));
		return false;
	}

	// TODO: check for void return in a more appropriate way
	if (_node.annotation().type->toString() != "tuple()")
	{
		// Create fresh variable to store the result
		smack::Decl* returnVar = smack::Decl::variable(
				funcName + "#" + to_string(_node.id()),
				ASTBoogieUtils::mapType(_node.annotation().type, _node));
		newDecls.push_back(returnVar);
		// Result of the function call is the fresh variable
		currentExpr = smack::Expr::id(returnVar->getName());

		if (isGetter)
		{
			// Getters are replaced with map access (instead of function call)
			newStatements.push_back(smack::Stmt::assign(
					smack::Expr::id(returnVar->getName()),
					smack::Expr::sel(smack::Expr::id(funcName), currentAddress)));
		}
		else
		{
			list<string> rets;
			rets.push_back(returnVar->getName());
			// Assign call to the fresh variable
			newStatements.push_back(smack::Stmt::call(funcName, args, rets));
		}

	}
	else
	{
		currentExpr = nullptr; // No return value for function
		newStatements.push_back(smack::Stmt::call(funcName, args));
	}

	return false;
}

bool ASTBoogieExpressionConverter::visit(NewExpression const& _node)
{
	return visitNode(_node);
}

bool ASTBoogieExpressionConverter::visit(MemberAccess const& _node)
{
	// Get expression recursively
	_node.expression().accept(*this);
	const smack::Expr* expr = currentExpr;
	// In case of a function call, the current expression is the address
	// on which the function is called
	currentAddress = currentExpr;
	// Type of the expression
	string typeStr = _node.expression().annotation().type->toString();

	// Handle special members/functions

	// address.balance
	if (typeStr == ASTBoogieUtils::SOLIDITY_ADDRESS_TYPE && _node.memberName() == ASTBoogieUtils::SOLIDITY_BALANCE)
	{
		currentExpr = smack::Expr::sel(smack::Expr::id(ASTBoogieUtils::BOOGIE_BALANCE), expr);
	}
	// address.transfer()
	else if (typeStr == ASTBoogieUtils::SOLIDITY_ADDRESS_TYPE && _node.memberName() == ASTBoogieUtils::SOLIDITY_TRANSFER)
	{
		currentExpr = smack::Expr::id(ASTBoogieUtils::BOOGIE_TRANSFER);
	}
	// msg.sender
	else if (typeStr == ASTBoogieUtils::SOLIDITY_MSG && _node.memberName() == ASTBoogieUtils::SOLIDITY_SENDER)
	{
		currentExpr = smack::Expr::id(ASTBoogieUtils::BOOGIE_MSG_SENDER);
	}
	// Non-special member access
	else
	{
		currentExpr = smack::Expr::id(ASTBoogieUtils::mapDeclName(*_node.annotation().referencedDeclaration));
		isGetter = false;
		if (dynamic_cast<const VariableDeclaration*>(_node.annotation().referencedDeclaration))
		{
			isGetter = true;
		}
	}
	return false;
}

bool ASTBoogieExpressionConverter::visit(IndexAccess const& _node)
{
	_node.baseExpression().accept(*this);
	const smack::Expr* baseExpr = currentExpr;

	_node.indexExpression()->accept(*this); // TODO: can this be a nullptr?
	const smack::Expr* indexExpr = currentExpr;

	currentExpr = smack::Expr::sel(baseExpr, indexExpr);

	return false;
}

bool ASTBoogieExpressionConverter::visit(Identifier const& _node)
{
	string declName = ASTBoogieUtils::mapDeclName(*(_node.annotation().referencedDeclaration));

	// Check if a state variable is referenced
	bool referencesStateVar = false;
	if (const VariableDeclaration* varDecl = dynamic_cast<const VariableDeclaration*>(_node.annotation().referencedDeclaration))
	{
		referencesStateVar = varDecl->isStateVariable();
	}

	// State variables must be referenced by accessing the map
	if (referencesStateVar) { currentExpr = smack::Expr::sel(declName, ASTBoogieUtils::BOOGIE_THIS); }
	// Other identifiers can be referenced by their name
	else { currentExpr = smack::Expr::id(declName); }
	return false;
}

bool ASTBoogieExpressionConverter::visit(ElementaryTypeNameExpression const& _node)
{
	return visitNode(_node);
}

bool ASTBoogieExpressionConverter::visit(Literal const& _node)
{
	string tpStr = _node.annotation().type->toString();
	// TODO: option for bit precise types
	if (boost::starts_with(tpStr, "int_const"))
	{
		currentExpr = smack::Expr::lit(stol(_node.value()));
		return false;
	}
	if (tpStr == "bool")
	{
		currentExpr = smack::Expr::lit(_node.value() == "true");
		return false;
	}

	BOOST_THROW_EXCEPTION(CompilerError() <<
			errinfo_comment("Unsupported type: " + tpStr) <<
			errinfo_sourceLocation(_node.location()));
	return false;
}

bool ASTBoogieExpressionConverter::visitNode(ASTNode const& _node)
{
	BOOST_THROW_EXCEPTION(InternalCompilerError() <<
					errinfo_comment("Unhandled node") <<
					errinfo_sourceLocation(_node.location()));
	return true;
}

}
}
