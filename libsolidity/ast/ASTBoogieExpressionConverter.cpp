#include <boost/algorithm/string/predicate.hpp>
#include <libsolidity/ast/ASTBoogieExpressionConverter.h>
#include <libsolidity/ast/ASTBoogieUtils.h>
#include <libsolidity/ast/AST.h>
#include <libsolidity/ast/TypeProvider.h>
#include <liblangutil/Exceptions.h>

using namespace std;
using namespace dev;
using namespace dev::solidity;
using namespace langutil;

namespace bg = boogie;

namespace dev
{
namespace solidity
{

void ASTBoogieExpressionConverter::addTCC(bg::Expr::Ref expr, TypePointer tp)
{
	if (m_context.encoding() == BoogieContext::Encoding::MOD && ASTBoogieUtils::isBitPreciseType(tp))
		m_tccs.push_back(ASTBoogieUtils::getTCCforExpr(expr, tp));
}

void ASTBoogieExpressionConverter::addSideEffect(bg::Stmt::Ref stmt)
{
	for (auto oc: m_ocs)
		m_newStatements.push_back(bg::Stmt::assign(
			bg::Expr::id(ASTBoogieUtils::VERIFIER_OVERFLOW),
			bg::Expr::or_(bg::Expr::id(ASTBoogieUtils::VERIFIER_OVERFLOW), bg::Expr::not_(oc))));

	m_ocs.clear();
	m_newStatements.push_back(stmt);
}

ASTBoogieExpressionConverter::ASTBoogieExpressionConverter(BoogieContext& context) :
		m_context(context),
		m_currentExpr(nullptr),
		m_currentAddress(nullptr),
		m_currentMsgValue(nullptr),
		m_isGetter(false),
		m_isLibraryCall(false),
		m_isLibraryCallStatic(false) {}

ASTBoogieExpressionConverter::Result ASTBoogieExpressionConverter::convert(Expression const& _node)
{
	m_currentExpr = nullptr;
	m_currentAddress = nullptr;
	m_currentMsgValue = nullptr;
	m_isGetter = false;
	m_isLibraryCall = false;
	m_isLibraryCallStatic = false;
	m_newStatements.clear();
	m_newDecls.clear();
	m_tccs.clear();
	m_ocs.clear();

	_node.accept(*this);

	return Result(m_currentExpr, m_newStatements, m_newDecls, m_tccs, m_ocs);
}

bool ASTBoogieExpressionConverter::visit(Conditional const& _node)
{
	// Get condition recursively
	_node.condition().accept(*this);
	bg::Expr::Ref cond = m_currentExpr;

	// Get true expression recursively
	_node.trueExpression().accept(*this);
	bg::Expr::Ref trueExpr = m_currentExpr;

	// Get false expression recursively
	_node.falseExpression().accept(*this);
	bg::Expr::Ref falseExpr = m_currentExpr;

	m_currentExpr = bg::Expr::cond(cond, trueExpr, falseExpr);
	return false;
}

bool ASTBoogieExpressionConverter::visit(Assignment const& _node)
{
	ASTBoogieUtils::ExprWithCC result;

	// LHS/RHS Nodes
	Expression const& lhsNode = _node.leftHandSide();
	Expression const& rhsNode = _node.rightHandSide();

	// LHS/RHS Types
	TypePointer lhsType = lhsNode.annotation().type;
	TypePointer rhsType = rhsNode.annotation().type;

	// Get lhs recursively
	_node.leftHandSide().accept(*this);
	bg::Expr::Ref lhsExpr = m_currentExpr;

	// Get rhs recursively
	_node.rightHandSide().accept(*this);
	bg::Expr::Ref rhsExpr = m_currentExpr;

	auto res = ASTBoogieUtils::makeAssign(
			ASTBoogieUtils::AssignParam{lhsExpr, lhsType, &lhsNode},
			ASTBoogieUtils::AssignParam{rhsExpr, rhsType, &rhsNode},
			_node.assignmentOperator(), &_node, m_context);
	m_newDecls.insert(m_newDecls.end(), res.newDecls.begin(), res.newDecls.end());
	m_ocs.insert(m_ocs.end(), res.ocs.begin(), res.ocs.end());
	for (auto stmt: res.newStmts)
		addSideEffect(stmt);

	// Result will be the LHS (for chained assignments like x = y = 5)
	m_currentExpr = lhsExpr;

	return false;
}

bool ASTBoogieExpressionConverter::visit(TupleExpression const& _node)
{
	if (_node.isInlineArray())
	{
		auto arrType = dynamic_cast<ArrayType const*>(_node.annotation().type);
		auto bgType = m_context.toBoogieType(arrType->baseType(), &_node);
		// Create new
		auto varDecl = ASTBoogieUtils::newArray(m_context.toBoogieType(_node.annotation().type, &_node), m_context);
		m_newDecls.push_back(varDecl);
		auto arrExpr = m_context.getMemArray(varDecl->getRefTo(), bgType);
		// Set each element
		for (size_t i = 0; i < _node.components().size(); i++)
		{
			_node.components()[i]->accept(*this);
			addSideEffect(bg::Stmt::assign(
					bg::Expr::arrsel(
							m_context.getInnerArray(arrExpr, bgType),
							m_context.intLit(i, 256)),
					m_currentExpr));
		}
		m_currentExpr = varDecl->getRefTo();
		// Set size
		addSideEffect(bg::Stmt::assign(
				m_context.getArrayLength(arrExpr, bgType),
				m_context.intLit(_node.components().size(), 256)));
		return false;
	}

	// Get the elements
	vector<bg::Expr::Ref> elements;
	for (auto element: _node.components())
	{
		if (element != nullptr)
		{
			element->accept(*this);
			elements.push_back(m_currentExpr);
		}
		else
			elements.push_back(nullptr);
	}

	// Make the expression (tuples of size 1, just use the expression)
	if (elements.size() == 1)
		m_currentExpr = elements.back();
	else
		m_currentExpr = bg::Expr::tuple(elements);

	return false;
}

bool ASTBoogieExpressionConverter::visit(UnaryOperation const& _node)
{
	// Check if constant propagation could infer the result
	TypePointer tp = _node.annotation().type;
	if (auto tpRational = dynamic_cast<RationalNumberType const*>(tp))
	{
		auto value = tpRational->literalValue(nullptr);
		if (tpRational->isNegative())
			m_currentExpr = bg::Expr::lit(bg::bigint(u2s(value)));
		else
			m_currentExpr = bg::Expr::lit(bg::bigint(value));
		return false;
	}

	// Get operand recursively
	_node.subExpression().accept(*this);
	bg::Expr::Ref subExpr = m_currentExpr;

	switch (_node.getOperator())
	{
	case Token::Add: m_currentExpr = subExpr; break; // Unary plus does not do anything
	case Token::Not: m_currentExpr = bg::Expr::not_(subExpr); break;

	case Token::Sub:
	case Token::BitNot: {
		unsigned bits = ASTBoogieUtils::getBits(_node.annotation().type);
		bool isSigned = ASTBoogieUtils::isSigned(_node.annotation().type);
		auto exprResult = ASTBoogieUtils::encodeArithUnaryOp(m_context, &_node, _node.getOperator(), subExpr, bits, isSigned);
		m_currentExpr = exprResult.expr;
		if (m_context.overflow() && exprResult.cc)
			m_ocs.push_back(exprResult.cc);
		break;
	}

	// Inc and Dec shares most part of the code
	case Token::Inc:
	case Token::Dec:
		{
			unsigned bits = ASTBoogieUtils::getBits(_node.annotation().type);
			bool isSigned = ASTBoogieUtils::isSigned(_node.annotation().type);
			bg::Expr::Ref lhs = subExpr;
			auto rhsResult =
					ASTBoogieUtils::encodeArithBinaryOp(m_context, &_node,
							_node.getOperator() == Token::Inc ? Token::Add : Token::Sub,
							lhs,
							m_context.intLit(1, bits),
							bits, isSigned);
			bg::Decl::Ref tempVar = m_context.freshTempVar(m_context.toBoogieType(_node.subExpression().annotation().type, &_node));
			m_newDecls.push_back(tempVar);
			if (_node.isPrefixOperation()) // ++x (or --x)
			{
				// First do the assignment x := x + 1 (or x := x - 1)
				if (m_context.overflow() && rhsResult.cc)
					m_ocs.push_back(rhsResult.cc);
				auto res = ASTBoogieUtils::makeAssign(
						ASTBoogieUtils::AssignParam{lhs, _node.annotation().type, &_node.subExpression()},
						ASTBoogieUtils::AssignParam{rhsResult.expr, _node.annotation().type, nullptr},
						Token::Assign, &_node, m_context);
				for (auto stmt: res.newStmts)
					addSideEffect(stmt);
				// Then the assignment tmp := x
				addSideEffect(bg::Stmt::assign(tempVar->getRefTo(), lhs));
			}
			else // x++ (or x--)
			{
				// First do the assignment tmp := x
				addSideEffect(bg::Stmt::assign(tempVar->getRefTo(), subExpr));
				// Then the assignment x := x + 1 (or x := x - 1)
				if (m_context.overflow() && rhsResult.cc)
					m_ocs.push_back(rhsResult.cc);
				auto res = ASTBoogieUtils::makeAssign(
						ASTBoogieUtils::AssignParam{lhs, _node.annotation().type, &_node.subExpression()},
						ASTBoogieUtils::AssignParam{rhsResult.expr, _node.annotation().type, nullptr},
						Token::Assign, &_node, m_context);
				for (auto stmt: res.newStmts)
					addSideEffect(stmt);
			}
			// Result is the tmp variable (if the assignment is part of an expression)
			m_currentExpr = tempVar->getRefTo();
		}
		break;
	default:
		m_context.reportError(&_node, string("Unsupported unary operator: ") + TokenTraits::toString(_node.getOperator()));
		m_currentExpr = bg::Expr::id(ASTBoogieUtils::ERR_EXPR);
		break;
	}

	return false;
}

bool ASTBoogieExpressionConverter::visit(BinaryOperation const& _node)
{
	// Check if constant propagation could infer the result
	TypePointer tp = _node.annotation().type;
	if (auto tpRational = dynamic_cast<RationalNumberType const*>(tp))
	{
		auto value = tpRational->literalValue(nullptr);
		if (tpRational->isNegative())
			m_currentExpr = bg::Expr::lit(bg::bigint(u2s(value)));
		else
			m_currentExpr = bg::Expr::lit(bg::bigint(value));
		return false;
	}

	// Get lhs recursively
	_node.leftExpression().accept(*this);
	bg::Expr::Ref lhs = m_currentExpr;

	// Get rhs recursively
	_node.rightExpression().accept(*this);
	bg::Expr::Ref rhs = m_currentExpr;

	// Common type might not be equal to the type of the node, e.g., in case of uint32 == uint64,
	// the common type is uint64, but the type of the node is bool
	auto commonType = _node.leftExpression().annotation().type->binaryOperatorResult(_node.getOperator(), _node.rightExpression().annotation().type);

	// Check implicit conversion for bitvectors
	if (m_context.isBvEncoding() && ASTBoogieUtils::isBitPreciseType(commonType))
	{
		lhs = ASTBoogieUtils::checkImplicitBvConversion(lhs, _node.leftExpression().annotation().type, commonType, m_context);
		rhs = ASTBoogieUtils::checkImplicitBvConversion(rhs, _node.rightExpression().annotation().type, commonType, m_context);
	}

	auto op = _node.getOperator();
	switch(op)
	{
	// Non-arithmetic operations
	case Token::And: m_currentExpr = bg::Expr::and_(lhs, rhs); break;
	case Token::Or: m_currentExpr = bg::Expr::or_(lhs, rhs); break;
	case Token::Equal: m_currentExpr = bg::Expr::eq(lhs, rhs); break;
	case Token::NotEqual: m_currentExpr = bg::Expr::neq(lhs, rhs); break;

	// Arithmetic operations
	case Token::Add:
	case Token::Sub:
	case Token::Mul:
	case Token::Div:
	case Token::Mod:
	case Token::Exp:

	case Token::LessThan:
	case Token::GreaterThan:
	case Token::LessThanOrEqual:
	case Token::GreaterThanOrEqual:

	case Token::BitAnd:
	case Token::BitOr:
	case Token::BitXor:
	case Token::SHL:
	case Token::SAR: {
		unsigned bits = ASTBoogieUtils::getBits(commonType);
		bool isSigned = ASTBoogieUtils::isSigned(commonType);
		auto exprResult = ASTBoogieUtils::encodeArithBinaryOp(m_context, &_node, _node.getOperator(), lhs, rhs, bits, isSigned);
		m_currentExpr = exprResult.expr;
		if (m_context.overflow() && exprResult.cc)
			m_ocs.push_back(exprResult.cc);
		break;
	}

	default:
		m_context.reportError(&_node, string("Unsupported binary operator ") + TokenTraits::toString(_node.getOperator()));
		m_currentExpr = bg::Expr::id(ASTBoogieUtils::ERR_EXPR);
		break;
	}

	return false;
}

bool ASTBoogieExpressionConverter::visit(FunctionCall const& _node)
{
	// Check for conversions
	if (_node.annotation().kind == FunctionCallKind::TypeConversion)
	{
		functionCallConversion(_node);
		return false;
	}
	// Function calls in Boogie are statements and cannot be part of
	// expressions, therefore each function call is given a fresh variable
	// for its return value and is mapped to a call statement

	// First, process the expression of the function call, which should give:
	// - The name of the called function in 'm_currentExpr'
	// - The address on which the function is called in 'm_currentAddress'
	// - The msg.value in 'm_currentMsgValue'
	// Example: f(z) gives 'f' as the name and 'this' as the address
	// Example: x.f(z) gives 'f' as the name and 'x' as the address
	// Example: x.f.value(y)(z) gives 'f' as the name, 'x' as the address and 'y' as the value

	// Check for the special case of calling the 'value' function
	// For example x.f.value(y)(z) should be treated as x.f(z), while setting
	// 'm_currentMsgValue' to 'y'.
	if (auto exprMa = dynamic_cast<MemberAccess const*>(&_node.expression()))
	{
		if (exprMa->memberName() == "value")
		{
			// Process the argument
			solAssert(_node.arguments().size() == 1, "Call to the value function should have exactly one argument");
			auto arg = *_node.arguments().begin();
			arg->accept(*this);
			m_currentMsgValue = m_currentExpr;
			if (m_context.isBvEncoding())
			{
				TypePointer tp_uint256 = TypeProvider::integer(256, IntegerType::Modifier::Unsigned);
				m_currentMsgValue = ASTBoogieUtils::checkImplicitBvConversion(m_currentMsgValue,
						arg->annotation().type, tp_uint256, m_context);
			}

			// Continue with the rest of the AST
			exprMa->expression().accept(*this);
			return false;
		}
	}

	// Ignore gas setting, e.g., x.f.gas(y)(z) is just x.f(z)
	if (auto exprMa = dynamic_cast<MemberAccess const*>(&_node.expression()))
	{
		if (exprMa->memberName() == "gas")
		{
			m_context.reportWarning(exprMa, "Ignored call to gas() function.");
			exprMa->expression().accept(*this);
			return false;
		}
	}

	m_currentExpr = nullptr;
	m_currentAddress = m_context.boogieThis()->getRefTo();
	m_currentMsgValue = nullptr;
	m_isGetter = false;
	m_isLibraryCall = false;

	// Special case for new array
	if (auto newExpr = dynamic_cast<NewExpression const*>(&_node.expression()))
	{
		if (dynamic_cast<ArrayTypeName const*>(&newExpr->typeName()))
		{
			functionCallNewArray(_node);
			return false;
		}
	}

	if (auto memAccExpr = dynamic_cast<MemberAccess const*>(&_node.expression()))
	{
		// array.push
		auto arrType = dynamic_cast<ArrayType const*>(memAccExpr->expression().annotation().type);
		if (arrType && (memAccExpr->memberName() == "push" || memAccExpr->memberName() == "pop"))
		{
			functionCallPushPop(memAccExpr, arrType, _node);
			return false;
		}
	}

	// Process expression
	_node.expression().accept(*this);

	if (m_isGetter)
		return false; // Result is already in the current expr

	// 'm_currentExpr' should be an identifier, giving the name of the function
	string funcName;
	if (auto varExpr = dynamic_pointer_cast<bg::VarExpr const>(m_currentExpr))
		funcName = varExpr->name();
	else
	{
		m_context.reportError(&_node, "Only identifiers are supported as function calls");
		funcName = ASTBoogieUtils::ERR_EXPR;
	}

	if (m_isGetter && _node.arguments().size() > 0)
		m_context.reportError(&_node, "Getter arguments are not supported");

	// Process arguments recursively
	vector<bg::Expr::Ref> allArgs;
	vector<bg::Expr::Ref> regularArgs;
	// First, pass extra arguments
	if (m_isLibraryCall)
		allArgs.push_back(m_context.boogieThis()->getRefTo()); // this
	else
		allArgs.push_back(m_currentAddress); // this
	// msg.sender is by default this, except for internal calls
	auto sender = m_context.boogieThis()->getRefTo();
	if (auto funcType = dynamic_cast<FunctionType const*>(_node.expression().annotation().type))
		if (funcType->kind() == FunctionType::Kind::Internal)
			sender = m_context.boogieMsgSender()->getRefTo();
	allArgs.push_back(sender); // msg.sender
	bg::Expr::Ref defaultMsgValue = m_context.intLit(0, 256);
	bg::Expr::Ref msgValue = m_currentMsgValue ? m_currentMsgValue : defaultMsgValue;
	allArgs.push_back(msgValue); // msg.value
	if (m_isLibraryCall && !m_isLibraryCallStatic)
		allArgs.push_back(m_currentAddress); // Non-static library calls require extra argument

	for (unsigned i = 0; i < _node.arguments().size(); ++i)
	{
		auto arg = _node.arguments()[i];
		arg->accept(*this);

		if (auto funcType = dynamic_cast<FunctionType const*>(_node.expression().annotation().type))
		{
			// Check for implicit conversions
			if (funcType->parameterTypes().size() > i && funcType->parameterTypes()[i] != arg->annotation().type &&
					funcName != ASTBoogieUtils::BOOGIE_CALL &&
					!boost::algorithm::starts_with(funcName, ASTBoogieUtils::VERIFIER_OLD) &&
					!boost::algorithm::starts_with(funcName, ASTBoogieUtils::VERIFIER_SUM))
			{
				// Introduce temp variable, make the assignment, including conversions
				auto argDecl = m_context.freshTempVar(m_context.toBoogieType(funcType->parameterTypes()[i], arg.get()), "call_arg");
				m_newDecls.push_back(argDecl);
				auto ar = ASTBoogieUtils::makeAssign(
						ASTBoogieUtils::AssignParam{argDecl->getRefTo(), funcType->parameterTypes()[i], nullptr },
						ASTBoogieUtils::AssignParam{m_currentExpr, arg->annotation().type, arg.get() },
						Token::Assign, &_node, m_context);
				m_newDecls.insert(m_newDecls.end(), ar.newDecls.begin(), ar.newDecls.end());
				for (auto stmt: ar.newStmts)
					addSideEffect(stmt);
				m_currentExpr = argDecl->getRefTo();
			}
		}

		// Do not add argument for call
		if (funcName != ASTBoogieUtils::BOOGIE_CALL)
		{
			allArgs.push_back(m_currentExpr);
			regularArgs.push_back(m_currentExpr);
		}
	}

	// Check for calls to special functions

	// Assert is a separate statement in Boogie (instead of a function call)
	if (funcName == ASTBoogieUtils::SOLIDITY_ASSERT)
	{
		solAssert(_node.arguments().size() == 1, "Assert should have exactly one argument");
		// Parameter of assert is the first (and only) normal argument
		addSideEffect(bg::Stmt::assert_(regularArgs[0], ASTBoogieUtils::createAttrs(_node.location(), "Assertion might not hold.", *m_context.currentScanner())));
		return false;
	}

	// Require is mapped to assume statement in Boogie (instead of a function call)
	if (funcName == ASTBoogieUtils::SOLIDITY_REQUIRE)
	{
		solAssert(1 <= _node.arguments().size() && _node.arguments().size() <=2, "Require should have one or two argument(s)");
		// Parameter of assume is the first normal argument (second is optional message omitted in Boogie)
		addSideEffect(bg::Stmt::assume(regularArgs[0]));
		return false;
	}

	// Revert is mapped to assume(false) statement in Boogie (instead of a function call)
	// Its argument is an optional message, omitted in Boogie
	if (funcName == ASTBoogieUtils::SOLIDITY_REVERT)
	{
		solAssert(_node.arguments().size() <= 1, "Revert should have at most one argument");
		addSideEffect(bg::Stmt::assume(bg::Expr::lit(false)));
		return false;
	}

	// Sum function
	if (boost::algorithm::starts_with(funcName, ASTBoogieUtils::VERIFIER_SUM))
	{
		solAssert(regularArgs.size() == 1, "Sum should have exactly one argument");
		functionCallSum(_node, regularArgs[0]);
		return false;
	}

	// Old function
	if (boost::algorithm::starts_with(funcName, ASTBoogieUtils::VERIFIER_OLD))
	{
		functionCallOld(_node, regularArgs);
		return false;
	}

	// Eq function
	if (boost::algorithm::starts_with(funcName, ASTBoogieUtils::VERIFIER_EQ))
	{
		functionCallEq(_node, regularArgs);
		return false;
	}

	// Struct initialization
	if (auto exprId = dynamic_cast<Identifier const*>(&_node.expression()))
	{
		if (auto structDef = dynamic_cast<StructDefinition const *>(exprId->annotation().referencedDeclaration))
		{
			functionCallNewStruct(structDef, regularArgs);
			return false;
		}
	}

	// If msg.value was set, we should reduce our own balance (and the called function will increase its own)
	if (msgValue != defaultMsgValue) functionCallReduceBalance(msgValue);

	// External calls require the invariants to hold
	if (funcName == ASTBoogieUtils::BOOGIE_CALL)
	{
		for (auto invar: m_context.currentContractInvars())
		{
			for (auto tcc: invar.tccs)
			{
				addSideEffect(bg::Stmt::assert_(tcc,
						ASTBoogieUtils::createAttrs(_node.location(), "Variables for invariant '" + invar.exprStr + "' might be out of range before external call.", *m_context.currentScanner())));
			}
			addSideEffect(bg::Stmt::assert_(invar.expr,
					ASTBoogieUtils::createAttrs(_node.location(), "Invariant '" + invar.exprStr + "' might not hold before external call.", *m_context.currentScanner())));
		}
	}

	auto returnType = _node.annotation().type;
	auto returnTupleType = dynamic_cast<TupleType const*>(returnType);

	// Create fresh variables to store the result of the function call
	std::vector<std::string> returnVarNames;
	std::vector<bg::Expr::Ref> returnVars;
	if (returnTupleType)
	{
		auto const& returnTypes = returnTupleType->components();
		solAssert(returnTypes.size() != 1, "");
		for (size_t i = 0; i < returnTypes.size(); ++ i)
		{
			auto varDecl = m_context.freshTempVar(m_context.toBoogieType(returnTypes[i], &_node), funcName + "_ret");
			m_newDecls.push_back(varDecl);
			returnVarNames.push_back(varDecl->getName());
			returnVars.push_back(varDecl->getRefTo());
		}
	}
	else
	{
		// New expressions already create the fresh variable
		if (!dynamic_cast<NewExpression const*>(&_node.expression()))
		{
			auto varDecl = m_context.freshTempVar(m_context.toBoogieType(returnType, &_node), funcName + "_ret");
			m_newDecls.push_back(varDecl);
			returnVarNames.push_back(varDecl->getName());
			returnVars.push_back(varDecl->getRefTo());
		}
	}
	// Assign call to the fresh variable
	addSideEffects({
		bg::Stmt::annot(ASTBoogieUtils::createAttrs(_node.location(), "", *m_context.currentScanner())),
		bg::Stmt::call(funcName, allArgs, returnVarNames)
	});

	// Result is the none, single variable, or a tuple of variables
	if (returnVars.size() == 0)
	{
		// For new expressions there is no return value, but the address should be used
		if (dynamic_cast<NewExpression const*>(&_node.expression())) m_currentExpr = m_currentAddress;
		else m_currentExpr = nullptr;
	}
	else if (returnVars.size() == 1)
	{
		m_currentExpr = returnVars[0];
	}
	else
	{
		m_currentExpr = bg::Expr::tuple(returnVars);
	}

	// Assume invariants after external call
	if (funcName == ASTBoogieUtils::BOOGIE_CALL)
	{
		for (auto invar: m_context.currentContractInvars())
		{
			for (auto tcc: invar.tccs)
				addSideEffect(bg::Stmt::assume(tcc));
			addSideEffect(bg::Stmt::assume(invar.expr));
		}
	}

	// The call function is special as it indicates failure in a return value and in this case
	// we must undo reducing our balance
	if (funcName == ASTBoogieUtils::BOOGIE_CALL && msgValue != defaultMsgValue)
		functionCallRevertBalance(msgValue);

	return false;
}

void ASTBoogieExpressionConverter::functionCallConversion(FunctionCall const& _node)
{
	solAssert(_node.arguments().size() == 1, "Type conversion should have exactly one argument");
	auto arg = _node.arguments()[0];
	// Converting to address
	bool toAddress = false;
	if (auto expr = dynamic_cast<ElementaryTypeNameExpression const*>(&_node.expression()))
		if (expr->typeName().token() == Token::Address)
			toAddress = true;

	// Converting to other kind of contract
	if (auto expr = dynamic_cast<Identifier const*>(&_node.expression()))
		if (dynamic_cast<ContractDefinition const *>(expr->annotation().referencedDeclaration))
			toAddress = true;

	if (toAddress)
	{
		arg->accept(*this);
		return;
	}

	bool converted = false;
	bg::TypeDeclRef targetType = m_context.toBoogieType(_node.annotation().type, &_node);
	bg::TypeDeclRef sourceType = m_context.toBoogieType(arg->annotation().type, arg.get());
	// Nothing to do when the two types are mapped to same type in Boogie,
	// e.g., conversion from uint256 to int256 if both are mapped to int
	// TODO: check the type instance instead of name as soon as it is a singleton
	if (targetType->getName() == sourceType->getName() || (targetType->getName() == "int" && sourceType->getName() == "int_const"))
	{
		arg->accept(*this);
		converted = true;
	}

	if (m_context.isBvEncoding() && ASTBoogieUtils::isBitPreciseType(_node.annotation().type))
	{
		arg->accept(*this);
		m_currentExpr = ASTBoogieUtils::checkExplicitBvConversion(m_currentExpr, arg->annotation().type, _node.annotation().type, m_context);
		converted = true;
	}

	if (converted)
	{
		// Range assertion for enums
		if (auto exprId = dynamic_cast<Identifier const*>(&_node.expression()))
		{
			if (auto enumDef = dynamic_cast<EnumDefinition const*>(exprId->annotation().referencedDeclaration))
			{
				m_newStatements.push_back(bg::Stmt::assert_(
						bg::Expr::and_(
								ASTBoogieUtils::encodeArithBinaryOp(m_context, &_node, langutil::Token::LessThanOrEqual,
										m_context.intLit(0, 256), m_currentExpr, 256, false).expr,
								ASTBoogieUtils::encodeArithBinaryOp(m_context, &_node, langutil::Token::LessThan,
										m_currentExpr, m_context.intLit(enumDef->members().size(), 256), 256, false).expr),
						ASTBoogieUtils::createAttrs(_node.location(), "Conversion to enum might be out of range", *m_context.currentScanner())));
			}
		}
	}
	else
	{
		m_context.reportError(&_node, "Unsupported type conversion");
		m_currentExpr = bg::Expr::id(ASTBoogieUtils::ERR_EXPR);
	}
}

void ASTBoogieExpressionConverter::functionCallNewStruct(StructDefinition const* structDef, vector<bg::Expr::Ref> const& args)
{
	auto varDecl = ASTBoogieUtils::newStruct(structDef, m_context);
	m_newDecls.push_back(varDecl);
	// Initialize each member
	for (size_t i = 0; i < structDef->members().size() && i < args.size(); ++i)
	{
		auto member = bg::Expr::id(m_context.mapDeclName(*structDef->members()[i]));
		auto init = bg::Expr::arrupd(member, varDecl->getRefTo(), args[i]);
		m_newStatements.push_back(bg::Stmt::assign(member, init));
	}
	// TODO: members not provided?
	// Return the address
	m_currentExpr = varDecl->getRefTo();
}

void ASTBoogieExpressionConverter::functionCallReduceBalance(bg::Expr::Ref msgValue)
{
	TypePointer tp_uint256 = TypeProvider::integer(256, IntegerType::Modifier::Unsigned);
	// assert(balance[this] >= msg.value)
	auto selExpr = bg::Expr::arrsel(m_context.boogieBalance()->getRefTo(), m_context.boogieThis()->getRefTo());
	auto geqResult = ASTBoogieUtils::encodeArithBinaryOp(m_context, nullptr,
			langutil::Token::GreaterThanOrEqual, selExpr, msgValue, 256, false);
	addSideEffect(bg::Stmt::comment("Implicit assumption that we have enough ether"));
	addSideEffect(bg::Stmt::assume(geqResult.expr));
	// balance[this] -= msg.value
	bg::Expr::Ref this_balance = bg::Expr::arrsel(m_context.boogieBalance()->getRefTo(),
			m_context.boogieThis()->getRefTo());
	if (m_context.encoding() == BoogieContext::Encoding::MOD)
	{
		addSideEffect(bg::Stmt::assume(ASTBoogieUtils::getTCCforExpr(this_balance, tp_uint256)));
		addSideEffect(bg::Stmt::assume(ASTBoogieUtils::getTCCforExpr(msgValue, tp_uint256)));
	}
	auto subResult = ASTBoogieUtils::encodeArithBinaryOp(m_context, nullptr, Token::Sub,
			this_balance, msgValue, 256, false);
	if (m_context.encoding() == BoogieContext::Encoding::MOD)
	{
		addSideEffect(bg::Stmt::comment("Implicit assumption that balances cannot overflow"));
		addSideEffect(bg::Stmt::assume(subResult.cc));
	}
	addSideEffect(
			bg::Stmt::assign(m_context.boogieBalance()->getRefTo(),
					bg::Expr::arrupd(m_context.boogieBalance()->getRefTo(),
							m_context.boogieThis()->getRefTo(), subResult.expr)));
}

void ASTBoogieExpressionConverter::functionCallRevertBalance(bg::Expr::Ref msgValue)
{
	TypePointer tp_uint256 = TypeProvider::integer(256, IntegerType::Modifier::Unsigned);
	bg::Block::Ref revert = bg::Block::block();
	// balance[this] += msg.value
	bg::Expr::Ref this_balance = bg::Expr::arrsel(m_context.boogieBalance()->getRefTo(), m_context.boogieThis()->getRefTo());
	if (m_context.encoding() == BoogieContext::Encoding::MOD)
		revert->addStmts( { bg::Stmt::assume(ASTBoogieUtils::getTCCforExpr(this_balance, tp_uint256)),
			bg::Stmt::assume(ASTBoogieUtils::getTCCforExpr(msgValue, tp_uint256)) });

	auto addResult = ASTBoogieUtils::encodeArithBinaryOp(m_context, nullptr, Token::Add,
			this_balance, msgValue, 256, false);
	if (m_context.encoding() == BoogieContext::Encoding::MOD)
		revert->addStmts( { bg::Stmt::comment("Implicit assumption that balances cannot overflow"),
			bg::Stmt::assume(addResult.cc) });

	revert->addStmt(
			bg::Stmt::assign(m_context.boogieBalance()->getRefTo(),
					bg::Expr::arrupd(m_context.boogieBalance()->getRefTo(),
							m_context.boogieThis()->getRefTo(), addResult.expr)));
	// Final statement for balance update in case of failure. Return value of call
	// is always a tuple (ok, data).
	auto okDataTuple = dynamic_pointer_cast<bg::TupleExpr const>(m_currentExpr);
	solAssert(okDataTuple, "");
	addSideEffect(bg::Stmt::ifelse(bg::Expr::not_(okDataTuple->elements()[0]), revert));
}

void ASTBoogieExpressionConverter::functionCallSum(FunctionCall const& _node, bg::Expr::Ref arg)
{
	m_currentExpr = m_context.getSumVar(arg, _node.arguments()[0].get(), _node.annotation().type);
	addTCC(m_currentExpr, _node.annotation().type);
}

void ASTBoogieExpressionConverter::functionCallOld(FunctionCall const& _node, vector<bg::Expr::Ref> const& args)
{
	solAssert(args.size() == 1, "Verifier old function must have exactly one argument");
	m_currentExpr = bg::Expr::old(args[0]);
	addTCC(m_currentExpr, _node.annotation().type);
}

void ASTBoogieExpressionConverter::functionCallEq(FunctionCall const& _node, vector<bg::Expr::Ref> const& args)
{
	if (args.size() != 2)
	{
		m_context.reportError(&_node, "Equality predicate must take exactly two arguments");
		return;
	}
	auto argType1 = _node.arguments()[0]->annotation().type;
	auto argType2 = _node.arguments()[1]->annotation().type;
	if (*argType1 != *argType2)
	{
		m_context.reportError(&_node, "Arguments must have the same type");
		return;
	}
	if (argType1->isValueType() && argType2->isValueType())
		m_context.reportWarning(&_node, "Use operator == for comparing value types");
	m_currentExpr = bg::Expr::eq(args[0], args[1]);
	addTCC(m_currentExpr, _node.annotation().type);
}

void ASTBoogieExpressionConverter::functionCallNewArray(FunctionCall const& _node)
{
	auto arrType = dynamic_cast<ArrayType const*>(_node.annotation().type);
	auto varDecl = ASTBoogieUtils::newArray(m_context.toBoogieType(_node.annotation().type, &_node), m_context);
	m_newDecls.push_back(varDecl);
	auto bgType = m_context.toBoogieType(arrType->baseType(), &_node);
	auto memArr = m_context.getMemArray(varDecl->getRefTo(), bgType);
	auto arrLen = m_context.getArrayLength(memArr, bgType);
	// Set length
	solAssert(_node.arguments().size() == 1, "Array initializer must have exactly one argument for size.");
	_node.arguments()[0]->accept(*this);
	addSideEffect(bg::Stmt::assign(arrLen, m_currentExpr));

	if (auto lit = dynamic_pointer_cast<bg::IntLit const>(m_currentExpr))
	{
		auto defaultVal = ASTBoogieUtils::defaultValue(arrType->baseType(), m_context);
		if (defaultVal)
		{
			for (size_t i = 0; i < lit->getVal(); i++)
				addSideEffect(bg::Stmt::assign(
						bg::Expr::arrsel(
								m_context.getInnerArray(memArr, bgType),
								m_context.intLit(i, 256)),
						defaultVal));
		}
		else
			m_context.reportWarning(&_node, "Could not set default values for array elements");
	}
	else
		m_context.reportWarning(&_node, "Array size not known at compile time, elements could not be set to default value");

	m_currentExpr = varDecl->getRefTo();
}

void ASTBoogieExpressionConverter::functionCallPushPop(MemberAccess const* memAccExpr, ArrayType const* arrType, FunctionCall const& _node)
{
	solAssert(arrType->dataStoredIn(DataLocation::Storage), "Push/pop to non-storage array");
	auto bgType = m_context.toBoogieType(arrType->baseType(), &_node);
	memAccExpr->expression().accept(*this);
	auto arr = m_currentExpr;
	auto len = m_context.getArrayLength(arr, bgType);
	ASTBoogieUtils::ExprWithCC lenUpd;
	if (memAccExpr->memberName() == "push")
	{
		solAssert(_node.arguments().size() == 1, "Push must take exactly one argument");
		_node.arguments()[0]->accept(*this);
		auto arg = m_currentExpr;
		// First we assign the default value (without updating the sum)
		addSideEffect(bg::Stmt::assign(
				bg::Expr::arrsel(m_context.getInnerArray(arr, bgType), len),
				ASTBoogieUtils::defaultValue(arrType->baseType(), m_context)));
		// Then we put the actual argument (updating also the sum)
		auto res = ASTBoogieUtils::makeAssign(
			ASTBoogieUtils::AssignParam{bg::Expr::arrsel(m_context.getInnerArray(arr, bgType), len), arrType->baseType(), nullptr},
			ASTBoogieUtils::AssignParam{arg, _node.arguments()[0]->annotation().type, _node.arguments()[0].get()},
			Token::Assign, &_node, m_context);
		m_newDecls.insert(m_newDecls.end(), res.newDecls.begin(), res.newDecls.end());
		for (auto stmt: res.newStmts)
			addSideEffect(stmt);
		lenUpd = ASTBoogieUtils::encodeArithBinaryOp(m_context, &_node, Token::Add, len, m_context.intLit(1, 256), 256, false);
	}
	else
	{
		solAssert(_node.arguments().size() == 0, "Pop must take no arguments");
		lenUpd = ASTBoogieUtils::encodeArithBinaryOp(m_context, &_node, Token::Sub, len, m_context.intLit(1, 256), 256, false);
	}
	addSideEffect(bg::Stmt::assign(len, lenUpd.expr));
	if (m_context.encoding() == BoogieContext::Encoding::MOD)
	{
		addSideEffect(bg::Stmt::comment("Implicit assumption that length cannot overflow"));
		addSideEffect(bg::Stmt::assume(lenUpd.cc));
	}
	if (memAccExpr->memberName() == "pop")
	{
		// Reset the removed element (updating the sum)
		auto res = ASTBoogieUtils::makeAssign(
			ASTBoogieUtils::AssignParam{bg::Expr::arrsel(m_context.getInnerArray(arr, bgType), len), arrType->baseType(), nullptr},
			ASTBoogieUtils::AssignParam{ASTBoogieUtils::defaultValue(arrType->baseType(), m_context), arrType->baseType(), nullptr},
			Token::Assign, &_node, m_context);
		m_newDecls.insert(m_newDecls.end(), res.newDecls.begin(), res.newDecls.end());
		for (auto stmt: res.newStmts)
			addSideEffect(stmt);
	}
}

bool ASTBoogieExpressionConverter::visit(NewExpression const& _node)
{

	if (auto userDefType = dynamic_cast<UserDefinedTypeName const*>(&_node.typeName()))
	{
		if (auto contract = dynamic_cast<ContractDefinition const*>(userDefType->annotation().referencedDeclaration))
		{
			// TODO: Make sure that it is a fresh address
			m_currentExpr = bg::Expr::id(ASTBoogieUtils::getConstructorName(contract));
			auto varDecl = m_context.freshTempVar(m_context.addressType(), "new");
			m_newDecls.push_back(varDecl);
			m_currentAddress = varDecl->getRefTo();
			return false;
		}
	}

	m_context.reportError(&_node, "Unsupported new expression");
	m_currentExpr = bg::Expr::id(ASTBoogieUtils::ERR_EXPR);
	return false;
}

bool ASTBoogieExpressionConverter::visit(MemberAccess const& _node)
{
	// Inline constants
	if (auto varDecl = dynamic_cast<VariableDeclaration const*>(_node.annotation().referencedDeclaration))
	{
		if (varDecl->isConstant())
		{
			varDecl->value()->accept(*this);
			return false;
		}
	}

	// Normally, the expression of the MemberAccess will give the address and
	// the memberName will give the name. For example, x.f() will have address
	// 'x' and name 'f'.

	// Get expression recursively
	_node.expression().accept(*this);
	bg::Expr::Ref expr = m_currentExpr;
	// The current expression gives the address on which something is done
	m_currentAddress = m_currentExpr;
	// Check for explicit scopings and replace with 'this'
	if (auto id = dynamic_cast<Identifier const*>(&_node.expression()))
	{
		auto refDecl = id->annotation().referencedDeclaration;
		// 'super'
		if (dynamic_cast<MagicVariableDeclaration const*>(refDecl) &&
				refDecl->name() == ASTBoogieUtils::SOLIDITY_SUPER)
			m_currentAddress = m_context.boogieThis()->getRefTo();
		// current contract name
		if (refDecl == m_context.currentContract())
			m_currentAddress = m_context.boogieThis()->getRefTo();
		// any base contract name
		auto bases = m_context.currentContract()->annotation().linearizedBaseContracts;
		if (std::find(bases.begin(), bases.end(), refDecl) != bases.end())
			m_currentAddress = m_context.boogieThis()->getRefTo();
	}

	// Type of the expression
	TypePointer type = _node.expression().annotation().type;
	Type::Category typeCategory = type->category();

	// Handle special members/functions
	TypePointer tp_uint256 = TypeProvider::integer(256, IntegerType::Modifier::Unsigned);

	// address.balance / this.balance
	bool isAddress = typeCategory == Type::Category::Address;
	if (isAddress && _node.memberName() == ASTBoogieUtils::SOLIDITY_BALANCE)
	{
		m_currentExpr = bg::Expr::arrsel(m_context.boogieBalance()->getRefTo(), expr);
		addTCC(m_currentExpr, tp_uint256);
		return false;
	}
	// address.transfer()
	if (isAddress && _node.memberName() == ASTBoogieUtils::SOLIDITY_TRANSFER)
	{
		m_context.includeTransferFunction();
		m_currentExpr = bg::Expr::id(ASTBoogieUtils::BOOGIE_TRANSFER);
		return false;
	}
	// address.send()
	if (isAddress && _node.memberName() == ASTBoogieUtils::SOLIDITY_SEND)
	{
		m_context.includeSendFunction();
		m_currentExpr = bg::Expr::id(ASTBoogieUtils::BOOGIE_SEND);
		return false;
	}
	// address.call()
	if (isAddress && _node.memberName() == ASTBoogieUtils::SOLIDITY_CALL)
	{
		m_context.includeCallFunction();
		m_currentExpr = bg::Expr::id(ASTBoogieUtils::BOOGIE_CALL);
		return false;
	}
	// msg.sender
	auto magicType = dynamic_cast<MagicType const*>(type);
	bool isMessage = magicType != nullptr && magicType->kind() == MagicType::Kind::Message;
	if (isMessage && _node.memberName() == ASTBoogieUtils::SOLIDITY_SENDER)
	{
		m_currentExpr = m_context.boogieMsgSender()->getRefTo();
		return false;
	}
	// msg.value
	if (isMessage && _node.memberName() == ASTBoogieUtils::SOLIDITY_VALUE)
	{
		m_currentExpr = m_context.boogieMsgValue()->getRefTo();
		TypePointer tp_uint256 = TypeProvider::integer(256, IntegerType::Modifier::Unsigned);
		addTCC(m_currentExpr, tp_uint256);
		return false;
	}
	// block
	bool isBlock = magicType != nullptr && magicType->kind() == MagicType::Kind::Block;
	// block.number
	if (isBlock && _node.memberName() == ASTBoogieUtils::SOLIDITY_NUMBER)
	{
		m_currentExpr = bg::Expr::id(ASTBoogieUtils::BOOGIE_BLOCKNO);
		return false;
	}
	// array.length
	bool isArray = type->category() == Type::Category::Array;
	if (isArray && _node.memberName() == "length")
	{
		auto arrType = dynamic_cast<ArrayType const*>(type);
		if (type->dataStoredIn(DataLocation::Memory) || type->dataStoredIn(DataLocation::CallData))
		{
			m_currentExpr = m_context.getMemArray(m_currentExpr, m_context.toBoogieType(arrType->baseType(), &_node));
		}
		m_currentExpr = m_context.getArrayLength(m_currentExpr, m_context.toBoogieType(arrType->baseType(), &_node));
		addTCC(m_currentExpr, tp_uint256);
		return false;
	}
	// fixed size byte array length
	if (type->category() == Type::Category::FixedBytes && _node.memberName() == "length")
	{
		auto fbType = dynamic_cast<FixedBytesType const*>(_node.expression().annotation().type);
		m_currentExpr = bg::Expr::lit(fbType->numBytes());
		return false;
	}

	// Enums
	if (_node.annotation().type->category() == Type::Category::Enum)
	{
		// Try to get the enum definition
		EnumDefinition const* enumDef = nullptr;
		if (auto exprId = dynamic_cast<Identifier const*>(&_node.expression()))
			enumDef = dynamic_cast<EnumDefinition const*>(exprId->annotation().referencedDeclaration);
		if (auto exprMemAcc = dynamic_cast<MemberAccess const*>(&_node.expression()))
			enumDef = dynamic_cast<EnumDefinition const*>(exprMemAcc->annotation().referencedDeclaration);

		if (enumDef)
		{
			// TODO: better way to get index?
			for (size_t i = 0; i < enumDef->members().size(); ++i)
			{
				if (enumDef->members()[i]->name() == _node.memberName())
				{
					m_currentExpr = m_context.intLit(i, 256);
					return false;
				}
			}
			solAssert(false, "Enum member not found");
		}
		else
			solAssert(false, "Enum definition not found");
	}

	// Non-special member access: 'referencedDeclaration' should point to the
	// declaration corresponding to 'memberName'
	if (_node.annotation().referencedDeclaration == nullptr)
	{
		m_context.reportError(&_node, "Member without corresponding declaration: " + _node.memberName());
		m_currentExpr = bg::Expr::id(ASTBoogieUtils::ERR_EXPR);
		return false;
	}
	m_currentExpr = bg::Expr::id(m_context.mapDeclName(*_node.annotation().referencedDeclaration));
	// Check for getter
	m_isGetter =  dynamic_cast<VariableDeclaration const*>(_node.annotation().referencedDeclaration);
	if (m_isGetter)
		m_currentExpr = bg::Expr::arrsel(m_currentExpr, m_currentAddress);

	// Check for library call
	m_isLibraryCall = false;
	if (auto fDef = dynamic_cast<FunctionDefinition const*>(_node.annotation().referencedDeclaration))
	{
		m_isLibraryCall = fDef->inContractKind() == ContractDefinition::ContractKind::Library;
		if (m_isLibraryCall)
		{
			// Check if library call is static (e.g., Math.add(1, 2)) or not (e.g., 1.add(2))
			m_isLibraryCallStatic = false;
			if (auto exprId = dynamic_cast<Identifier const *>(&_node.expression()))
			{
				if (dynamic_cast<ContractDefinition const *>(exprId->annotation().referencedDeclaration))
					m_isLibraryCallStatic = true;
			}
			return false;
		}
	}

	// Member access on structures: create selector expression
	if (typeCategory == Type::Category::Struct)
	{
		auto structType = dynamic_cast<StructType const*>(type);
		if (structType->location() == DataLocation::Memory || structType->location() == DataLocation::CallData)
		{
			m_currentExpr = bg::Expr::id(m_context.mapDeclName(*_node.annotation().referencedDeclaration));
			m_currentExpr = bg::Expr::arrsel(m_currentExpr, m_currentAddress);
		}
		else if (structType->location() == DataLocation::Storage)
		{
			// Local pointers: unpack first
			if (structType->dataStoredIn(DataLocation::Storage) && structType->isPointer())
				if (auto id = dynamic_cast<Identifier const*>(&_node.expression()))
					m_currentAddress = ASTBoogieUtils::unpackLocalPtr(id, m_context);

			m_currentExpr = bg::Expr::dtsel(m_currentAddress,
					m_context.mapDeclName(*_node.annotation().referencedDeclaration),
					m_context.getStructConstructor(&structType->structDefinition()),
					dynamic_pointer_cast<bg::DataTypeDecl>(m_context.getStructType(&structType->structDefinition(), structType->location())));
		}
		else
		{
			m_context.reportError(&_node, "Member access unsupported for location " + ASTBoogieUtils::dataLocToStr(structType->location()));
			m_currentExpr = bg::Expr::id(ASTBoogieUtils::ERR_EXPR);
		}
		return false;
	}

	return false;
}

bool ASTBoogieExpressionConverter::visit(IndexAccess const& _node)
{
	Expression const& base = _node.baseExpression();
	base.accept(*this);
	bg::Expr::Ref baseExpr = m_currentExpr;

	Expression const* index = _node.indexExpression();
	index->accept(*this); // TODO: can this be a nullptr?
	bg::Expr::Ref indexExpr = m_currentExpr;

	TypePointer baseType = base.annotation().type;
	TypePointer indexType = index->annotation().type;

	// Fixed size byte arrays
	if (baseType->category() == Type::Category::FixedBytes)
	{
		auto fbType = dynamic_cast<FixedBytesType const*>(baseType);
		unsigned fbSize = fbType->numBytes();

		// Check bounds (typechecked for unsigned, so >= 0)
		addSideEffect(bg::Stmt::assume(bg::Expr::gte(indexExpr, bg::Expr::lit((unsigned)0))));
		addSideEffect(bg::Stmt::assert_(bg::Expr::lt(indexExpr, bg::Expr::lit(fbSize)),
					ASTBoogieUtils::createAttrs(_node.location(), "Index may be out of bounds", *m_context.currentScanner())));

		// Do a case split on which slice to use
		for (unsigned i = 0; i < fbSize; ++ i)
		{
			bg::Expr::Ref slice = m_context.intSlice(baseExpr, fbSize*8, (i+1)*8 - 1, i*8);
			if (i == 0)
				m_currentExpr = slice;
			else
			{
				m_currentExpr = bg::Expr::cond(
						bg::Expr::eq(indexExpr, bg::Expr::lit(i)),
						slice, m_currentExpr);
			}
		}
		return false;
	}

	if (baseType->category() == Type::Category::Array && m_context.isBvEncoding())
	{
		// For arrays, cast to uint
		TypePointer tp_uint256 = TypeProvider::integer(256, IntegerType::Modifier::Unsigned);
		indexExpr = ASTBoogieUtils::checkImplicitBvConversion(indexExpr, indexType, tp_uint256, m_context);
	}

	if (baseType->category() == Type::Category::Mapping && m_context.isBvEncoding())
	{
		// For mappings, do implicit conversion
		auto mappingType = dynamic_cast<MappingType const*>(baseType);
		indexExpr = ASTBoogieUtils::checkImplicitBvConversion(indexExpr, indexType, mappingType->keyType(), m_context);
	}

	// Indexing arrays requires accessing the actual array inside the datatype
	if (baseType->category() == Type::Category::Array)
	{
		auto arrType = dynamic_cast<ArrayType const*>(baseType);
		auto bgArrType = m_context.toBoogieType(arrType->baseType(), &_node);
		// Extra indirection for memory arrays
		if (baseType->dataStoredIn(DataLocation::Memory) || baseType->dataStoredIn(DataLocation::CallData))
		{
			baseExpr = m_context.getMemArray(baseExpr, bgArrType);
		}
		m_context.toBoogieType(baseType, &_node); // Make sure that types are declared
		baseExpr = m_context.getInnerArray(baseExpr, bgArrType);
	}

	// Index access is simply converted to a select in Boogie, which is fine
	// as long as it is not an LHS of an assignment (e.g., x[i] = v), but
	// that case is handled when converting assignments
	m_currentExpr = bg::Expr::arrsel(baseExpr, indexExpr);

	addTCC(m_currentExpr, _node.annotation().type);

	return false;
}

bool ASTBoogieExpressionConverter::visit(Identifier const& _node)
{
	if (_node.name() == ASTBoogieUtils::VERIFIER_SUM)
	{
		m_currentExpr = bg::Expr::id(ASTBoogieUtils::VERIFIER_SUM);
		return false;
	}

	if (!_node.annotation().referencedDeclaration)
	{
		m_context.reportError(&_node, "Identifier '" + _node.name() + "' has no matching declaration");
		m_currentExpr = bg::Expr::id(ASTBoogieUtils::ERR_EXPR);
		return false;
	}
	auto decl = _node.annotation().referencedDeclaration;

	// Inline constants
	if (auto varDecl = dynamic_cast<VariableDeclaration const*>(decl))
	{
		if (varDecl->isConstant())
		{
			varDecl->value()->accept(*this);
			return false;
		}
	}

	string declName = m_context.mapDeclName(*(decl));

	// State variables must be referenced by accessing the map
	auto varDecl = dynamic_cast<VariableDeclaration const*>(decl);
	if (varDecl && varDecl->isStateVariable())
		m_currentExpr = bg::Expr::arrsel(bg::Expr::id(declName), m_context.boogieThis()->getRefTo());
	// Other identifiers can be referenced by their name
	else
		m_currentExpr = bg::Expr::id(declName);

	addTCC(m_currentExpr, decl->type());

	return false;
}

bool ASTBoogieExpressionConverter::visit(ElementaryTypeNameExpression const& _node)
{
	m_context.reportError(&_node, "Unhandled node: ElementaryTypeNameExpression");
	m_currentExpr = bg::Expr::id(ASTBoogieUtils::ERR_EXPR);
	return false;
}

bool ASTBoogieExpressionConverter::visit(Literal const& _node)
{
	TypePointer type = _node.annotation().type;
	Type::Category typeCategory = type->category();

	switch (typeCategory)
	{
	case Type::Category::RationalNumber:
	{
		auto rationalType = dynamic_cast<RationalNumberType const*>(type);
		if (rationalType != nullptr)
		{
			m_currentExpr = bg::Expr::lit(rationalType->literalValue(nullptr));
			return false;
		}
		break;
	}
	case Type::Category::Bool:
		m_currentExpr = bg::Expr::lit(_node.value() == "true");
		return false;
	case Type::Category::Address:
	{
		m_currentExpr = m_context.getAddressLiteral(_node.value());
		return false;
	}
	case Type::Category::StringLiteral:
	{
		m_currentExpr = m_context.getStringLiteral(_node.value());
		return false;
	}
	default:
	{
		// Report unsupported
		string tpStr = type->toString();
		m_context.reportError(&_node, "Unsupported literal for type " + tpStr.substr(0, tpStr.find(' ')));
		m_currentExpr = bg::Expr::id(ASTBoogieUtils::ERR_EXPR);
		break;
	}
	}

	return false;
}

bool ASTBoogieExpressionConverter::visitNode(ASTNode const&)
{
	solAssert(false, "Unhandled node (unknown)");
	return true;
}

}
}
