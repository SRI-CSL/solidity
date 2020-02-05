//
// This file is distributed under the MIT License. See SMACK-LICENSE for details.
//
#pragma once

#include <sstream>
#include <string>
#include <memory>
#include <vector>

#include "libdevcore/Common.h"

namespace boogie
{

using bigint = dev::bigint;

class TypeDecl;
using TypeDeclRef = std::shared_ptr<TypeDecl>;

struct Binding;

class ProcDecl;
using ProcDeclRef = std::shared_ptr<ProcDecl>;
class FuncDecl;
using FuncDeclRef = std::shared_ptr<FuncDecl>;
class DataTypeDecl;
using DataTypeDeclRef = std::shared_ptr<DataTypeDecl>;

class Expr
{
public:

	/** Reference to expressions */
	using Ref = std::shared_ptr<Expr const>;

	virtual ~Expr() {}
	virtual void print(std::ostream& os) const = 0;
	virtual void printSMT2(std::ostream& os) const;

	std::string toString() const;
	std::string toSMT2() const;

	virtual bool isError() const { return false; }

	/** Special expression to denote errors */
	static Ref error();

	static Ref exists(std::vector<Binding> const&, Ref e);
	static Ref forall(std::vector<Binding> const&, Ref e);
	static Ref and_(Ref l, Ref r);
	static Ref or_(Ref l, Ref r);
	static Ref cond(Ref c, Ref t, Ref e);
	static Ref eq(Ref l, Ref r);
	static Ref lt(Ref l, Ref r);
	static Ref gt(Ref l, Ref r);
	static Ref lte(Ref l, Ref r);
	static Ref gte(Ref l, Ref r);
	static Ref plus(Ref l, Ref r);
	static Ref minus(Ref l, Ref r);
	static Ref div(Ref l, Ref r);
	static Ref intdiv(Ref l, Ref r);
	static Ref times(Ref l, Ref r);
	static Ref mod(Ref l, Ref r);
	static Ref exp(Ref l, Ref r);
	static Ref fn(std::string f, Ref x);
	static Ref fn(std::string f, Ref x, Ref y);
	static Ref fn(std::string f, Ref x, Ref y, Ref z);
	static Ref fn(std::string f, std::vector<Ref> const& args);
	static Ref id(std::string x);
	static Ref impl(Ref l, Ref r);
	static Ref iff(Ref l, Ref r);
	static Ref lit(bool b);
	static Ref lit(std::string v);
	static Ref lit(unsigned v) { return lit((unsigned long) v); }
	static Ref lit(unsigned long v);
	static Ref lit(long v);
	static Ref lit(bigint v);
	static Ref lit(std::string v, unsigned w);
	static Ref lit(bigint v, unsigned w);
	static Ref lit(bool n, std::string s, std::string e, unsigned ss, unsigned es);
	static Ref neq(Ref l, Ref r);
	static Ref not_(Ref e);
	static Ref neg(Ref e);
	static Ref arrconst(TypeDeclRef arrType, Ref val);
	static Ref arrsel(Ref b, Ref i);
	static Ref arrupd(Ref b, Ref i, Ref v);
	static Ref dtsel(Ref b, std::string mem, FuncDeclRef constr, DataTypeDeclRef dt);
	static Ref dtupd(Ref b, std::string mem, Ref v, FuncDeclRef constr, DataTypeDeclRef dt);
	static Ref old(Ref expr);
	static Ref tuple(std::vector<Ref> const& e);

	static Ref selectToUpdate(Ref sel, Ref value);
};

struct Binding
{
	Expr::Ref id;
	TypeDeclRef type;
};

class ErrorExpr : public Expr
{
public:
	bool isError() const override { return true; }
	void print(std::ostream& os) const override;
};

class BinExpr : public Expr
{
public:
	enum Binary
	{
		Iff, Imp, Or, And, Eq, Neq, Lt, Gt, Lte, Gte, Sub,
		Conc, Plus, Minus, Times, Div, IntDiv, Mod, Exp
	};
private:
	Binary const op;
	Expr::Ref lhs;
	Expr::Ref rhs;
public:
	BinExpr(Binary const op, Expr::Ref l, Expr::Ref r) : op(op), lhs(l), rhs(r) {}
	void print(std::ostream& os) const override;
};

class CondExpr : public Expr {
	Expr::Ref cond;
	Expr::Ref then;
	Expr::Ref else_;
public:
	CondExpr(Expr::Ref c, Expr::Ref t, Expr::Ref e)
		: cond(c), then(t), else_(e) {}
	void print(std::ostream& os) const override;

