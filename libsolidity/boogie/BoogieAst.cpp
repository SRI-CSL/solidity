//
// This file is distributed under the MIT License. See SMACK-LICENSE for details.
//
#include <libsolidity/boogie/BoogieAst.h>
#include <liblangutil/Exceptions.h>
#include <sstream>
#include <iostream>
#include <set>
#include <cassert>
#include <cstring>
#include <boost/algorithm/string/predicate.hpp>
#include <liblangutil/Exceptions.h>

namespace boogie {

unsigned Decl::uniqueId = 0;

void Expr::printSMT2(std::ostream& out) const
{
	// By default print Boogie, override if differences
	print(out);
}

std::string Expr::toString() const
{
	std::stringstream ss;
	print(ss);
	return ss.str();
}

std::string Expr::toSMT2() const
{
	std::stringstream ss;
	printSMT2(ss);
	return ss.str();
}

Expr::Ref Expr::error()
{
	return std::make_shared<ErrorExpr const>();
}

Expr::Ref Expr::exists(std::vector<Binding> const& vars, Ref e)
{
	return std::make_shared<QuantExpr const>(QuantExpr::Exists, vars, e);
}

Expr::Ref Expr::forall(std::vector<Binding> const& vars, Ref e)
{
	return std::make_shared<QuantExpr const>(QuantExpr::Forall, vars, e);
}

Expr::Ref Expr::and_(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::And, l, r);
}

Expr::Ref Expr::and_(std::vector<Expr::Ref> const& es)
{
	if (es.size() == 0)
		return lit(true);
	Expr::Ref result = es[0];
	for (size_t i = 1; i < es.size(); ++ i)
		result = and_(result, es[i]);
	return result;
}

Expr::Ref Expr::or_(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Or, l, r);
}

Expr::Ref Expr::or_(std::vector<Expr::Ref> const& es)
{
	if (es.size() == 0)
		return lit(false);
	Expr::Ref result = es[0];
	for (size_t i = 1; i < es.size(); ++ i)
		result = or_(result, es[i]);
	return result;
}

Expr::Ref Expr::cond(Ref c, Ref t, Ref e)
{
	return std::make_shared<CondExpr const>(c,t,e);
}

Expr::Ref Expr::oneOf(std::vector<Expr::Ref> const& es)
{
	std::vector<Expr::Ref> conjuncts(es);
	std::vector<Expr::Ref> disjuncts;
	for (size_t i = 0; i < es.size(); ++ i) {
		Expr::Ref pick = es[i];
		conjuncts[i] = not_(pick);
		disjuncts.push_back(and_(conjuncts));
		conjuncts[i] = pick;
	}
	return or_(disjuncts);
}


Expr::Ref Expr::eq(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Eq, l, r);
}

Expr::Ref Expr::lt(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Lt, l, r);
}

Expr::Ref Expr::gt(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Gt, l, r);
}

Expr::Ref Expr::lte(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Lte, l, r);
}

Expr::Ref Expr::gte(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Gte, l, r);
}

Expr::Ref Expr::plus(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Plus, l, r);
}

Expr::Ref Expr::minus(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Minus, l, r);
}

Expr::Ref Expr::div(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Div, l, r);
}

Expr::Ref Expr::intdiv(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::IntDiv, l, r);
}

Expr::Ref Expr::times(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Times, l, r);
}

Expr::Ref Expr::mod(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Mod, l, r);
}

Expr::Ref Expr::exp(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Exp, l, r);
}

Expr::Ref Expr::fn(std::string f, std::vector<Ref> const& args)
{
	return std::make_shared<FunExpr const>(f, args);
}

Expr::Ref Expr::fn(std::string f, Ref x)
{
	return std::make_shared<FunExpr const>(f, std::vector<Ref>{x});
}

Expr::Ref Expr::fn(std::string f, Ref x, Ref y)
{
	return std::make_shared<FunExpr const>(f, std::vector<Ref>{x, y});
}

Expr::Ref Expr::fn(std::string f, Ref x, Ref y, Ref z)
{
	return std::make_shared<FunExpr const>(f, std::vector<Ref>{x, y, z});
}

Expr::Ref Expr::id(std::string s)
{
	return std::make_shared<VarExpr const>(s);
}

Expr::Ref Expr::impl(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Imp, l, r);
}

Expr::Ref Expr::iff(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Iff, l, r);
}

Expr::Ref Expr::lit(bool b)
{
	return std::make_shared<BoolLit const>(b);
}

Expr::Ref Expr::lit(std::string v)
{
	return std::make_shared<StringLit const>(v);
}

Expr::Ref Expr::lit(unsigned long v)
{
	return std::make_shared<IntLit const>(v);
}

Expr::Ref Expr::lit(long v)
{
	return std::make_shared<IntLit const>(v);
}

Expr::Ref Expr::lit(bigint v)
{
	return std::make_shared<IntLit const>(v);
}

Expr::Ref Expr::lit(std::string v, unsigned w)
{
	return w ? (Ref) new BvLit(v,w) : (Ref) new IntLit(v);
}

Expr::Ref Expr::lit(bigint v, unsigned w)
{
	return std::make_shared<BvLit const>(v,w);
}

Expr::Ref Expr::neq(Ref l, Ref r)
{
	return std::make_shared<BinExpr const>(BinExpr::Neq, l, r);
}

Expr::Ref Expr::not_(Ref e)
{
	return std::make_shared<NotExpr const>(e);
}

Expr::Ref Expr::neg(Ref e)
{
	return std::make_shared<NegExpr const>(e);
}

