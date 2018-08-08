#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <libsolidity/analysis/NameAndTypeResolver.h>
#include <libsolidity/analysis/TypeChecker.h>
#include <libsolidity/ast/ASTBoogieConverter.h>
#include <libsolidity/ast/ASTBoogieExpressionConverter.h>
#include <libsolidity/ast/ASTBoogieUtils.h>
#include <libsolidity/parsing/Parser.h>

using namespace std;
using namespace dev;
using namespace dev::solidity;

namespace dev
{
namespace solidity
{

const string ASTBoogieConverter::DOCTAG_CONTRACT_INVAR = "invariant";
const string ASTBoogieConverter::DOCTAG_CONTRACT_INVARS_INCLUDE = "{contractInvariants}";
const string ASTBoogieConverter::DOCTAG_LOOP_INVAR = "invariant";
const string ASTBoogieConverter::DOCTAG_PRECOND = "precondition";
const string ASTBoogieConverter::DOCTAG_POSTCOND = "postcondition";

void ASTBoogieConverter::addGlobalComment(string str)
{
	m_context.program().getDeclarations().push_back(smack::Decl::comment("", str));
}

const smack::Expr* ASTBoogieConverter::convertExpression(Expression const& _node)
{
	ASTBoogieExpressionConverter::Result result = ASTBoogieExpressionConverter(m_context).convert(_node);

	for (auto d : result.newDecls) { m_localDecls.push_back(d); }
	for (auto s : result.newStatements) { m_currentBlocks.top()->addStmt(s); }
	for (auto c : result.newConstants)
	{
		bool alreadyDefined = false;
		for (auto d : m_context.program().getDeclarations())
		{
			if (d->getName() == c->getName())
			{
				// TODO: check that other fields are equal
				alreadyDefined = true;
				break;
			}
		}
		if (!alreadyDefined) m_context.program().getDeclarations().push_back(c);
	}

	return result.expr;
}

void ASTBoogieConverter::createDefaultConstructor(ContractDefinition const& _node)
{
	addGlobalComment("");
	addGlobalComment("Default constructor");

	// Input parameters
	list<smack::Binding> params {
		{ASTBoogieUtils::BOOGIE_THIS, ASTBoogieUtils::BOOGIE_ADDRESS_TYPE}, // this
		{ASTBoogieUtils::BOOGIE_MSG_SENDER, ASTBoogieUtils::BOOGIE_ADDRESS_TYPE}, // msg.sender
		{ASTBoogieUtils::BOOGIE_MSG_VALUE, m_context.isBvEncoding() ? "bv256" : "int"} // msg.value
	};

	smack::Block* block = smack::Block::block();
	// State variable initializers should go to the beginning of the constructor
	for (auto i : m_stateVarInitializers) { block->addStmt(i); }
	m_stateVarInitializers.clear(); // Clear list so that we know that initializers have been included

	string funcName = ASTBoogieUtils::BOOGIE_CONSTRUCTOR + "#" + toString(_node.id());

	// Create the procedure
	auto procDecl = smack::Decl::procedure(funcName, params, {}, {}, {block});
	for (auto invar : m_context.currentContractInvars())
	{
		procDecl->getEnsures().push_back(smack::Specification::spec(invar.first,
				ASTBoogieUtils::createAttrs(_node.location(), "State variable initializers might violate invariant '" + invar.second + "'.", *m_context.currentScanner())));
	}
	procDecl->addAttrs(ASTBoogieUtils::createAttrs(_node.location(), "Default constructor", *m_context.currentScanner()));
	m_context.program().getDeclarations().push_back(procDecl);
}

map<smack::Expr const*, string> ASTBoogieConverter::getExprsFromDocTags(ASTNode const& _node, DocumentedAnnotation const& _annot, ASTNode const* _scope, string _tag)
{
	map<smack::Expr const*, string> exprs;
	// Process expressions
	// TODO: we use a different error reporter for the type checker to ignore
	// error messages related to our special functions like the __verifier_sum
	ErrorList typeCheckerErrList;
	ErrorReporter typeCheckerErrReporter(typeCheckerErrList);

	NameAndTypeResolver resolver(m_context.globalDecls(), m_context.scopes(), *m_context.errorReporter());
	TypeChecker typeChecker(m_context.evmVersion(), typeCheckerErrReporter, m_currentContract);

	for (auto docTag : _annot.docTags)
	{
		if (docTag.first == "notice" && boost::starts_with(docTag.second.content, _tag)) // Find expressions with the given tag
		{
			if (_tag == DOCTAG_CONTRACT_INVARS_INCLUDE) // Special tag to include contract invariants
			{
				// TODO: warning when currentinvars is empty
				for (auto invar : m_context.currentContractInvars()) { exprs[invar.first] = invar.second; }
			}
			else // Normal tags just parse the expression afterwards
			{
				// Parse
				string exprStr = docTag.second.content.substr(_tag.length() + 1);
				ASTPointer<Expression> expr = Parser(*m_context.errorReporter())
						.parseExpression(shared_ptr<Scanner>(new Scanner((CharStream)exprStr, m_currentContract->sourceUnitName())));
				// Resolve references, using the given scope
				m_context.scopes()[&*expr] = m_context.scopes()[_scope];
				resolver.resolveNamesAndTypes(*expr);
				// Do type checking
				typeChecker.checkTypeRequirements(*expr);
				// Convert expression to Boogie representation
				auto result = ASTBoogieExpressionConverter(m_context).convert(*expr);
				if (!result.newStatements.empty()) // Make sure that there are no side effects
				{
					m_context.reportError(&_node, "Annotation expression introduces intermediate statements");
				}
				if (!result.newDecls.empty()) // Make sure that there are no side effects
				{
					m_context.reportError(&_node, "Annotation expression introduces intermediate declarations");
				}
				exprs[result.expr] = exprStr;
			}
		}
	}

	return exprs;
}

ASTBoogieConverter::ASTBoogieConverter(BoogieContext& context) :
				m_context(context),
				m_currentRet(nullptr),
				m_nextReturnLabelId(0)
{
	// Initialize global declarations
	addGlobalComment("Global declarations and definitions related to the address type");
	// address type
	m_context.program().getDeclarations().push_back(smack::Decl::typee(ASTBoogieUtils::BOOGIE_ADDRESS_TYPE));
	m_context.program().getDeclarations().push_back(smack::Decl::constant(ASTBoogieUtils::BOOGIE_ZERO_ADDRESS, ASTBoogieUtils::BOOGIE_ADDRESS_TYPE, true));
	// address.balance
	m_context.program().getDeclarations().push_back(smack::Decl::variable(ASTBoogieUtils::BOOGIE_BALANCE,
			"[" + ASTBoogieUtils::BOOGIE_ADDRESS_TYPE + "]" + (m_context.isBvEncoding() ? "bv256" : "int")));
	// Uninterpreted type for strings
	m_context.program().getDeclarations().push_back(smack::Decl::typee(ASTBoogieUtils::BOOGIE_STRING_TYPE));
	// now
	m_context.program().getDeclarations().push_back(smack::Decl::variable(ASTBoogieUtils::BOOGIE_NOW, m_context.isBvEncoding() ? "bv256" : "int"));
}

// ---------------------------------------------------------------------------
//         Visitor methods for top-level nodes and declarations
// ---------------------------------------------------------------------------

bool ASTBoogieConverter::visit(SourceUnit const& _node)
{
	rememberScope(_node);

	// Boogie programs are flat, source units do not appear explicitly
	addGlobalComment("");
	addGlobalComment("------- Source: " + _node.annotation().path + " -------");
	return true; // Simply apply visitor recursively
}

bool ASTBoogieConverter::visit(PragmaDirective const& _node)
{
	rememberScope(_node);

	// Pragmas are only included as comments
	addGlobalComment("Pragma: " + boost::algorithm::join(_node.literals(), ""));
	return false;
}

bool ASTBoogieConverter::visit(ImportDirective const& _node)
{
	rememberScope(_node);

//	BOOST_THROW_EXCEPTION(InternalCompilerError() << errinfo_comment("Unhandled node: ImportDirective") << errinfo_sourceLocation(_node.location()));
	return false;
}

bool ASTBoogieConverter::visit(ContractDefinition const& _node)
{
	rememberScope(_node);

	m_currentContract = &_node;
	// Boogie programs are flat, contracts do not appear explicitly
	addGlobalComment("");
	addGlobalComment("------- Contract: " + _node.name() + " -------");

	// Process contract invariants
	m_context.currentContractInvars().clear();
	m_context.currentSumDecls().clear();

	for (auto invar : getExprsFromDocTags(_node, _node.annotation(), &_node, DOCTAG_CONTRACT_INVAR))
	{
		addGlobalComment("Contract invariant: " + invar.second);
		m_context.currentContractInvars()[invar.first] = invar.second;
	}

	// Add new shadow variables for sum
	for (auto sumDecl : m_context.currentSumDecls())
	{
		addGlobalComment("Shadow variable for sum over '" + sumDecl.first->name() + "'");
		m_context.program().getDeclarations().push_back(
					smack::Decl::variable(ASTBoogieUtils::mapDeclName(*sumDecl.first) + ASTBoogieUtils::BOOGIE_SUM,
					"[" + ASTBoogieUtils::BOOGIE_ADDRESS_TYPE + "]" +
					ASTBoogieUtils::mapType(sumDecl.second, *sumDecl.first, m_context)));
	}

	// Process state variables first (to get initializer expressions)
	m_stateVarInitializers.clear();
	for (auto sn : _node.subNodes())
	{
		if (dynamic_cast<VariableDeclaration const*>(&*sn)) { sn->accept(*this); }
	}

	// Process other elements
	for (auto sn : _node.subNodes())
	{
		if (!dynamic_cast<VariableDeclaration const*>(&*sn)) { sn->accept(*this); }
	}

	// If there are still initializers left, there was no constructor, so we create one
	if (!m_stateVarInitializers.empty()) { createDefaultConstructor(_node); }

	return false;
}

bool ASTBoogieConverter::visit(InheritanceSpecifier const& _node)
{
	rememberScope(_node);

	// TODO: calling constructor of superclass?
	// Boogie programs are flat, inheritance does not appear explicitly
	addGlobalComment("Inherits from: " + boost::algorithm::join(_node.name().namePath(), "#"));
	if (_node.arguments() && _node.arguments()->size() > 0)
	{
		m_context.reportError(&_node, "Arguments for base constructor in inheritance list is not supported");
	}
	return false;
}

bool ASTBoogieConverter::visit(UsingForDirective const& _node)
{
	rememberScope(_node);

	// Nothing to do with using for directives, calls to functions are resolved in the AST
	string libraryName = _node.libraryName().annotation().type->toString();
	string typeName = _node.typeName() ? _node.typeName()->annotation().type->toString() : "*";
	addGlobalComment("Using " + libraryName + " for " + typeName);
	return false;
}

bool ASTBoogieConverter::visit(StructDefinition const& _node)
{
	rememberScope(_node);

	m_context.reportError(&_node, "Struct definitions are not supported");
	return false;
}

bool ASTBoogieConverter::visit(EnumDefinition const& _node)
{
	rememberScope(_node);

	m_context.reportError(&_node, "Enum definitions are not supported");
	return false;
}

bool ASTBoogieConverter::visit(EnumValue const& _node)
{
	rememberScope(_node);

	BOOST_THROW_EXCEPTION(InternalCompilerError() << errinfo_comment("Unhandled node: EnumValue") << errinfo_sourceLocation(_node.location()));
	return false;
}

bool ASTBoogieConverter::visit(ParameterList const& _node)
{
	rememberScope(_node);

	BOOST_THROW_EXCEPTION(InternalCompilerError() << errinfo_comment("Unhandled node: ParameterList") << errinfo_sourceLocation(_node.location()));
	return false;
}

bool ASTBoogieConverter::visit(FunctionDefinition const& _node)
{
	rememberScope(_node);

	// Solidity functions are mapped to Boogie procedures
	m_currentFunc = &_node;

	addGlobalComment("");
	string funcType = _node.visibility() == Declaration::Visibility::External ? "" : " : " + _node.type()->toString();
	addGlobalComment("Function: " + _node.name() + funcType);

	// Input parameters
	list<smack::Binding> params {
		// Globally available stuff
		{ASTBoogieUtils::BOOGIE_THIS, ASTBoogieUtils::BOOGIE_ADDRESS_TYPE}, // this
		{ASTBoogieUtils::BOOGIE_MSG_SENDER, ASTBoogieUtils::BOOGIE_ADDRESS_TYPE}, // msg.sender
		{ASTBoogieUtils::BOOGIE_MSG_VALUE, m_context.isBvEncoding() ? "bv256" : "int"} // msg.value
	};
	// Add original parameters of the function
	for (auto par : _node.parameters())
	{
		params.push_back({ASTBoogieUtils::mapDeclName(*par), ASTBoogieUtils::mapType(par->type(), *par, m_context)});
		if (par->type()->category() == Type::Category::Array) // Array length
		{
			params.push_back({ASTBoogieUtils::mapDeclName(*par) + ASTBoogieUtils::BOOGIE_LENGTH, m_context.isBvEncoding() ? "bv256" : "int"});
		}
	}

	// Return values
	list<smack::Binding> rets;
	for (auto ret : _node.returnParameters())
	{
		if (ret->type()->category() == Type::Category::Array)
		{
			m_context.reportError(&_node, "Arrays are not supported as return values");
			return false;
		}
		rets.push_back({ASTBoogieUtils::mapDeclName(*ret), ASTBoogieUtils::mapType(ret->type(), *ret, m_context)});
	}

	// Boogie treats return as an assignment to the return variable(s)
	if (_node.returnParameters().empty())
	{
		m_currentRet = nullptr;
	}
	else if (_node.returnParameters().size() == 1)
	{
		m_currentRet = smack::Expr::id(rets.begin()->first);
	}
	else
	{
		m_context.reportError(&_node, "Multiple return values are not supported");
		return false;
	}

	// Convert function body, collect result
	m_localDecls.clear();
	// Create new empty block
	m_currentBlocks.push(smack::Block::block());
	// State variable initializers should go to the beginning of the constructor
	if (_node.isConstructor())
	{
		for (auto i : m_stateVarInitializers) m_currentBlocks.top()->addStmt(i);
		m_stateVarInitializers.clear(); // Clear list so that we know that initializers have been included
	}
	// Payable functions should handle msg.value
	if (_node.isPayable())
	{
		// balance[this] += msg.value
		m_currentBlocks.top()->addStmt(smack::Stmt::assign(
					smack::Expr::id(ASTBoogieUtils::BOOGIE_BALANCE),
					smack::Expr::upd(
							smack::Expr::id(ASTBoogieUtils::BOOGIE_BALANCE),
							smack::Expr::id(ASTBoogieUtils::BOOGIE_THIS),
							ASTBoogieUtils::encodeArithBinaryOp(m_context, nullptr, Token::Value::Add,
									smack::Expr::sel(ASTBoogieUtils::BOOGIE_BALANCE, ASTBoogieUtils::BOOGIE_THIS),
									smack::Expr::id(ASTBoogieUtils::BOOGIE_MSG_VALUE), 256, false))));
	}

	// Modifiers need to be inlined
	if (_node.modifiers().empty())
	{
		if (_node.isImplemented())
		{
			string retLabel = "$return" + to_string(m_nextReturnLabelId);
			++m_nextReturnLabelId;
			m_currentReturnLabel = retLabel;
			_node.body().accept(*this);
			m_currentBlocks.top()->addStmt(smack::Stmt::label(retLabel));
		}
	}
	else
	{
		m_currentModifier = 0;
		auto modifier = _node.modifiers()[m_currentModifier];
		auto modifierDecl = dynamic_cast<ModifierDefinition const*>(modifier->name()->annotation().referencedDeclaration);

		if (modifierDecl)
		{
			string retLabel = "$return" + to_string(m_nextReturnLabelId);
			++m_nextReturnLabelId;
			m_currentReturnLabel = retLabel;
			m_currentBlocks.top()->addStmt(smack::Stmt::comment("Inlined modifier " + modifierDecl->name() + " starts here"));

			// Introduce and assign local variables for modifier arguments
			if (modifier->arguments())
			{
				for (unsigned long i = 0; i < modifier->arguments()->size(); ++i)
				{
					smack::Decl* modifierParam = smack::Decl::variable(ASTBoogieUtils::mapDeclName(*(modifierDecl->parameters()[i])),
							ASTBoogieUtils::mapType(modifierDecl->parameters()[i]->annotation().type, *(modifierDecl->parameters()[i]), m_context));
					m_localDecls.push_back(modifierParam);
					smack::Expr const* modifierArg = convertExpression(*modifier->arguments()->at(i));
					m_currentBlocks.top()->addStmt(smack::Stmt::assign(smack::Expr::id(modifierParam->getName()), modifierArg));
				}
			}

			modifierDecl->body().accept(*this);
			m_currentBlocks.top()->addStmt(smack::Stmt::label(retLabel));
			m_currentBlocks.top()->addStmt(smack::Stmt::comment("Inlined modifier " + modifierDecl->name() + " ends here"));
		}
		else
		{
			m_context.reportError(&*modifier, "Unsupported modifier invocation");
		}
	}

	list<smack::Block*> blocks;
	if (!m_currentBlocks.top()->getStatements().empty()) { blocks.push_back(m_currentBlocks.top()); }
	m_currentBlocks.pop();
	if (!m_currentBlocks.empty())
	{
		BOOST_THROW_EXCEPTION(InternalCompilerError() <<
				errinfo_comment("Non-empty stack of blocks at the end of function."));
	}

	// Get the name of the function
	string funcName = _node.isConstructor() ?
			ASTBoogieUtils::BOOGIE_CONSTRUCTOR + "#" + toString(_node.id()) : // TODO: we should use the ID of the contract (the default constructor can only use that)
			ASTBoogieUtils::mapDeclName(_node);

	// Create the procedure
	auto procDecl = smack::Decl::procedure(funcName, params, rets, m_localDecls, blocks);

	if (_node.isPublic()) // Public functions: add invariants as pre/postconditions
	{
		for (auto invar : m_context.currentContractInvars())
		{
			if (!_node.isConstructor())
			{
				procDecl->getRequires().push_back(smack::Specification::spec(invar.first,
					ASTBoogieUtils::createAttrs(_node.location(), "Invariant '" + invar.second + "' might not hold when entering function.", *m_context.currentScanner())));
			}
			procDecl->getEnsures().push_back(smack::Specification::spec(invar.first,
					ASTBoogieUtils::createAttrs(_node.location(), "Invariant '" + invar.second + "' might not hold at end of function.", *m_context.currentScanner())));
		}
	}
	else // Private functions: inline
	{
		procDecl->addAttr(smack::Attr::attr("inline", 1));

		// Add contract invariants only if explicitly requested
		for (auto cInv : getExprsFromDocTags(_node, _node.annotation(), &_node, DOCTAG_CONTRACT_INVARS_INCLUDE))
		{
			procDecl->getRequires().push_back(smack::Specification::spec(cInv.first,
					ASTBoogieUtils::createAttrs(_node.location(), "Invariant '" + cInv.second + "' might not hold when entering function.", *m_context.currentScanner())));
			procDecl->getEnsures().push_back(smack::Specification::spec(cInv.first,
					ASTBoogieUtils::createAttrs(_node.location(), "Invariant '" + cInv.second + "' might not hold at end of function.", *m_context.currentScanner())));
		}
	}

	for (auto pre : getExprsFromDocTags(_node, _node.annotation(), &_node, DOCTAG_PRECOND))
	{
		procDecl->getRequires().push_back(smack::Specification::spec(pre.first,
							ASTBoogieUtils::createAttrs(_node.location(), "Precondition '" + pre.second + "' might not hold when entering function.", *m_context.currentScanner())));
	}
	for (auto post : getExprsFromDocTags(_node, _node.annotation(), &_node, DOCTAG_POSTCOND))
	{
		procDecl->getEnsures().push_back(smack::Specification::spec(post.first,
							ASTBoogieUtils::createAttrs(_node.location(), "Postcondition '" + post.second + "' might not hold at end of function.", *m_context.currentScanner())));
	}
	// TODO: check that no new sum variables were introduced

	procDecl->addAttrs(ASTBoogieUtils::createAttrs(_node.location(), _node.name(), *m_context.currentScanner()));
	m_context.program().getDeclarations().push_back(procDecl);
	return false;
}

bool ASTBoogieConverter::visit(VariableDeclaration const& _node)
{
	rememberScope(_node);

	// Non-state variables should be handled in the VariableDeclarationStatement
	if (!_node.isStateVariable())
	{
		BOOST_THROW_EXCEPTION(InternalCompilerError() <<
				errinfo_comment("Non-state variable appearing in VariableDeclaration") <<
				errinfo_sourceLocation(_node.location()));
	}
	if (_node.value())
	{
		// Initialization is saved for the constructor
		// A new block is introduced to collect side effects of the initializer expression
		m_currentBlocks.push(smack::Block::block());
		smack::Expr const* initExpr = convertExpression(*_node.value());
		if (m_context.isBvEncoding() && ASTBoogieUtils::isBitPreciseType(_node.annotation().type))
		{
			initExpr = ASTBoogieUtils::checkImplicitBvConversion(initExpr, _node.value()->annotation().type, _node.annotation().type, m_context);
		}
		for (auto stmt : *m_currentBlocks.top()) { m_stateVarInitializers.push_back(stmt); }
		m_currentBlocks.pop();
		m_stateVarInitializers.push_back(smack::Stmt::assign(smack::Expr::id(ASTBoogieUtils::mapDeclName(_node)),
				smack::Expr::upd(
						smack::Expr::id(ASTBoogieUtils::mapDeclName(_node)),
						smack::Expr::id(ASTBoogieUtils::BOOGIE_THIS),
						initExpr)));
	}

	addGlobalComment("");
	addGlobalComment("State variable: " + _node.name() + " : " + _node.type()->toString());
	// State variables are represented as maps from address to their type
	auto varDecl = smack::Decl::variable(ASTBoogieUtils::mapDeclName(_node),
			"[" + ASTBoogieUtils::BOOGIE_ADDRESS_TYPE + "]" + ASTBoogieUtils::mapType(_node.type(), _node, m_context));
	varDecl->addAttrs(ASTBoogieUtils::createAttrs(_node.location(), _node.name(), *m_context.currentScanner()));
	m_context.program().getDeclarations().push_back(varDecl);

	// Arrays require an extra variable for their length
	if (_node.type()->category() == Type::Category::Array)
	{
		m_context.program().getDeclarations().push_back(
				smack::Decl::variable(ASTBoogieUtils::mapDeclName(_node) + ASTBoogieUtils::BOOGIE_LENGTH,
				"[" + ASTBoogieUtils::BOOGIE_ADDRESS_TYPE + "]" + (m_context.isBvEncoding() ? "bv256" : "int")));
	}
	return false;
}

bool ASTBoogieConverter::visit(ModifierDefinition const& _node)
{
	rememberScope(_node);

	// Modifier definitions do not appear explicitly, but are instead inlined to functions
	return false;
}

bool ASTBoogieConverter::visit(ModifierInvocation const& _node)
{
	rememberScope(_node);

	BOOST_THROW_EXCEPTION(InternalCompilerError() << errinfo_comment("Unhandled node: ModifierInvocation") << errinfo_sourceLocation(_node.location()));
	return false;
}

bool ASTBoogieConverter::visit(EventDefinition const& _node)
{
	rememberScope(_node);

	m_context.reportWarning(&_node, "Ignored event definition");
	return false;
}

bool ASTBoogieConverter::visit(ElementaryTypeName const& _node)
{
	rememberScope(_node);

	BOOST_THROW_EXCEPTION(InternalCompilerError() << errinfo_comment("Unhandled node: ElementaryTypeName") << errinfo_sourceLocation(_node.location()));
	return false;
}

bool ASTBoogieConverter::visit(UserDefinedTypeName const& _node)
{
	rememberScope(_node);

	BOOST_THROW_EXCEPTION(InternalCompilerError() << errinfo_comment("Unhandled node: UserDefinedTypeName") << errinfo_sourceLocation(_node.location()));
	return false;
}

bool ASTBoogieConverter::visit(FunctionTypeName const& _node)
{
	rememberScope(_node);

	BOOST_THROW_EXCEPTION(InternalCompilerError() << errinfo_comment("Unhandled node: FunctionTypeName") << errinfo_sourceLocation(_node.location()));
	return false;
}

bool ASTBoogieConverter::visit(Mapping const& _node)
{
	rememberScope(_node);

	BOOST_THROW_EXCEPTION(InternalCompilerError() << errinfo_comment("Unhandled node: Mapping") << errinfo_sourceLocation(_node.location()));
	return false;
}

bool ASTBoogieConverter::visit(ArrayTypeName const& _node)
{
	rememberScope(_node);

	BOOST_THROW_EXCEPTION(InternalCompilerError() << errinfo_comment("Unhandled node: ArrayTypeName") << errinfo_sourceLocation(_node.location()));
	return false;
}

// ---------------------------------------------------------------------------
//                     Visitor methods for statements
// ---------------------------------------------------------------------------

bool ASTBoogieConverter::visit(InlineAssembly const& _node)
{
	rememberScope(_node);

	m_context.reportError(&_node, "Inline assembly is not supported");
	return false;
}

bool ASTBoogieConverter::visit(Block const& _node)
{
	rememberScope(_node);

	// Simply apply visitor recursively, compound statements will create new blocks when required
	return true;
}

bool ASTBoogieConverter::visit(PlaceholderStatement const& _node)
{
	rememberScope(_node);

	// We go one level deeper in the modifier list
	m_currentModifier++;

	if (m_currentModifier < m_currentFunc->modifiers().size()) // We still have modifiers
	{
		auto modifier = m_currentFunc->modifiers()[m_currentModifier];
		auto modifierDecl = dynamic_cast<ModifierDefinition const*>(modifier->name()->annotation().referencedDeclaration);

		if (modifierDecl)
		{
			// Duplicated modifiers currently do not work, because they will introduce
			// local variables for their parameters with the same name
			for (unsigned long i = 0; i < m_currentModifier; ++i)
			{
				if (m_currentFunc->modifiers()[i]->name()->annotation().referencedDeclaration->id() == modifierDecl->id()) {
					m_context.reportError(m_currentFunc, "Duplicated modifiers are not supported");
				}
			}

			string oldReturnLabel = m_currentReturnLabel;
			m_currentReturnLabel = "$return" + to_string(m_nextReturnLabelId);
			++m_nextReturnLabelId;
			m_currentBlocks.top()->addStmt(smack::Stmt::comment("Inlined modifier " + modifierDecl->name() + " starts here"));

			// Introduce and assign local variables for modifier arguments
			if (modifier->arguments())
			{
				for (unsigned long i = 0; i < modifier->arguments()->size(); ++i)
				{
					smack::Decl* modifierParam = smack::Decl::variable(ASTBoogieUtils::mapDeclName(*(modifierDecl->parameters()[i])),
							ASTBoogieUtils::mapType(modifierDecl->parameters()[i]->annotation().type, *(modifierDecl->parameters()[i]), m_context));
					m_localDecls.push_back(modifierParam);
					smack::Expr const* modifierArg = convertExpression(*modifier->arguments()->at(i));
					m_currentBlocks.top()->addStmt(smack::Stmt::assign(smack::Expr::id(modifierParam->getName()), modifierArg));
				}
			}
			modifierDecl->body().accept(*this);
			m_currentBlocks.top()->addStmt(smack::Stmt::label(m_currentReturnLabel));
			m_currentBlocks.top()->addStmt(smack::Stmt::comment("Inlined modifier " + modifierDecl->name() + " ends here"));
			m_currentReturnLabel = oldReturnLabel;
		}
		else
		{
			m_context.reportError(&*modifier, "Unsupported modifier invocation");
		}
	}
	else if (m_currentFunc->isImplemented()) // We reached the function
	{
		string oldReturnLabel = m_currentReturnLabel;
		m_currentReturnLabel = "$return" + to_string(m_nextReturnLabelId);
		++m_nextReturnLabelId;
		m_currentBlocks.top()->addStmt(smack::Stmt::comment("Function body starts here"));
		m_currentFunc->body().accept(*this);
		m_currentBlocks.top()->addStmt(smack::Stmt::label(m_currentReturnLabel));
		m_currentBlocks.top()->addStmt(smack::Stmt::comment("Function body ends here"));
		m_currentReturnLabel = oldReturnLabel;
	}

	m_currentModifier--; // Go back one level in the modifier list

	return false;
}

bool ASTBoogieConverter::visit(IfStatement const& _node)
{
	rememberScope(_node);

	// Get condition recursively
	const smack::Expr* cond = convertExpression(_node.condition());

	// Get true branch recursively
	m_currentBlocks.push(smack::Block::block());
	_node.trueStatement().accept(*this);
	const smack::Block* thenBlock = m_currentBlocks.top();
	m_currentBlocks.pop();

	// Get false branch recursively (might not exist)
	const smack::Block* elseBlock = nullptr;
	if (_node.falseStatement())
	{
		m_currentBlocks.push(smack::Block::block());
		_node.falseStatement()->accept(*this);
		elseBlock = m_currentBlocks.top();
		m_currentBlocks.pop();
	}

	m_currentBlocks.top()->addStmt(smack::Stmt::ifelse(cond, thenBlock, elseBlock));
	return false;
}

bool ASTBoogieConverter::visit(WhileStatement const& _node)
{
	rememberScope(_node);

	// Get condition recursively
	const smack::Expr* cond = convertExpression(_node.condition());

	// Get if branch recursively
	m_currentBlocks.push(smack::Block::block());
	_node.body().accept(*this);
	const smack::Block* body = m_currentBlocks.top();
	m_currentBlocks.pop();

	std::list<smack::Specification const*> invars;
	for (auto invar : getExprsFromDocTags(_node, _node.annotation(), scope(), DOCTAG_LOOP_INVAR))
	{
		invars.push_back(smack::Specification::spec(invar.first, ASTBoogieUtils::createAttrs(_node.location(), invar.second, *m_context.currentScanner())));
	}
	for (auto invar : getExprsFromDocTags(_node, _node.annotation(), scope(), DOCTAG_CONTRACT_INVARS_INCLUDE))
	{
		invars.push_back(smack::Specification::spec(invar.first, ASTBoogieUtils::createAttrs(_node.location(), invar.second, *m_context.currentScanner())));
	}
	// TODO: check that invariants did not introduce new sum variables

	m_currentBlocks.top()->addStmt(smack::Stmt::while_(cond, body, invars));

	return false;
}

bool ASTBoogieConverter::visit(ForStatement const& _node)
{
	rememberScope(_node);

	// Boogie does not have a for statement, therefore it is transformed
	// into a while statement in the following way:
	//
	// for (initExpr; cond; loopExpr) { body }
	//
	// initExpr; while (cond) { body; loopExpr }

	// Get initialization recursively (adds statement to current block)
	m_currentBlocks.top()->addStmt(smack::Stmt::comment("The following while loop was mapped from a for loop"));
	if (_node.initializationExpression()) { _node.initializationExpression()->accept(*this); }

	// Get condition recursively
	const smack::Expr* cond = _node.condition() ? convertExpression(*_node.condition()) : nullptr;

	// Get body recursively
	m_currentBlocks.push(smack::Block::block());
	_node.body().accept(*this);
	// Include loop expression at the end of body
	if (_node.loopExpression())
	{
		_node.loopExpression()->accept(*this); // Adds statements to current block
	}
	const smack::Block* body = m_currentBlocks.top();
	m_currentBlocks.pop();

	std::list<smack::Specification const*> invars;
	for (auto invar : getExprsFromDocTags(_node, _node.annotation(), &_node, DOCTAG_LOOP_INVAR))
	{
		invars.push_back(smack::Specification::spec(invar.first, ASTBoogieUtils::createAttrs(_node.location(), invar.second, *m_context.currentScanner())));
	}
	for (auto invar : getExprsFromDocTags(_node, _node.annotation(), &_node, DOCTAG_CONTRACT_INVARS_INCLUDE))
	{
		invars.push_back(smack::Specification::spec(invar.first, ASTBoogieUtils::createAttrs(_node.location(), invar.second, *m_context.currentScanner())));
	}
	// TODO: check that invariants did not introduce new sum variables

	m_currentBlocks.top()->addStmt(smack::Stmt::while_(cond, body, invars));

	return false;
}

bool ASTBoogieConverter::visit(Continue const& _node)
{
	rememberScope(_node);

	// TODO: Boogie does not support continue, this must be mapped manually
	// using labels and gotos
	m_context.reportError(&_node, "Continue statement is not supported");
	return false;
}

bool ASTBoogieConverter::visit(Break const& _node)
{
	rememberScope(_node);

	m_currentBlocks.top()->addStmt(smack::Stmt::break_());
	return false;
}

bool ASTBoogieConverter::visit(Return const& _node)
{
	rememberScope(_node);

	if (_node.expression() != nullptr)
	{
		// Get rhs recursively
		const smack::Expr* rhs = convertExpression(*_node.expression());

		if (m_context.isBvEncoding())
		{
			// We already throw an error elsewhere if there are multiple return values
			auto returnType = m_currentFunc->returnParameters()[0]->annotation().type;
			rhs = ASTBoogieUtils::checkImplicitBvConversion(rhs, _node.expression()->annotation().type,
					returnType, m_context);
		}

		// LHS of assignment should already be known (set by the enclosing FunctionDefinition)
		const smack::Expr* lhs = m_currentRet;

		// First create an assignment, and then an empty return
		m_currentBlocks.top()->addStmt(smack::Stmt::assign(lhs, rhs));
	}
	m_currentBlocks.top()->addStmt(smack::Stmt::goto_({m_currentReturnLabel}));
	return false;
}

bool ASTBoogieConverter::visit(Throw const& _node)
{
	rememberScope(_node);

	m_currentBlocks.top()->addStmt(smack::Stmt::assume(smack::Expr::lit(false)));
	return false;
}

bool ASTBoogieConverter::visit(EmitStatement const& _node)
{
	rememberScope(_node);

	m_context.reportWarning(&_node, "Ignored emit statement");
	return false;
}

bool ASTBoogieConverter::visit(VariableDeclarationStatement const& _node)
{
	rememberScope(_node);

	for (auto decl : _node.declarations())
	{
		// Decl can be null, e.g., var (x,,) = (1,2,3)
		// In this case we just ignore it
		if (decl != nullptr)
		{
			if (decl->isLocalVariable())
			{
				// Boogie requires local variables to be declared at the beginning of the procedure
				auto varDecl = smack::Decl::variable(
						ASTBoogieUtils::mapDeclName(*decl),
						ASTBoogieUtils::mapType(decl->type(), *decl, m_context));
				varDecl->addAttrs(ASTBoogieUtils::createAttrs(decl->location(), decl->name(), *m_context.currentScanner()));
				m_localDecls.push_back(varDecl);
			}
			else
			{
				// Non-local variables should be handled elsewhere
				BOOST_THROW_EXCEPTION(InternalCompilerError() <<
						errinfo_comment("Non-local variable appearing in VariableDeclarationStatement") <<
						errinfo_sourceLocation(_node.location()));
			}
		}
	}

	// Convert initial value into an assignment statement
	if (_node.initialValue())
	{
		if (_node.declarations().size() == 1)
		{
			auto decl = *_node.declarations().begin();
			// Get expression recursively
			const smack::Expr* rhs = convertExpression(*_node.initialValue());

			if (m_context.isBvEncoding() && ASTBoogieUtils::isBitPreciseType(decl->annotation().type))
			{
				rhs = ASTBoogieUtils::checkImplicitBvConversion(rhs, _node.initialValue()->annotation().type, decl->annotation().type, m_context);
			}

			m_currentBlocks.top()->addStmt(smack::Stmt::assign(
					smack::Expr::id(ASTBoogieUtils::mapDeclName(**_node.declarations().begin())),
					rhs));
		}
		else
		{
			m_context.reportError(&_node, "Assignment to multiple variables is not supported");
		}
	}
	return false;
}

bool ASTBoogieConverter::visit(ExpressionStatement const& _node)
{
	rememberScope(_node);

	// Some expressions have specific statements in Boogie

	// Assignment
	if (dynamic_cast<Assignment const*>(&_node.expression()))
	{
		convertExpression(_node.expression());
		return false;
	}

	// Function call
	if (dynamic_cast<FunctionCall const*>(&_node.expression()))
	{
		convertExpression(_node.expression());
		return false;
	}

	// Increment, decrement
	if (auto unaryExpr = dynamic_cast<UnaryOperation const*>(&_node.expression()))
	{
		if (unaryExpr->getOperator() == Token::Inc || unaryExpr->getOperator() == Token::Dec)
		{
			convertExpression(_node.expression());
			return false;
		}
	}

	// Other statements are assigned to a temp variable because Boogie
	// does not allow stand alone expressions

	smack::Expr const* expr = convertExpression(_node.expression());
	smack::Decl* tmpVar = smack::Decl::variable("tmpVar" + to_string(_node.id()),
			ASTBoogieUtils::mapType(_node.expression().annotation().type, _node, m_context));
	m_localDecls.push_back(tmpVar);
	m_currentBlocks.top()->addStmt(smack::Stmt::comment("Assignment to temp variable introduced because Boogie does not support stand alone expressions"));
	m_currentBlocks.top()->addStmt(smack::Stmt::assign(smack::Expr::id(tmpVar->getName()), expr));
	return false;
}


bool ASTBoogieConverter::visitNode(ASTNode const& _node)
{
	rememberScope(_node);

	BOOST_THROW_EXCEPTION(InternalCompilerError() << errinfo_comment("Unhandled node (unknown)") << errinfo_sourceLocation(_node.location()));
	return true;
}

}
}