	Expr::Ref getCond() const { return cond; }
	Expr::Ref getThen() const { return then; }
	Expr::Ref getElse() const { return else_; }
};

class FunExpr : public Expr {
	std::string fun;
	std::vector<Ref> args;
public:
	FunExpr(std::string f, std::vector<Ref> const& xs) : fun(f), args(xs) {}
	void print(std::ostream& os) const override;
};

class BoolLit : public Expr {
	bool val;
public:
	BoolLit(bool b) : val(b) {}
	void print(std::ostream& os) const override;
};

class IntLit : public Expr {
	bigint val;
public:
	IntLit(std::string v) : val(v) {}
	IntLit(unsigned long v) : val(v) {}
	IntLit(long v) : val(v) {}
	IntLit(bigint v) : val(v) {}
	bigint getVal() const { return val; }
	void print(std::ostream& os) const override;
};

class BvLit : public Expr {
	std::string val;
	unsigned width;
public:
	BvLit(std::string v, unsigned w) : val(v), width(w) {}
	BvLit(bigint v, unsigned w) : width(w) {
		std::stringstream s;
		s << v;
		val = s.str();
	}
	std::string getVal() const { return val; }
	void print(std::ostream& os) const override;
	void printSMT2(std::ostream& os) const override;
};

class FPLit : public Expr {
	bool neg;
	std::string sig;
	std::string expo;
	unsigned sigSize;
	unsigned expSize;
public:
	FPLit(bool n, std::string s, std::string e, unsigned ss, unsigned es) : neg(n), sig(s), expo(e), sigSize(ss), expSize(es) {}
	void print(std::ostream& os) const override;
};

class StringLit : public Expr {
	std::string val;
public:
	StringLit(std::string v) : val(v) {}
	void print(std::ostream& os) const override;
};

class NegExpr : public Expr {
	Expr::Ref expr;
public:
	NegExpr(Expr::Ref e) : expr(e) {}
	void print(std::ostream& os) const override;
};

class NotExpr : public Expr {
	Expr::Ref expr;
public:
	NotExpr(Expr::Ref e) : expr(e) {}
	void print(std::ostream& os) const override;
};

class QuantExpr : public Expr {
public:
	enum Quantifier { Exists, Forall };
private:
	Quantifier quant;
	std::vector<Binding> vars;
	Ref expr;
public:
	QuantExpr(Quantifier q, std::vector<Binding> const& vs, Ref e) : quant(q), vars(vs), expr(e) {}
	void print(std::ostream& os) const override;
};

class SelExpr : public Expr {
protected:
	Ref base;
public:
	SelExpr(Ref base) : base(base) {}
	Ref getBase() const { return base; }
	virtual Ref toUpdate(Ref v) const = 0;
	virtual Ref replaceBase(Ref b) const = 0;
};

class UpdExpr : public Expr {
protected:
	Ref base;
	Ref val;
public:
	UpdExpr(Ref base, Ref val) : base(base), val(val) {}
	Ref getBase() const { return base; }
};

class ArrConstExpr : public Expr {
	TypeDeclRef arrType;
	Ref val;
public:
	ArrConstExpr(TypeDeclRef arrType, Ref val) : arrType(arrType), val(val) {}
	void print(std::ostream& os) const override;
};

class ArrSelExpr : public SelExpr {
	Ref idx;
public:
	ArrSelExpr(Ref a, Ref i) : SelExpr(a), idx(i) {}
	Ref const& getIdx() const { return idx; }
	void print(std::ostream& os) const override;
	Ref toUpdate(Ref v) const override { return Expr::arrupd(base, idx, v); }
	Ref replaceBase(Ref b) const override { return Expr::arrsel(b, idx); }
};

class ArrUpdExpr : public UpdExpr {
	Ref idx;
public:
	ArrUpdExpr(Ref a, Ref i, Ref v)
		: UpdExpr(a, v), idx(i) {}
	void print(std::ostream& os) const override;
};

class VarExpr : public Expr {
	std::string var;
public:
	VarExpr(std::string v) : var(v) {}
	std::string name() const { return var; }
	void print(std::ostream& os) const override;
};

class OldExpr : public Expr {
	Ref expr;
public:
	OldExpr(Ref expr) : expr(expr) {}
	void print(std::ostream& os) const override;
};

class TupleExpr : public Expr {
	std::vector<Ref> es;
public:
	TupleExpr(std::vector<Ref> const& elements): es(elements) {}
	std::vector<Ref> const& elements() const { return es; }
	void print(std::ostream& os) const override;
};

class Attr {
protected:
	std::string name;
	std::vector<Expr::Ref> vals;
public:

	using Ref = std::shared_ptr<Attr const>;

	Attr(std::string n, std::vector<Expr::Ref> const& vs) : name(n), vals(vs) {}
	void print(std::ostream& os) const;
	std::string getName() const { return name; }

	static Ref attr(std::string s);
	static Ref attr(std::string s, std::string v);
	static Ref attr(std::string s, int v);
	static Ref attr(std::string s, std::string v, int i);
	static Ref attr(std::string s, std::string v, int i, int j);
	static Ref attr(std::string s, std::vector<Expr::Ref> const& vs);
};

class Block;
using BlockRef = std::shared_ptr<Block>;
using BlockConstRef = std::shared_ptr<Block const>;

class Specification;
using SpecificationRef = std::shared_ptr<Specification const>;

class Stmt {
public:
	enum Kind {
		ASSERT, ASSUME, ASSIGN, HAVOC, GOTO, CALL, RETURN, COMMENT, IFELSE, WHILE, BREAK, LABEL
	};

	using Ref = std::shared_ptr<Stmt const>;

private:
	Kind const kind;
protected:
	Stmt(Kind k) : kind(k) {}
public:
	Kind getKind() const { return kind; }

public:
	virtual ~Stmt() {}
	static Ref annot(std::vector<Attr::Ref> const& attrs);
	static Ref annot(Attr::Ref a);
	static Ref assert_(Expr::Ref e,
		std::vector<Attr::Ref> const& attrs = {});
	static Ref assign(Expr::Ref e, Expr::Ref f);
	static Ref assume(Expr::Ref e);
	static Ref assume(Expr::Ref e, Attr::Ref attr);
	static Ref call(
		std::string p,
		std::vector<Expr::Ref> const& args = {},
		std::vector<std::string> const& rets = {},
		std::vector<Attr::Ref> const& attrs = {});
	static Ref comment(std::string c);
	static Ref goto_(std::vector<std::string> const& ts);
	static Ref havoc(std::string x);
	static Ref return_();
	static Ref return_(Expr::Ref e);
	static Ref skip();
	static Ref ifelse(Expr::Ref cond, BlockConstRef then, BlockConstRef elze = nullptr);
	static Ref while_(
			Expr::Ref cond,
			BlockConstRef body,
			std::vector<SpecificationRef> const& invars = {});
	static Ref break_();
	static Ref label(std::string name);
	virtual void print(std::ostream& os) const = 0;
};

class AssertStmt : public Stmt {
	Expr::Ref expr;
	std::vector<Attr::Ref> attrs;
public:
	AssertStmt(Expr::Ref e, std::vector<Attr::Ref> const& ax)
		: Stmt(ASSERT), expr(e), attrs(ax) {}
	void print(std::ostream& os) const override;
	static bool classof(Ref S) { return S->getKind() == ASSERT; }
};

class AssignStmt : public Stmt {
	std::vector<Expr::Ref> lhs;
	std::vector<Expr::Ref> rhs;
public:
	AssignStmt(std::vector<Expr::Ref> const& lhs, std::vector<Expr::Ref> const& rhs)
		: Stmt(ASSIGN), lhs(lhs), rhs(rhs) {}
	void print(std::ostream& os) const override;
	static bool classof(Ref S) { return S->getKind() == ASSIGN; }
};

class AssumeStmt : public Stmt {
	Expr::Ref expr;
	std::vector<Attr::Ref> attrs;
public:
	AssumeStmt(Expr::Ref e) : Stmt(ASSUME), expr(e) {}
	void add(Attr::Ref a) {
		attrs.push_back(a);
	}
	bool hasAttr(std::string name) const {
		for (auto a = attrs.begin(); a != attrs.end(); ++a) {
			if ((*a)->getName() == name)
				return true;
		}
		return false;
	}
	void print(std::ostream& os) const override;
	static bool classof(Ref S) { return S->getKind() == ASSUME; }
};

class CallStmt : public Stmt {
	std::string proc;
	std::vector<Attr::Ref> attrs;
	std::vector<Expr::Ref> params;
	std::vector<std::string> returns;
public:
	CallStmt(std::string p,
		std::vector<Attr::Ref> const& attrs,
		std::vector<Expr::Ref> const& args,
		std::vector<std::string> const& rets)
		: Stmt(CALL), proc(p), attrs(attrs), params(args), returns(rets) {}

