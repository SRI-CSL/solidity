#pragma once

#include <libsolidity/ast/ASTVisitor.h>
#include <libsolidity/ast/BoogieAst.h>
#include <libsolidity/ast/BoogieContext.h>

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
	BoogieContext& m_context;

	// Helper variables to pass information between the visit methods
	ContractDefinition const* m_currentContract;
	FunctionDefinition const* m_currentFunc; // Function currently being processed
	unsigned long m_currentModifier; // Index of the current modifier being processed

	// Collect local variable declarations (Boogie requires them at the beginning of the function).
	std::list<smack::Decl*> m_localDecls;

	// Collect initializers for state variables to be added to the beginning of the constructor
	// If there is no constructor, but there are initializers, we create one
	std::list<smack::Stmt const*> m_stateVarInitializers;

	// Current block(s) where statements are appended, stack is needed due to nested blocks
	std::stack<smack::Block*> m_currentBlocks;

	// Return statement in Solidity is mapped to an assignment to the return
	// variables in Boogie, which is described by currentRet
	const smack::Expr* m_currentRet;
	// Current label to jump to when encountering a return. This is needed because modifiers
	// are inlined and their returns should not jump out of the whole function.
	std::string m_currentReturnLabel;
	int m_nextReturnLabelId;

	/**
	 * Add a top-level comment
	 */
	void addGlobalComment(std::string str);

	/**
	 * Helper method to convert an expression using the dedicated expression converter class,
	 * it also handles side-effect statements and declarations introduced by the conversion
	 */
	const smack::Expr* convertExpression(Expression const& _node);

	/**
	 * Create default constructor for a contract (it is required when there is no constructor,
	 * but state variables are initialized when declared)
	 */
	void createDefaultConstructor(ContractDefinition const& _node);

	std::map<smack::Expr const*, std::string> getExprsFromDocTags(ASTNode const& _node, DocumentedAnnotation const& _annot, ASTNode const* _scope, std::string _tag);

public:
	/**
	 * Create a new instance with a given context
	 */
	ASTBoogieConverter(BoogieContext& context);

	/**
	 * Convert a node and add it to the actual Boogie program
	 */
	void convert(ASTNode const& _node) { _node.accept(*this); }

	/**
	 * Print the actual Boogie program to an output stream
	 */
	void print(std::ostream& _stream) { m_context.program().print(_stream); }

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