Expr::Ref Expr::arrconst(TypeDeclRef arrType, Ref val)
{
	return std::make_shared<ArrConstExpr const>(arrType, val);
}

Expr::Ref Expr::arrsel(Ref b, Ref i)
{
	return std::make_shared<ArrSelExpr const>(b, i);
}

Expr::Ref Expr::arrupd(Ref b, Ref i, Ref v)
{
	return std::make_shared<ArrUpdExpr const>(b, i, v);
}

Expr::Ref Expr::dtsel(Ref b, std::string mem, FuncDeclRef constr, DataTypeDeclRef dt)
{
	return std::make_shared<DtSelExpr>(b, mem, constr, dt);
}

Expr::Ref Expr::dtupd(Ref b, std::string mem, Ref v, FuncDeclRef constr, DataTypeDeclRef dt)
{
	return std::make_shared<DtUpdExpr>(b, mem, v, constr, dt);
}

Expr::Ref Expr::old(Ref expr)
{
	return std::make_shared<OldExpr const>(expr);
}

Expr::Ref Expr::tuple(std::vector<Ref> const& e)
{
	return std::make_shared<TupleExpr const>(e);
}

Attr::Ref Attr::attr(std::string s, std::vector<Expr::Ref> const& vs)
{
	return std::make_shared<Attr const>(s,vs);
}

Attr::Ref Attr::attr(std::string s)
{
	return attr(s, std::vector<Expr::Ref>());
}

Attr::Ref Attr::attr(std::string s, std::string v)
{
	return Attr::Ref(new Attr(s, { Expr::lit(v) }));
}

Attr::Ref Attr::attr(std::string s, int v)
{
	return attr(s, {Expr::lit((long) v)});
}

Attr::Ref Attr::attr(std::string s, std::string v, int i)
{
	return attr(s, std::vector<Expr::Ref>{ Expr::lit(v), Expr::lit((long) i) });
}

Attr::Ref Attr::attr(std::string s, std::string v, int i, int j)
{
	return attr(s, {Expr::lit(v), Expr::lit((long) i), Expr::lit((long) j)});
}

Stmt::Ref Stmt::annot(std::vector<Attr::Ref> const& attrs)
{
	AssumeStmt* s = new AssumeStmt(Expr::lit(true));
	for (auto A: attrs)
		s->add(A);
	return std::shared_ptr<AssumeStmt const>(s);
}

Stmt::Ref Stmt::annot(Attr::Ref a)
{
	return Stmt::annot(std::vector<Attr::Ref>{a});
}

Stmt::Ref Stmt::assert_(Expr::Ref e, std::vector<Attr::Ref> const& attrs)
{
	return std::make_shared<AssertStmt const>(e, attrs);
}

Expr::Ref Expr::selectToUpdate(Expr::Ref sel, Expr::Ref value)
{
	if (auto selExpr = std::dynamic_pointer_cast<SelExpr const>(sel))
	{
		if (auto base = std::dynamic_pointer_cast<SelExpr const>(selExpr->getBase()))
			return selectToUpdate(base, selExpr->toUpdate(value));
		else
			return selExpr->toUpdate(value);
	}
	solAssert(false, "Expected datatype/array select");
	return nullptr;
}

Stmt::Ref Stmt::assign(Expr::Ref e, Expr::Ref f)
{
	if (auto cond = std::dynamic_pointer_cast<CondExpr const>(e))
		return Stmt::ifelse(cond->getCond(),
				Block::block("", {Stmt::assign(cond->getThen(), f)}),
				Block::block("", {Stmt::assign(cond->getElse(), f)}));
	if (auto sel = std::dynamic_pointer_cast<SelExpr const>(e))
	{
		auto upd = std::dynamic_pointer_cast<UpdExpr const>(Expr::selectToUpdate(sel, f));
		solAssert(upd, "Update expression expected");
		return Stmt::assign(upd->getBase(), upd);
	}
	return std::make_shared<AssignStmt const>(std::vector<Expr::Ref>{e}, std::vector<Expr::Ref>{f});
}

Stmt::Ref Stmt::assume(Expr::Ref e)
{
	return std::make_shared<AssumeStmt const>(e);
}

Stmt::Ref Stmt::assume(Expr::Ref e, Attr::Ref a)
{
	AssumeStmt* s = new AssumeStmt(e);
	s->add(a);
	return std::shared_ptr<AssumeStmt const>(s);
}

Stmt::Ref Stmt::call(std::string p, std::vector<Expr::Ref> const& args, std::vector<std::string> const& rets,
		std::vector<Attr::Ref> const& attrs)
{
	return std::make_shared<CallStmt const>(p, attrs, args, rets);
}

Stmt::Ref Stmt::comment(std::string s)
{
	return std::make_shared<Comment const>(s);
}

Stmt::Ref Stmt::goto_(std::vector<std::string> const& ts)
{
	return std::make_shared<GotoStmt const>(ts);
}

Stmt::Ref Stmt::havoc(std::string x)
{
	return std::make_shared<HavocStmt const>(std::vector<std::string>{x});
}

Stmt::Ref Stmt::return_(Expr::Ref e)
{
	return std::make_shared<ReturnStmt const>(e);
}

Stmt::Ref Stmt::return_()
{
	return std::make_shared<ReturnStmt const>();
}

Stmt::Ref Stmt::skip()
{
	return std::make_shared<AssumeStmt const>(Expr::lit(true));
}