	void print(std::ostream& os) const override;
	static bool classof(Ref S) { return S->getKind() == CALL; }
};

class Comment : public Stmt {
	std::string str;
public:
	Comment(std::string s) : Stmt(COMMENT), str(s) {}
	void print(std::ostream& os) const override;
	static bool classof(Ref S) { return S->getKind() == COMMENT; }
};

class GotoStmt : public Stmt {
	std::vector<std::string> targets;
public:
	GotoStmt(std::vector<std::string> const& ts) : Stmt(GOTO), targets(ts) {}
	void print(std::ostream& os) const override;
	static bool classof(Ref S) { return S->getKind() == GOTO; }
};

class HavocStmt : public Stmt {
	std::vector<std::string> vars;
public:
	HavocStmt(std::vector<std::string> const& vs) : Stmt(HAVOC), vars(vs) {}
	void print(std::ostream& os) const override;
	static bool classof(Ref S) { return S->getKind() == HAVOC; }
};

class ReturnStmt : public Stmt {
	Expr::Ref expr;
public:
	ReturnStmt(Expr::Ref e = nullptr) : Stmt(RETURN), expr(e) {}
	void print(std::ostream& os) const override;
	static bool classof(Ref S) { return S->getKind() == RETURN; }
};

class IfElseStmt : public Stmt {
	Expr::Ref cond;
	BlockConstRef then;
	BlockConstRef elze;
public:
	IfElseStmt(Expr::Ref cond, BlockConstRef then, BlockConstRef elze)
		: Stmt(IFELSE), cond(cond), then(then), elze(elze) {}
	void print(std::ostream& os) const override;
	static bool classof(Ref S) { return S->getKind() == IFELSE; }
};

class WhileStmt : public Stmt {
	Expr::Ref cond;
	BlockConstRef body;
	std::vector<SpecificationRef> invars;
public:
	WhileStmt(Expr::Ref cond, BlockConstRef body, std::vector<SpecificationRef> const& invars)
		: Stmt(WHILE), cond(cond), body(body), invars(invars) {}
	void print(std::ostream& os) const override;
	static bool classof(Ref S) { return S->getKind() == WHILE; }
};

class BreakStmt : public Stmt {
public:
	BreakStmt() : Stmt(BREAK) {}
	void print(std::ostream& os) const override;
	static bool classof(Ref S) { return S->getKind() == BREAK; }
};

class LabelStmt : public Stmt {
	std::string str;
public:
	LabelStmt(std::string s) : Stmt(LABEL), str(s) {}
	void print(std::ostream& os) const override;
	static bool classof(Ref S) { return S->getKind() == LABEL; }
};

class VarDecl;
using VarDeclRef = std::shared_ptr<VarDecl>;

class Decl {
public:
	enum Kind {
		CONSTANT, VARIABLE, PROCEDURE, FUNCTION, TYPE, AXIOM, CODE, COMMENT
	};
private:
	Kind const kind;
public:
	Kind getKind() const { return kind; }
private:
	static unsigned uniqueId;
protected:
	unsigned id;
	std::string name;
	std::vector<Attr::Ref> attrs;
	Decl(Kind k, std::string n, std::vector<Attr::Ref> const& ax)
		: kind(k), id(uniqueId++), name(n), attrs(ax) { }
public:

	using Ref = std::shared_ptr<Decl>;
	using ConstRef = std::shared_ptr<Decl const>;

	virtual ~Decl() {}
	virtual void print(std::ostream& os) const = 0;
	unsigned getId() const { return id; }
	std::string getName() const { return name; }
	Expr::Ref getRefTo() const { return Expr::id(name); }
	void addAttr(Attr::Ref a) { attrs.push_back(a); }
	void addAttrs(std::vector<Attr::Ref> const& ax) { for (auto a: ax) addAttr(a); }

	static TypeDeclRef elementarytype(std::string name);
	static TypeDeclRef aliasedtype(std::string name, TypeDeclRef alias);
	static TypeDeclRef customtype(std::string name);
	static TypeDeclRef arraytype(TypeDeclRef keyType, TypeDeclRef valueType);
	static DataTypeDeclRef datatype(std::string name, std::vector<Binding> members);
	static Ref axiom(Expr::Ref e, std::string name = "");
	static FuncDeclRef function(
		std::string name,
		std::vector<Binding> const& args,
		TypeDeclRef type,
		Expr::Ref e = nullptr,
		std::vector<Attr::Ref> const& attrs = {});
	static Ref constant(std::string name, TypeDeclRef type);
	static Ref constant(std::string name, TypeDeclRef type, bool unique);
	static Ref constant(std::string name, TypeDeclRef type, std::vector<Attr::Ref> const& ax, bool unique);
	static VarDeclRef variable(std::string name, TypeDeclRef type);
	static ProcDeclRef procedure(std::string name,
		std::vector<Binding> const& params = {},
		std::vector<Binding> const& rets = {},
		std::vector<Ref> const& decls = {},
		std::vector<BlockRef> const& blocks = {});
	static Ref code(std::string name, std::string s);
	static FuncDeclRef code(ProcDeclRef P);
	static Ref comment(std::string name, std::string str);
};

class TypeDecl : public Decl {
protected:
	std::string alias;
	std::string smttype;
public:
	TypeDecl(std::string n, std::string a, std::vector<Attr::Ref> const& ax, std::string smt)
		: Decl(TYPE, n, ax), alias(a), smttype(smt) {}
	std::string getAlias() const { return alias; }
	void print(std::ostream& os) const override;
	static bool classof(Decl::ConstRef D) { return D->getKind() == TYPE; }
	std::string getSmtType() const { return smttype; }
};

class DataTypeDecl : public TypeDecl {
	std::vector<Binding> members;
public:
	DataTypeDecl(std::string n, std::string t, std::vector<Attr::Ref> const& ax, std::vector<Binding> members)
		: TypeDecl(n, t, ax, "|T@" + n + "|"), members(members) { attrs.push_back(Attr::attr("datatype")); }
	std::vector<Binding> getMembers() const { return members; }
	void print(std::ostream& os) const override;
};

class DtSelExpr : public SelExpr {
	std::string member;
	FuncDeclRef constr;
	DataTypeDeclRef dt;
public:
	DtSelExpr(Ref base, std::string member, FuncDeclRef constr, DataTypeDeclRef dt)
		: SelExpr(base), member(member), constr(constr), dt(dt) {}
	std::string getMember() const { return member; }
	FuncDeclRef getConstr() const { return constr; }
	DataTypeDeclRef getDataType() const { return dt; }
	void print(std::ostream& os) const override;
	Ref toUpdate(Ref v) const override { return Expr::dtupd(base, member, v, constr, dt); }
	Ref replaceBase(Ref b) const override { return Expr::dtsel(b, member, constr, dt); }
};

class DtUpdExpr : public UpdExpr {
	std::string member;
	FuncDeclRef constr;
	DataTypeDeclRef dt;
public:
	DtUpdExpr(Ref base, std::string member, Ref v, FuncDeclRef constr, DataTypeDeclRef dt)
		: UpdExpr(base, v), member(member), constr(constr), dt(dt) {}
	Ref getBase() const { return base; }
	std::string getMember() const { return member; }
	FuncDeclRef getConstr() const { return constr; }
	DataTypeDeclRef getDataType() const { return dt; }
	void print(std::ostream& os) const override;
};

class AxiomDecl : public Decl {
	Expr::Ref expr;
	static int uniqueId;
public:
	AxiomDecl(std::string n, Expr::Ref e) : Decl(AXIOM, n, {}), expr(e) {}
	void print(std::ostream& os) const override;
	static bool classof(Decl::ConstRef D) { return D->getKind() == AXIOM; }
};

class ConstDecl : public Decl {
	TypeDeclRef type;
	bool unique;
public:
	ConstDecl(std::string n, TypeDeclRef t, std::vector<Attr::Ref> const& ax, bool u)
		: Decl(CONSTANT, n, ax), type(t), unique(u) {}
	void print(std::ostream& os) const override;
	static bool classof(Decl::ConstRef D) { return D->getKind() == CONSTANT; }
};

class FuncDecl : public Decl {
	std::vector<Binding> params;
	TypeDeclRef type;
	Expr::Ref body;
public:
	FuncDecl(std::string n, std::vector<Attr::Ref> const& ax, std::vector<Binding> const& ps,
			TypeDeclRef t, Expr::Ref b)
		: Decl(FUNCTION, n, ax), params(ps), type(t), body(b) {}
	void print(std::ostream& os) const override;
	static bool classof(Decl::ConstRef D) { return D->getKind() == FUNCTION; }
};

class VarDecl : public Decl {
	TypeDeclRef type;
public:
	VarDecl(std::string n, TypeDeclRef t) : Decl(VARIABLE, n, {}), type(t) {}
	void print(std::ostream& os) const override;
	static bool classof(Decl::ConstRef D) { return D->getKind() == VARIABLE; }
	TypeDeclRef getType() const { return type; }
};

class Block {
	std::string name;
	typedef std::vector<Stmt::Ref> StatementList;
	StatementList stmts;
public:

	using Ref = std::shared_ptr<Block>;
	using ConstRef = std::shared_ptr<Block const>;

	static
	Ref block(std::string n = "", std::vector<Stmt::Ref> const& stmts = {}) {
		return std::make_shared<Block>(n,stmts);
	}

	Block(std::string n, std::vector<Stmt::Ref> const& stmts) : name(n), stmts(stmts) {}
	void print(std::ostream& os) const;
	typedef StatementList::iterator iterator;
	iterator begin() { return stmts.begin(); }
	iterator end() { return stmts.end(); }
	StatementList& getStatements() { return stmts; }

	void addStmt(Stmt::Ref s) {
		stmts.push_back(s);
	}

	void addStmts(std::vector<Stmt::Ref> const& stmts) {
		for (auto s: stmts)
			addStmt(s);
	}

	std::string getName() {
		return name;
	}
};

class CodeContainer {
protected:
	typedef std::vector<Decl::Ref> DeclarationList;
	typedef std::vector<Block::Ref> BlockList;
	typedef std::vector<std::string> ModifiesList;
	DeclarationList decls;
	BlockList blocks;
	ModifiesList mods;
	CodeContainer(DeclarationList ds, BlockList bs) : decls(ds), blocks(bs) {}
public:
	typedef DeclarationList::iterator decl_iterator;
	decl_iterator decl_begin() { return decls.begin(); }
	decl_iterator decl_end() { return decls.end(); }
	DeclarationList& getDeclarations() { return decls; }

