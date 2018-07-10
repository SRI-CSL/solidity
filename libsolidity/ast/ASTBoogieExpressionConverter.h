#pragma once

#include <libsolidity/ast/ASTVisitor.h>
#include <libsolidity/ast/BoogieAst.h>

namespace dev
{
namespace solidity
{

/**
 * Converter from Solidity expressions to Boogie expressions.
 */
class ASTBoogieExpressionConverter : private ASTConstVisitor
{
private:
	// Helper variables to pass information between the visit methods
	const smack::Expr* currentExpr;
	const smack::Expr* currentAddress;
	const smack::Expr* currentValue;
	bool isGetter;
	bool isLibraryCall;
	bool isLibraryCallStatic;

	// Converting expressions might result in new statements and declarations
	// due to differences between Solidity and Boogie
	std::vector<smack::Stmt const*> newStatements;
	std::list<smack::Decl*> newDecls;
	std::list<smack::Decl*> newConstants;

	const smack::Expr* getArrayLength(const smack::Expr* expr);
	void createAssignment(Expression const& originalLhs, smack::Expr const *lhs, smack::Expr const* rhs);
	smack::Expr const* selectToUpdate(smack::SelExpr const* sel, smack::Expr const* value);

public:

	/**
	 * Result of converting a Solidity expression to a Boogie expression.
	 * Due to differences between Solidity and Boogie, the result might
	 * introduce new statements and declarations as well.
	 */
	struct Result
	{
		const smack::Expr* expr;
		std::vector<smack::Stmt const*> newStatements;
		std::list<smack::Decl*> newDecls;
		std::list<smack::Decl*> newConstants;

		Result(const smack::Expr* expr, std::vector<smack::Stmt const*> newStatements,
				std::list<smack::Decl*> newDecls, std::list<smack::Decl*> newConstants)
			:expr(expr), newStatements(newStatements), newDecls(newDecls), newConstants(newConstants) {}
	};


	ASTBoogieExpressionConverter();

	/**
	 * Convert a Solidity Expression into a Boogie expression. As a side
	 * effect, the conversion might introduce new statements and declarations
	 * (included in the result).
	 */
	Result convert(Expression const& _node);

	bool visit(Conditional const& _node) override;
	bool visit(Assignment const& _node) override;
	bool visit(TupleExpression const& _node) override;
	bool visit(UnaryOperation const& _node) override;
	bool visit(BinaryOperation const& _node) override;
	bool visit(FunctionCall const& _node) override;
	bool visit(NewExpression const& _node) override;
	bool visit(MemberAccess const& _node) override;
	bool visit(IndexAccess const& _node) override;
	bool visit(Identifier const& _node) override;
	bool visit(ElementaryTypeNameExpression const& _node) override;
	bool visit(Literal const& _node) override;

	bool visitNode(ASTNode const&) override;

};

}
}