Stmt::Ref Stmt::ifelse(Expr::Ref cond, Block::ConstRef then, Block::ConstRef elze)
{
	return std::make_shared<IfElseStmt const>(cond, then, elze);
}

Stmt::Ref Stmt::while_(Expr::Ref cond, Block::ConstRef body, std::vector<Specification::Ref> const& invars)
{
	return std::make_shared<WhileStmt const>(cond, body, invars);
}

Stmt::Ref Stmt::break_()
{
	return std::make_shared<BreakStmt const>();
}

Stmt::Ref Stmt::label(std::string s)
{
	return std::make_shared<LabelStmt const>(s);
}

TypeDeclRef Decl::elementarytype(std::string name)
{
	std::string smtname = name;
	if (name == "int" || name == "bool")
		smtname[0] = toupper(smtname[0]);
	if (boost::starts_with(name, "bv"))
		smtname = "(_ BitVec " + name.substr(2) + ")";
	return std::make_shared<TypeDecl>(name, "", std::vector<Attr::Ref>(), smtname);
}

TypeDeclRef Decl::aliasedtype(std::string name, TypeDeclRef alias)
{
	return std::make_shared<TypeDecl>(name, alias->getName(), std::vector<Attr::Ref>(), alias->getSmtType());
}

TypeDeclRef Decl::customtype(std::string name)
{
	return std::make_shared<TypeDecl>(name, "", std::vector<Attr::Ref>(), "T@" + name);
}

TypeDeclRef Decl::arraytype(TypeDeclRef keyType, TypeDeclRef valueType)
{
	return std::make_shared<TypeDecl>("[" + keyType->getName() + "]" + valueType->getName(),
			"", std::vector<Attr::Ref>(),
			"(Array " + keyType->getSmtType() + " " + valueType->getSmtType() + ")");
}

DataTypeDeclRef Decl::datatype(std::string name, std::vector<Binding> members)
{
	return std::make_shared<DataTypeDecl>(name, "", std::vector<Attr::Ref>(), members);
}

Decl::Ref Decl::axiom(Expr::Ref e, std::string name)
{
	return std::make_shared<AxiomDecl>(name, e);
}

FuncDeclRef Decl::function(std::string name, std::vector<Binding> const& args,
		TypeDeclRef type, Expr::Ref e, std::vector<Attr::Ref> const& attrs)
{
	return std::make_shared<FuncDecl>(name,attrs,args,type,e);
}

Decl::Ref Decl::constant(std::string name, TypeDeclRef type)
{
	return Decl::constant(name, type, {}, false);
}

Decl::Ref Decl::constant(std::string name, TypeDeclRef type, bool unique)
{
	return Decl::constant(name, type, {}, unique);
}

Decl::Ref Decl::constant(std::string name, TypeDeclRef type, std::vector<Attr::Ref> const& ax, bool unique)
{
	return std::make_shared<ConstDecl>(name, type, ax, unique);
}

VarDeclRef Decl::variable(std::string name, TypeDeclRef type)
{
	return std::make_shared<VarDecl>(name, type);
}

ProcDeclRef Decl::procedure(std::string name,
		std::vector<Binding> const& args,
		std::vector<Binding> const& rets,
		std::vector<Decl::Ref> const& decls,
		std::vector<Block::Ref> const& blocks)
{
	return std::make_shared<ProcDecl>(name, args, rets, decls, blocks);
}
Decl::Ref Decl::code(std::string name, std::string s)
{
	return std::make_shared<CodeDecl>(name, s);
}

Decl::Ref Decl::comment(std::string name, std::string str)
{
	return std::make_shared<CommentDecl>(name, str);
}

std::ostream& operator<<(std::ostream& os, Expr const& e)
{
	e.print(os);
	return os;
}

std::ostream& operator<<(std::ostream& os, Expr::Ref e)
{
	e->print(os);
	return os;
}

std::ostream& operator<<(std::ostream& os, Attr::Ref a)
{
	a->print(os);
	return os;
}

std::ostream& operator<<(std::ostream& os, Stmt::Ref s)
{
	s->print(os);
	return os;
}

std::ostream& operator<<(std::ostream& os, Block::ConstRef b)
{
	b->print(os);
	return os;
}

std::ostream& operator<<(std::ostream& os, Block::Ref b)
{
	b->print(os);
	return os;
}

std::ostream& operator<<(std::ostream& os, Decl& d)
{
	d.print(os);
	return os;
}

std::ostream& operator<<(std::ostream& os, Decl::Ref d)
{
	d->print(os);
	return os;
}

std::ostream& operator<<(std::ostream& os, Decl::ConstRef d)
{
	d->print(os);
	return os;
}

std::ostream& operator<<(std::ostream& os, Program const* p)
{
	if (p == 0)
		os << "<null> Program!\n";
	else
		p->print(os);
	return os;
}
std::ostream& operator<<(std::ostream& os, Program const& p)
{
	p.print(os);
	return os;
}

std::ostream& operator<<(std::ostream& os, Binding const& p)
{
	os << p.id << ": " << p.type->getName();
	return os;
}

template<class T>
void print_seq(std::ostream& os, std::vector<T> const& ts, std::string init, std::string sep, std::string term)
{
	os << init;
	for (auto i = ts.begin(); i != ts.end(); ++i)
		os << (i == ts.begin() ? "" : sep) << *i;
	os << term;
}

template<class T>
void print_seq(std::ostream& os, std::vector<T> const& ts, std::string sep)
{
	print_seq<T>(os, ts, "", sep, "");
}

template<class T>
void print_seq(std::ostream& os, std::vector<T> const& ts)
{
	print_seq<T>(os, ts, "", "", "");
}