	typedef BlockList::iterator iterator;
	iterator begin() { return blocks.begin(); }
	iterator end() { return blocks.end(); }
	BlockList& getBlocks() { return blocks; }

	typedef ModifiesList::iterator mod_iterator;
	mod_iterator mod_begin() { return mods.begin(); }
	mod_iterator mod_end() { return mods.end(); }
	ModifiesList& getModifies() { return mods; }
};

class CodeExpr : public Expr, public CodeContainer {
public:
	CodeExpr(DeclarationList ds, BlockList bs) : CodeContainer(ds, bs) {}
	void print(std::ostream& os) const override;
};

class Specification {
	Expr::Ref expr;
	std::vector<Attr::Ref> attrs;
public:

	using Ref = std::shared_ptr<Specification const>;

	Specification(Expr::Ref e, std::vector<Attr::Ref> const& ax)
		: expr(e), attrs(ax) {}

	void print(std::ostream& os, std::string kind) const;
	static Ref spec(Expr::Ref e, std::vector<Attr::Ref> const& ax);
	static Ref spec(Expr::Ref e);
};

class ProcDecl : public Decl, public CodeContainer {
	typedef Binding Parameter;
	typedef std::vector<Parameter> ParameterList;
	typedef std::vector<Specification::Ref> SpecificationList;

	ParameterList params;
	ParameterList rets;
	SpecificationList requires;
	SpecificationList ensures;
public:
	ProcDecl(std::string n, ParameterList ps, ParameterList rs,
		DeclarationList ds, BlockList bs)
		: Decl(PROCEDURE, n, {}), CodeContainer(ds, bs), params(ps), rets(rs) {}
	typedef ParameterList::iterator param_iterator;
	param_iterator param_begin() { return params.begin(); }
	param_iterator param_end() { return params.end(); }
	ParameterList& getParameters() { return params; }

	param_iterator returns_begin() { return rets.begin(); }
	param_iterator returns_end() { return rets.end(); }
	ParameterList& getReturns() { return rets; }

	typedef SpecificationList::iterator spec_iterator;
	spec_iterator requires_begin() { return requires.begin(); }
	spec_iterator requires_end() { return requires.end(); }
	SpecificationList& getRequires() { return requires; }

	spec_iterator ensures_begin() { return ensures.begin(); }
	spec_iterator ensures_end() { return ensures.end(); }
	SpecificationList& getEnsures() { return ensures; }

	void print(std::ostream& os) const override;
	static bool classof(Decl::ConstRef D) { return D->getKind() == PROCEDURE; }
};

class CodeDecl : public Decl {
	std::string code;
public:
	CodeDecl(std::string name, std::string s) : Decl(CODE, name, {}), code(s) {}
	void print(std::ostream& os) const override;
	static bool classof(Decl::ConstRef D) { return D->getKind() == CODE; }
};

class CommentDecl : public Decl {
	std::string str;
public:
	CommentDecl(std::string name, std::string str) : Decl(COMMENT, name, {}), str(str) {}
	void print(std::ostream& os) const override;
	static bool classof(Decl::ConstRef D) { return D->getKind() == COMMENT; }
};

class Program {
	std::string prelude;
	typedef std::vector<Decl::Ref> DeclarationList;
	DeclarationList decls;
public:
	Program() {}
	void print(std::ostream& os) const;
	typedef DeclarationList::iterator iterator;
	iterator begin() { return decls.begin(); }
	iterator end() { return decls.end(); }
	unsigned size() { return decls.size(); }
	bool empty() { return decls.empty(); }
	DeclarationList& getDeclarations() { return decls; }
	void appendPrelude(std::string s) { prelude += s; }
};

std::ostream& operator<<(std::ostream& os, Expr const& e);
std::ostream& operator<<(std::ostream& os, Expr::Ref e);

std::ostream& operator<<(std::ostream& os, Decl& e);
std::ostream& operator<<(std::ostream& os, Decl::Ref e);
std::ostream& operator<<(std::ostream& os, Decl::ConstRef e);

}
