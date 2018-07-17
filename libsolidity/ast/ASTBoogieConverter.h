#pragma once

#include <libsolidity/ast/ASTVisitor.h>
#include <libsolidity/ast/BoogieAst.h>
#include <libsolidity/interface/ErrorReporter.h>
#include <map>
#include <libsolidity/analysis/DeclarationContainer.h>
#include <libsolidity/analysis/GlobalContext.h>

namespace dev
{
namespace solidity
{

/**
 * Converter from Solidity AST to Boogie AST.
 */
class ASTBoogieConverter : private ASTConstVisitor
{
private:
	ErrorReporter& m_errorReporter;
	std::shared_ptr<GlobalContext> m_globalContext;
	std::map<ASTNode const*, std::shared_ptr<DeclarationContainer>> m_scopes;

	// Top-level element is a single Boogie program
	smack::Program m_program;

	// Helper variables to pass information between the visit methods

	std::vector<smack::Expr const*> m_currentInvars;

	// Function currently being processed
	FunctionDefinition const* m_currentFunc;
	unsigned long m_currentModifier;

	// Collect local variable declarations (Boogie requires them at the
	// beginning of the function).
	std::list<smack::Decl*> m_localDecls;

	// Collect initializer for state variables to be added to the beginning
	// of the constructor
	std::list<smack::Stmt const*> m_stateVarInitializers;

	// Current block(s) where statements are appended, stack is needed
	// due to nested blocks
	std::stack<smack::Block*> m_currentBlocks;

	// Return statement in Solidity is mapped to an assignment to the return
	// variables in Boogie, which is described by currentRet
	const smack::Expr* m_currentRet;
	std::string m_currentReturnLabel;
	int nextReturnLabelId;

	/**
	 * Add a top-level comment
	 */
	void addGlobalComment(std::string str);

	/**
	 * Helper method to convert an expression using the dedicated expression
	 * converter class. It also handles the statements and declarations
	 * returned by the conversion.
	 */
	const smack::Expr* convertExpression(Expression const& _node);

	/**
	 * Create default constructor for a contract
	 */
	void createDefaultConstructor(ContractDefinition const& _node);

public:
	ASTBoogieConverter(ErrorReporter& errorReporter, std::shared_ptr<GlobalContext> globalContext,
			std::map<ASTNode const*, std::shared_ptr<DeclarationContainer>> scopes);

	/**
	 * Convert a node and add it to the actual Boogie program
	 */
	void convert(ASTNode const& _node);

	/**
	 * Print the actual Boogie program to an output stream
	 */
	void print(std::ostream& _stream);

	bool visit(SourceUnit const& _node) override;
	bool visit(PragmaDirective const& _node) override;
	bool visit(ImportDirective const& _node) override;
	bool visit(ContractDefinition const& _node) override;
	bool visit(InheritanceSpecifier const& _node) override;
	bool visit(UsingForDirective const& _node) override;
	bool visit(StructDefinition const& _node) override;
	bool visit(EnumDefinition const& _node) override;
	bool visit(EnumValue const& _node) override;
	bool visit(ParameterList const& _node) override;
	bool visit(FunctionDefinition const& _node) override;
	bool visit(VariableDeclaration const& _node) override;
	bool visit(ModifierDefinition const& _node) override;
	bool visit(ModifierInvocation const& _node) override;
	bool visit(EventDefinition const& _node) override;
	bool visit(ElementaryTypeName const& _node) override;
	bool visit(UserDefinedTypeName const& _node) override;
	bool visit(FunctionTypeName const& _node) override;
	bool visit(Mapping const& _node) override;
	bool visit(ArrayTypeName const& _node) override;
	bool visit(InlineAssembly const& _node) override;
	bool visit(Block const& _node) override;
	bool visit(PlaceholderStatement const& _node) override;
	bool visit(IfStatement const& _node) override;
	bool visit(WhileStatement const& _node) override;
	bool visit(ForStatement const& _node) override;
	bool visit(Continue const& _node) override;
	bool visit(Break const& _node) override;
	bool visit(Return const& _node) override;
	bool visit(Throw const& _node) override;
	bool visit(EmitStatement const& _node) override;
	bool visit(VariableDeclarationStatement const& _node) override;
	bool visit(ExpressionStatement const& _node) override;

	// Conversion of expressions is implemented by a separate class

	bool visitNode(ASTNode const&) override;

};

}
}