template<class T, class C>
void print_set(std::ostream& os, std::set<T,C> const& ts, std::string init, std::string sep, std::string term)
{
	os << init;
	for (typename std::set<T,C>::const_iterator i = ts.begin(); i != ts.end(); ++i)
		os << (i == ts.begin() ? "" : sep) << *i;
	os << term;
}

template<class T, class C>
void print_set(std::ostream& os, std::set<T,C> const& ts, std::string sep)
{
	print_set<T,C>(os, ts, "", sep, "");
}

template<class T, class C>
void print_set(std::ostream& os, std::set<T,C> const& ts)
{
	print_set<T,C>(os, ts, "", "", "");
}

void ErrorExpr::print(std::ostream& os) const
{
	os << "ERROR";
}

void BinExpr::print(std::ostream& os) const
{
	os << "(" << lhs << " ";
	switch (op)
	{
	case Iff:
		os << "<==>";
		break;
	case Imp:
		os << "==>";
		break;
	case Or:
		os << "||";
		break;
	case And:
		os << "&&";
		break;
	case Eq:
		os << "==";
		break;
	case Neq:
		os << "!=";
		break;
	case Lt:
		os << "<";
		break;
	case Gt:
		os << ">";
		break;
	case Lte:
		os << "<=";
		break;
	case Gte:
		os << ">=";
		break;
	case Sub:
		os << "<:";
		break;
	case Conc:
		os << "++";
		break;
	case Plus:
		os << "+";
		break;
	case Minus:
		os << "-";
		break;
	case Times:
		os << "*";
		break;
	case Div:
		os << "/";
		break;
	case IntDiv:
		os << "div";
		break;
	case Mod:
		os << "mod";
		break;
	case Exp:
		os << "**";
		break;
	}
	os << " " << rhs << ")";
}

void CondExpr::print(std::ostream& os) const
{
	os << "(if " << cond << " then " << then << " else " << else_ << ")";
}

void FunExpr::print(std::ostream& os) const
{
	os << fun;
	print_seq(os, args, "(", ", ", ")");
}

void BoolLit::print(std::ostream& os) const
{
	os << (val ? "true" : "false");
}

void IntLit::print(std::ostream& os) const
{
	os << val;
}

void BvLit::print(std::ostream& os) const
{
	os << val << "bv" << width;
}

void BvLit::printSMT2(std::ostream& os) const
{
	os << "(_ bv" << val << " " << width << ")";
}

void NegExpr::print(std::ostream& os) const
{
	os << "-(" << expr << ")";
}

void NotExpr::print(std::ostream& os) const
{
	os << "!(" << expr << ")";
}

void QuantExpr::print(std::ostream& os) const
{
	os << "(";
	switch (quant)
	{
	case Forall:
		os << "forall ";
		break;
	case Exists:
		os << "exists ";
		break;
	}
	print_seq(os, vars, ", ");
	os << " :: " << expr << ")";
}

void ArrConstExpr::print(std::ostream& os) const
{
	os << "((as const " << arrType << ") " << val << ")";
}

void ArrSelExpr::print(std::ostream& os) const
{
	os << base << "[" << idx << "]";
}

void ArrUpdExpr::print(std::ostream& os) const
{
	os << base << "[" << idx << " := " << val << "]";
}

void VarExpr::print(std::ostream& os) const
{
	os << var;
}

void OldExpr::print(std::ostream& os) const
{
	os << "old(";
	expr->print(os);
	os << ")";
}

void CodeExpr::print(std::ostream& os) const
{
	os << "|{" << "\n";
	if (decls.size() > 0)
		print_seq(os, decls, "	", "\n	", "\n");
	print_seq(os, blocks, "\n");
	os << "\n" << "}|";
}

void TupleExpr::print(std::ostream& os) const
{
	print_seq(os, es, ", ");
}

void StringLit::print(std::ostream& os) const
{
	os << "\"" << val << "\"";
}

void Attr::print(std::ostream& os) const
{
	os << "{:" << name;
	if (vals.size() > 0)
		print_seq(os, vals, " ", ", ", "");
	os << "}";
}

void AssertStmt::print(std::ostream& os) const
{
	os << "assert ";
	if (attrs.size() > 0)
		print_seq(os, attrs, "", " ", " ");
	os << expr << ";";
}

void AssignStmt::print(std::ostream& os) const
{
	print_seq(os, lhs, ", ");
	os << " := ";
	print_seq(os, rhs, ", ");
	os << ";";
}

void AssumeStmt::print(std::ostream& os) const
{
	os << "assume ";
	if (attrs.size() > 0)
		print_seq(os, attrs, "", " ", " ");
	os << expr << ";";
}

void CallStmt::print(std::ostream& os) const
{
	os << "call ";
	if (attrs.size() > 0)
		print_seq(os, attrs, "", " ", " ");
	if (returns.size() > 0)
		print_seq(os, returns, "", ", ", " := ");
	os << proc;
	print_seq(os, params, "(", ", ", ")");
	os << ";";
}

void Comment::print(std::ostream& os) const
{
	os << "// " << str;
}

void GotoStmt::print(std::ostream& os) const
{
	os << "goto ";
	print_seq(os, targets, ", ");
	os << ";";
}

void HavocStmt::print(std::ostream& os) const
{
	os << "havoc ";
	print_seq(os, vars, ", ");
	os << ";";
}

void ReturnStmt::print(std::ostream& os) const
{
	os << "return";
	if (expr)
		os << " " << expr;
	os << ";";
}

void IfElseStmt::print(std::ostream& os) const
{
	os << "if (";
	cond->print(os);
	os << ") {\n";
	then->print(os);
	os << "\n	}\n";

	if (elze)
	{
		os << "	else {\n";
		elze->print(os);
		os << "\n	}\n";
	}
}

void WhileStmt::print(std::ostream& os) const
{
	os << "while (";
	if (cond)
	{
		cond->print(os);
	}
	else
	{
		// Can be null in for loops when condition is omitted, e.g. for (;;) { break }
		os << "true";
	}
	os << ")";

	if (invars.empty())
	{
		os << " {\n";
	}
	else
	{
		os << "\n";
		for (auto inv: invars)
		{
			inv->print(os, "invariant");
			os << "\n";
		}
		os << "\n	{\n";
	}
	body->print(os);
	os << "\n	}\n";
}

void BreakStmt::print(std::ostream& os) const
{
	os << "break;";
}

void LabelStmt::print(std::ostream& os) const
{
	os << str << ":";
}

void TypeDecl::print(std::ostream& os) const
{
	os << "type ";
	if (attrs.size() > 0)
		print_seq(os, attrs, "", " ", " ");
	os << name;
	if (alias != "")
		os << " = " << alias;
	os << ";";
}

void DataTypeDecl::print(std::ostream& os) const
{
	os << "type ";
	if (attrs.size() > 0)
		print_seq(os, attrs, "", " ", " ");
	os << name;
	if (alias != "")
		os << " = " << alias;
	os << ";";
}

void DtSelExpr::print(std::ostream& os) const
{
	os << member << "#" << constr->getName();
	os << "(";
	base->print(os);
	os << ")";
}

void DtUpdExpr::print(std::ostream& os) const
{
	os << constr->getName();
	os << "(";
	for (size_t i = 0; i < dt->getMembers().size(); i++)
	{
		auto mem = dt->getMembers()[i];
		std::string memberName = std::dynamic_pointer_cast<VarExpr const>(mem.id)->name();
		if (memberName == member)
			val->print(os);
		else
			Expr::dtsel(base, memberName, constr, dt)->print(os);

		if (i < dt->getMembers().size() - 1)
			os << ", ";
	}
	os << ")";
}

void AxiomDecl::print(std::ostream& os) const
{
	os << "axiom ";
	if (attrs.size() > 0)
		print_seq(os, attrs, "", " ", " ");
	os << expr << ";";
}

void ConstDecl::print(std::ostream& os) const
{
	os << "const ";
	if (attrs.size() > 0)
		print_seq(os, attrs, "", " ", " ");
	os << (unique ? "unique " : "") << name << ": " << type->getName() << ";";
}

void FuncDecl::print(std::ostream& os) const
{
	os << "function ";
	if (attrs.size() > 0)
		print_seq(os, attrs, "", " ", " ");
	os << name << "(";
	for (auto P = params.begin(), E = params.end(); P != E; ++P)
	{
		std::stringstream pname;
		pname << *(P->id);
		os << (P == params.begin() ? "" : ", ") << (pname.str() != "" ? pname.str() + ": " : "") << P->type->getName();
	}
	os << ") returns (" << type->getName() << ")";
	if (body)
		os << " { " << body << " }";
	else
		os << ";";
}

void VarDecl::print(std::ostream& os) const
{
	os << "var ";
	if (attrs.size() > 0)
		print_seq(os, attrs, "", " ", " ");
	os << name << ": " << type->getName() << ";";
}

Specification::Specification(Expr::Ref e, std::vector<Attr::Ref> const& ax)
: expr(e), attrs(ax)
{
	solAssert(e, "Specification cannot be null");
}

void Specification::print(std::ostream& os, std::string kind) const
{
	os << "	" << kind << " ";
	if (attrs.size() > 0)
			print_seq(os, attrs, "", " ", " ");
	os << expr << ";\n";
}

Specification::Ref Specification::spec(Expr::Ref e, std::vector<Attr::Ref> const& ax){
	return std::make_shared<Specification const>(e, ax);
}

Specification::Ref Specification::spec(Expr::Ref e){
	return Specification::spec(e, {});
}

void ProcDecl::print(std::ostream& os) const
{
	os << "procedure ";
	if (attrs.size() > 0)
		print_seq(os, attrs, "", " ", " ");
	os << name << "(";
	for (auto P = params.begin(), E = params.end(); P != E; ++P)
		os << (P == params.begin() ? "" : ", ") << P->id << ": " << P->type->getName();
	os << ")";
	if (rets.size() > 0)
	{
		os << "\n";
		os << "	returns (";
		for (auto R = rets.begin(), E = rets.end(); R != E; ++R)
			os << (R == rets.begin() ? "" : ", ") << R->id << ": " << R->type->getName();
		os << ")";
	}
	if (blocks.size() == 0)
		os << ";";

	if (mods.size() > 0)
	{
		os << "\n";
		print_seq(os, mods, "	modifies ", ", ", ";");
	}
	if (requires.size() > 0)
	{
		os << "\n";
		for (auto req: requires) req->print(os, "requires");
	}
	if (ensures.size() > 0)
	{
		os << "\n";
		for (auto ens: ensures) ens->print(os, "ensures");
	}
	if (blocks.size() > 0)
	{
		os << "\n";
		os << "{" << "\n";
		if (decls.size() > 0)
			print_seq(os, decls, "	", "\n	", "\n");
		print_seq(os, blocks, "\n");
		os << "\n" << "}";
	}
	os << "\n";
}

void CodeDecl::print(std::ostream& os) const
{
	os << code;
}

void CommentDecl::print(std::ostream& os) const
{
	os << "// ";
	for (char c: str)
	{
		os << c;
		if (c == '\n')
			os << "// ";
	}
}

void Block::print(std::ostream& os) const
{
	if (name != "")
		os << name << ":" << "\n";
	print_seq(os, stmts, "	", "\n	", "");
}

void Program::print(std::ostream& os) const
{
	os << prelude;
	print_seq(os, decls, "\n");
	os << "\n";
}

//
// Substitution stuff
//

Expr::Ref ErrorExpr::substitute(Expr::substitution const& s) const
{
	(void)s;
	return Expr::error();
}

Expr::Ref BinExpr::substitute(Expr::substitution const& s) const
{
	Ref lhs1 = lhs->substitute(s);
	Ref rhs1 = rhs->substitute(s);
	return std::make_shared<BinExpr const>(op, lhs1, rhs1);
}

Expr::Ref CondExpr::substitute(Expr::substitution const& s) const
{
	Ref cond1 = cond->substitute(s);
	Ref then1 = then->substitute(s);
	Ref else1 = else_->substitute(s);
	return std::make_shared<CondExpr const>(cond1, then1, else1);
}

Expr::Ref FunExpr::substitute(Expr::substitution const& s) const
{
	std::vector<Ref> args1;
	for (Ref a: args)
		args1.push_back(a->substitute(s));
	return std::make_shared<FunExpr const>(fun, args1);
}

Expr::Ref BoolLit::substitute(Expr::substitution const& s) const
{
	(void)s;
	return std::make_shared<BoolLit const>(val);
}

Expr::Ref IntLit::substitute(Expr::substitution const& s) const
{
	(void)s;
	return std::make_shared<IntLit const>(val);
}

Expr::Ref BvLit::substitute(Expr::substitution const& s) const
{
	(void)s;
	return std::make_shared<BvLit const>(val, width);
}

Expr::Ref NegExpr::substitute(Expr::substitution const& s) const
{
	Ref expr1 = expr->substitute(s);
	return std::make_shared<NegExpr const>(expr1);
}

Expr::Ref NotExpr::substitute(Expr::substitution const& s) const
{
	Ref expr1 = expr->substitute(s);
	return std::make_shared<NotExpr const>(expr1);
}

Expr::Ref QuantExpr::substitute(Expr::substitution const& s) const
{
	Expr::substitution s1(s);

	// Setup the binding (remove any bound variables from substitution)
	for (Binding b: vars)
	{
		auto varExpr = std::dynamic_pointer_cast<VarExpr const>(b.id);
		solAssert(varExpr, "Binding is not a variable");
		std::string id = varExpr->name();
		auto idFind = s.find(id);
		if (idFind != s.end())
			s1.erase(idFind);
	}

	// Substitute the expression
	Ref expr1 = expr->substitute(s1);
	return std::make_shared<QuantExpr const>(quant, vars, expr1);
}

Expr::Ref ArrConstExpr::substitute(Expr::substitution const& s) const
{
	Ref val1 = val->substitute(s);
	return std::make_shared<ArrConstExpr const>(arrType, val1);
}

Expr::Ref ArrSelExpr::substitute(Expr::substitution const& s) const
{
	Ref base1 = base->substitute(s);
	Ref idx1 = idx->substitute(s);
	return std::make_shared<ArrSelExpr const>(base1, idx1);
}

Expr::Ref ArrUpdExpr::substitute(Expr::substitution const& s) const
{
	Ref base1 = base->substitute(s);
	Ref idx1 = idx->substitute(s);
	Ref val1 = val->substitute(s);
	return std::make_shared<ArrUpdExpr const>(base1, idx1, val1);
}

Expr::Ref VarExpr::substitute(Expr::substitution const& s) const
{
	auto find = s.find(var);
	if (find != s.end())
		return find->second;
	else
		return std::make_shared<VarExpr const>(var);
}

Expr::Ref OldExpr::substitute(Expr::substitution const& s) const
{
	Ref expr1 = expr->substitute(s);
	return std::make_shared<OldExpr const>(expr1);
}

Expr::Ref CodeExpr::substitute(Expr::substitution const& s) const
{
	(void)s;
	solAssert(false, "CodeExpr not supported for substitution");
	return Expr::error();
}

Expr::Ref TupleExpr::substitute(Expr::substitution const& s) const
{
	std::vector<Ref> es1;
	for (Ref e: es)
		es1.push_back(e->substitute(s));
	return std::make_shared<TupleExpr const>(es1);
}

Expr::Ref StringLit::substitute(Expr::substitution const& s) const
{
	(void)s;
	return std::make_shared<StringLit const>(val);
}

Expr::Ref DtSelExpr::substitute(Expr::substitution const& s) const
{
	Ref base1 = base->substitute(s);
	return std::make_shared<DtSelExpr const>(base1, member, constr, dt);
}

Expr::Ref DtUpdExpr::substitute(Expr::substitution const& s) const
{
	Ref base1 = base->substitute(s);
	Ref val1 = val->substitute(s);
	return std::make_shared<DtUpdExpr const>(base1, member, val1, constr, dt);
}

//
// Containtment
//

bool ErrorExpr::contains(std::string id) const
{
	(void)id;
	return false;
}

bool BinExpr::contains(std::string id) const
{
	if (lhs->contains(id))
		return true;
	if (rhs->contains(id))
		return true;;
	return false;
}

bool CondExpr::contains(std::string id) const
{
	if (cond->contains(id))
		return true;
	if (then->contains(id))
		return true;
	if (else_->contains(id))
		return true;
	return false;
}

bool FunExpr::contains(std::string id) const
{
	for (Ref a: args)
		if (a->contains(id))
			return true;
	return false;
}

bool BoolLit::contains(std::string id) const
{
	(void) id;
	return false;
}

bool IntLit::contains(std::string id) const
{
	(void) id;
	return false;
}

bool BvLit::contains(std::string id) const
{
	(void) id;
	return false;
}

bool NegExpr::contains(std::string id) const
{
	return expr->contains(id);
}

bool NotExpr::contains(std::string id) const
{
	return expr->contains(id);
}

bool QuantExpr::contains(std::string id) const
{
	for (Binding b: vars)
	{
		auto varExpr = std::dynamic_pointer_cast<VarExpr const>(b.id);
		if (id == varExpr->name())
			return false;
	}
	return expr->contains(id);
}

bool ArrConstExpr::contains(std::string id) const
{
	return val->contains(id);
}

bool ArrSelExpr::contains(std::string id) const
{
	if (base->contains(id))
		return true;
	if (idx->contains(id))
		return true;
	return false;
}

bool ArrUpdExpr::contains(std::string id) const
{
	if (base->contains(id))
		return true;
	if (idx->contains(id))
		return true;
	if (val->contains(id))
		return true;
	return false;
}

bool VarExpr::contains(std::string id) const
{
	return id == var;
}

bool OldExpr::contains(std::string id) const
{
	return expr->contains(id);
}

bool CodeExpr::contains(std::string id) const
{
	(void)id;
	solAssert(false, "CodeExpr not supported for containment");
	return false;
}

bool TupleExpr::contains(std::string id) const
{
	for (Ref e: es)
		if (e->contains(id))
			return true;
	return false;
}

bool StringLit::contains(std::string id) const
{
	(void)id;
	return false;
}

bool DtSelExpr::contains(std::string id) const
{
	return base->contains(id);
}

bool DtUpdExpr::contains(std::string id) const
{
	if (base->contains(id))
		return true;
	if (val->contains(id))
		return true;
	return false;
}

//
// Comparison stuff
//

template<typename T>
struct CmpHelper {
	static int cmp(Expr::Ref e1, Expr::Ref e2)
	{
		auto ptr1 = dynamic_cast<T const*>(e1.get());
		solAssert(ptr1, "Wrong type");
		auto ptr2 = dynamic_cast<T const*>(e2.get());
		solAssert(ptr1, "Wrong type");
		return ptr1->cmp(*ptr2);
	}
};

int Expr::cmp(Expr::Ref e1, Expr::Ref e2)
{
	if (e1->kind() != e2->kind())
		return static_cast<int>(e1->kind()) - static_cast<int>(e2->kind());

	Kind kind = e1->kind();

	switch (kind)
	{
	case Kind::ERROR: return CmpHelper<ErrorExpr>::cmp(e1, e2);
	case Kind::EXISTS: return CmpHelper<QuantExpr>::cmp(e1, e2);
	case Kind::FORALL: return CmpHelper<QuantExpr>::cmp(e1, e2);
	case Kind::AND: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::OR: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::COND: return CmpHelper<CondExpr>::cmp(e1, e2);
	case Kind::EQ: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::LT: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::GT: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::LTE: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::GTE: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::PLUS: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::MINUS: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::SUB: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::DIV: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::INTDIV: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::TIMES: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::MOD: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::EXP: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::FN: return CmpHelper<FunExpr>::cmp(e1, e2);
	case Kind::VARIABLE: return CmpHelper<VarExpr>::cmp(e1, e2);
	case Kind::IMPL: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::IFF: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::LIT_BOOL: return CmpHelper<BoolLit>::cmp(e1, e2);
	case Kind::LIT_STRING: return CmpHelper<StringLit>::cmp(e1, e2);
	case Kind::LIT_INT: return CmpHelper<IntLit>::cmp(e1, e2);
	case Kind::LIT_BV: return CmpHelper<BvLit>::cmp(e1, e2);
	case Kind::NEQ: return CmpHelper<BinExpr>::cmp(e1, e2);
	case Kind::NOT: return CmpHelper<NotExpr>::cmp(e1, e2);
	case Kind::NEG: return CmpHelper<NegExpr>::cmp(e1, e2);
	case Kind::ARRAY_CONST: return CmpHelper<ArrConstExpr>::cmp(e1, e2);
	case Kind::ARRAY_SELECT: return CmpHelper<ArrSelExpr>::cmp(e1, e2);
	case Kind::ARRAY_UPDATE: return CmpHelper<ArrUpdExpr>::cmp(e1, e2);
	case Kind::DATATYPE_SELECT: return CmpHelper<DtSelExpr>::cmp(e1, e2);
	case Kind::DATATYPE_UPDATE: return CmpHelper<DtUpdExpr>::cmp(e1, e2);
	case Kind::OLD: return CmpHelper<OldExpr>::cmp(e1, e2);
	case Kind::TUPLE: return CmpHelper<TupleExpr>::cmp(e1, e2);
	case Kind::CODE: return CmpHelper<CodeExpr>::cmp(e1, e2);
	case Kind::CONCAT: return CmpHelper<BinExpr>::cmp(e1, e2);
	}

	solAssert(false, "Unknown expression");
	return 0;
}

Expr::Kind BinExpr::kind() const {
	switch (op)
	{
	case Iff:
		return Kind::IFF;
	case Imp:
		return Kind::IMPL;
	case Or:
		return Kind::OR;
	case And:
		return Kind::AND;
	case Eq:
		return Kind::EQ;
	case Neq:
		return Kind::NEG;
	case Lt:
		return Kind::LT;
	case Gt:
		return Kind::GT;
	case Lte:
		return Kind::LTE;
	case Gte:
		return Kind::GTE;
	case Sub:
		return Kind::SUB;
	case Conc:
		return Kind::CONCAT;
	case Plus:
		return Kind::PLUS;
	case Minus:
		return Kind::MINUS;
	case Times:
		return Kind::TIMES;
	case Div:
		return Kind::DIV;
	case IntDiv:
		return Kind::INTDIV;
	case Mod:
		return Kind::MOD;
	case Exp:
		return Kind::EXP;
	}
	return Kind::ERROR;
}

Expr::Kind QuantExpr::kind() const
{
	switch (quant)
	{
	case Exists:
		return Kind::EXISTS;
	case Forall:
		return Kind::FORALL;
	}
	return Kind::ERROR;
}

int ErrorExpr::cmp(ErrorExpr const& e) const
{
	(void) e;
	return 0;
}

int BinExpr::cmp(BinExpr const& e) const
{
	solAssert(op == e.op, "Must be same binary expression");
	return Expr::cmp({lhs, rhs}, {e.lhs, e.rhs});
}

int CondExpr::cmp(CondExpr const& e) const
{
	return Expr::cmp({cond, then, else_}, {e.cond, e.then, e.else_});
}

int FunExpr::cmp(FunExpr const& e) const
{
	int cmp = std::strcmp(fun.c_str(), e.fun.c_str());
	if (cmp != 0) return cmp;
	if (args.size() != e.args.size())
		return static_cast<int>(args.size()) - static_cast<int>(e.args.size());
	return Expr::cmp(args, e.args);
}

int BoolLit::cmp(BoolLit const& e) const
{
	return static_cast<int>(val) - static_cast<int>(e.val);
}

int IntLit::cmp(IntLit const& e) const
{
	return val.compare(e.val);
}

int BvLit::cmp(BvLit const& e) const
{
	if (width != e.width)
		return static_cast<int>(width) - static_cast<int>(e.width);
	return std::strcmp(val.c_str(), e.val.c_str());
}

int NegExpr::cmp(NegExpr const& e) const
{
	return Expr::cmp(expr, e.expr);
}

int NotExpr::cmp(NotExpr const& e) const
{
	return Expr::cmp(expr, e.expr);
}

int QuantExpr::cmp(QuantExpr const& e) const
{
	solAssert(quant == e.quant, "Must be the same quantifier");

	if (vars.size() != e.vars.size())
		return static_cast<int>(vars.size()) - static_cast<int>(e.vars.size());

	for (unsigned i = 0; i < vars.size(); ++ i)
	{
		int cmp = Expr::cmp(vars[i].id, e.vars[i].id);
		if (cmp != 0)
			return cmp;
		cmp = vars[i].type->cmp(*e.vars[i].type.get());
		if (cmp != 0)
			return cmp;
	}

	return Expr::cmp(expr, e.expr);
}

int ArrConstExpr::cmp(ArrConstExpr const& e) const
{
	int cmp = arrType->cmp(*e.arrType.get());
	if (cmp != 0)
		return cmp;
	return Expr::cmp(val, e.val);
}

int ArrSelExpr::cmp(ArrSelExpr const& e) const
{
	return Expr::cmp({base, idx}, {e.base, e.idx});
}

int ArrUpdExpr::cmp(ArrUpdExpr const& e) const
{
	return Expr::cmp({base, idx, val}, {e.base, e.idx, e.val});
}

int VarExpr::cmp(VarExpr const& e) const
{
	return std::strcmp(var.c_str(), e.var.c_str());
}

int OldExpr::cmp(OldExpr const& e) const
{
	return Expr::cmp(expr, e.expr);
}

int CodeExpr::cmp(CodeExpr const& e) const
{
	(void)e;
	solAssert(false, "CodeExpr not supported for comparison");
	return false;
}

int TupleExpr::cmp(TupleExpr const& e) const
{
	if (es.size() != e.es.size())
		return static_cast<int>(es.size()) - static_cast<int>(e.es.size());
	return Expr::cmp(es, e.es);
}

int StringLit::cmp(StringLit const& e) const
{
	return std::strcmp(val.c_str(), e.val.c_str());
}

int DtSelExpr::cmp(DtSelExpr const& e) const
{
	int cmp = std::strcmp(member.c_str(), e.member.c_str());
	if (cmp != 0)
		return cmp;
	return Expr::cmp(base, e.base);
}

int DtUpdExpr::cmp(DtUpdExpr const& e) const
{
	int cmp = std::strcmp(member.c_str(), e.member.c_str());
	if (cmp != 0)
		return cmp;
	return Expr::cmp({base, val}, {e.base, e.val});
}

int TypeDecl::cmp(TypeDecl const& td) const
{
	return std::strcmp(smttype.c_str(), td.smttype.c_str());
}

int Expr::cmp(std::vector<Ref> const& l1, std::vector<Ref> const& l2)
{
	solAssert(l1.size() == l2.size(), "Only vectors of same size");
	for (unsigned i = 0; i < l1.size(); ++ i)
	{
		int cmp = Expr::cmp(l1[i], l2[i]);
		if (cmp != 0)
			return cmp;
	}
	return 0;
}

}
