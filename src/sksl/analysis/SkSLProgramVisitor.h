/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkSLProgramVisitor_DEFINED
#define SkSLProgramVisitor_DEFINED

#include <memory>

// #include "include/private/base/SkAPI.h"

#include "src/sksl/ir/SkSLBinaryExpression.h"
#include "src/sksl/ir/SkSLBlock.h"
#include "src/sksl/ir/SkSLChildCall.h"
#include "src/sksl/ir/SkSLConstructor.h"
#include "src/sksl/ir/SkSLDoStatement.h"
#include "src/sksl/ir/SkSLExpression.h"
#include "src/sksl/ir/SkSLExpressionStatement.h"
#include "src/sksl/ir/SkSLFieldAccess.h"
#include "src/sksl/ir/SkSLForStatement.h"
#include "src/sksl/ir/SkSLFunctionCall.h"
#include "src/sksl/ir/SkSLFunctionDeclaration.h"
#include "src/sksl/ir/SkSLFunctionDefinition.h"
#include "src/sksl/ir/SkSLIRNode.h"
#include "src/sksl/ir/SkSLIfStatement.h"
#include "src/sksl/ir/SkSLIndexExpression.h"
#include "src/sksl/ir/SkSLLayout.h"
#include "src/sksl/ir/SkSLModifierFlags.h"
#include "src/sksl/ir/SkSLPostfixExpression.h"
#include "src/sksl/ir/SkSLPrefixExpression.h"
#include "src/sksl/ir/SkSLProgram.h"
#include "src/sksl/ir/SkSLProgramElement.h"
#include "src/sksl/ir/SkSLReturnStatement.h"
#include "src/sksl/ir/SkSLStatement.h"
#include "src/sksl/ir/SkSLSwitchCase.h"
#include "src/sksl/ir/SkSLSwitchStatement.h"
#include "src/sksl/ir/SkSLSwizzle.h"
#include "src/sksl/ir/SkSLSymbol.h"
#include "src/sksl/ir/SkSLTernaryExpression.h"
#include "src/sksl/ir/SkSLType.h"
#include "src/sksl/ir/SkSLVarDeclarations.h"
#include "src/sksl/ir/SkSLVariable.h"
#include "src/sksl/ir/SkSLVariableReference.h"

namespace SkSL {

struct Program;
class Expression;
class Statement;
class ProgramElement;

/**
 * Utility class to visit every element, statement, and expression in an SkSL program IR.
 * This is intended for simple analysis and accumulation, where custom visitation behavior is only
 * needed for a limited set of expression kinds.
 *
 * Subclasses should override visitExpression/visitStatement/visitProgramElement as needed and
 * intercept elements of interest. They can then invoke the base class's function to visit all
 * sub expressions. They can also choose not to call the base function to arrest recursion, or
 * implement custom recursion.
 *
 * The visit functions return a bool that determines how the default implementation recurses. Once
 * any visit call returns true, the default behavior stops recursing and propagates true up the
 * stack.
 */
template <typename T> class TProgramVisitor {
public:
    virtual ~TProgramVisitor() = default;

protected:
    virtual bool visitExpression(typename T::Expression& expression) {
        typename T::Expression& e = expression;
        switch (e.kind()) {
            case Expression::Kind::kEmpty:
            case Expression::Kind::kFunctionReference:
            case Expression::Kind::kLiteral:
            case Expression::Kind::kMethodReference:
            case Expression::Kind::kPoison:
            case Expression::Kind::kSetting:
            case Expression::Kind::kTypeReference:
            case Expression::Kind::kVariableReference:
                // Leaf expressions return false
                return false;

            case Expression::Kind::kBinary: {
                auto& b = e.template as<BinaryExpression>();
                return (b.left() && this->visitExpressionPtr(b.left())) ||
                       (b.right() && this->visitExpressionPtr(b.right()));
            }
            case Expression::Kind::kChildCall: {
                // We don't visit the child variable itself, just the arguments
                auto& c = e.template as<ChildCall>();
                for (auto& arg : c.arguments()) {
                    if (arg && this->visitExpressionPtr(arg)) {
                        return true;
                    }
                }
                return false;
            }
            case Expression::Kind::kConstructorArray:
            case Expression::Kind::kConstructorArrayCast:
            case Expression::Kind::kConstructorCompound:
            case Expression::Kind::kConstructorCompoundCast:
            case Expression::Kind::kConstructorDiagonalMatrix:
            case Expression::Kind::kConstructorMatrixResize:
            case Expression::Kind::kConstructorScalarCast:
            case Expression::Kind::kConstructorSplat:
            case Expression::Kind::kConstructorStruct: {
                auto& c = e.asAnyConstructor();
                for (auto& arg : c.argumentSpan()) {
                    if (this->visitExpressionPtr(arg)) {
                        return true;
                    }
                }
                return false;
            }
            case Expression::Kind::kFieldAccess:
                return this->visitExpressionPtr(e.template as<FieldAccess>().base());

            case Expression::Kind::kFunctionCall: {
                auto& c = e.template as<FunctionCall>();
                for (auto& arg : c.arguments()) {
                    if (arg && this->visitExpressionPtr(arg)) {
                        return true;
                    }
                }
                return false;
            }
            case Expression::Kind::kIndex: {
                auto& i = e.template as<IndexExpression>();
                return this->visitExpressionPtr(i.base()) || this->visitExpressionPtr(i.index());
            }
            case Expression::Kind::kPostfix:
                return this->visitExpressionPtr(e.template as<PostfixExpression>().operand());

            case Expression::Kind::kPrefix:
                return this->visitExpressionPtr(e.template as<PrefixExpression>().operand());

            case Expression::Kind::kSwizzle: {
                auto& s = e.template as<Swizzle>();
                return s.base() && this->visitExpressionPtr(s.base());
            }

            case Expression::Kind::kTernary: {
                auto& t = e.template as<TernaryExpression>();
                return this->visitExpressionPtr(t.test()) ||
                       (t.ifTrue() && this->visitExpressionPtr(t.ifTrue())) ||
                       (t.ifFalse() && this->visitExpressionPtr(t.ifFalse()));
            }
            default:
                SkUNREACHABLE;
        }
    }
    virtual bool visitStatement(typename T::Statement& statement) {
        typename T::Statement& s = statement;
        switch (s.kind()) {
            case Statement::Kind::kBreak:
            case Statement::Kind::kContinue:
            case Statement::Kind::kDiscard:
            case Statement::Kind::kNop:
                // Leaf statements just return false
                return false;

            case Statement::Kind::kBlock:
                for (auto& stmt : s.template as<Block>().children()) {
                    if (stmt && this->visitStatementPtr(stmt)) {
                        return true;
                    }
                }
                return false;

            case Statement::Kind::kSwitchCase: {
                auto& sc = s.template as<SwitchCase>();
                return this->visitStatementPtr(sc.statement());
            }
            case Statement::Kind::kDo: {
                auto& d = s.template as<DoStatement>();
                return this->visitExpressionPtr(d.test()) || this->visitStatementPtr(d.statement());
            }
            case Statement::Kind::kExpression:
                return this->visitExpressionPtr(s.template as<ExpressionStatement>().expression());

            case Statement::Kind::kFor: {
                auto& f = s.template as<ForStatement>();
                return (f.initializer() && this->visitStatementPtr(f.initializer())) ||
                       (f.test() && this->visitExpressionPtr(f.test())) ||
                       (f.next() && this->visitExpressionPtr(f.next())) ||
                       this->visitStatementPtr(f.statement());
            }
            case Statement::Kind::kIf: {
                auto& i = s.template as<IfStatement>();
                return (i.test() && this->visitExpressionPtr(i.test())) ||
                       (i.ifTrue() && this->visitStatementPtr(i.ifTrue())) ||
                       (i.ifFalse() && this->visitStatementPtr(i.ifFalse()));
            }
            case Statement::Kind::kReturn: {
                auto& r = s.template as<ReturnStatement>();
                return r.expression() && this->visitExpressionPtr(r.expression());
            }
            case Statement::Kind::kSwitch: {
                auto& sw = s.template as<SwitchStatement>();
                return this->visitExpressionPtr(sw.value()) ||
                       this->visitStatementPtr(sw.caseBlock());
            }
            case Statement::Kind::kVarDeclaration: {
                auto& v = s.template as<VarDeclaration>();
                return v.value() && this->visitExpressionPtr(v.value());
            }
            default:
                SkUNREACHABLE;
        }
    }
    virtual bool visitProgramElement(typename T::ProgramElement& programElement) {
        typename T::ProgramElement& pe = programElement;
        switch (pe.kind()) {
            case ProgramElement::Kind::kExtension:
            case ProgramElement::Kind::kFunctionPrototype:
            case ProgramElement::Kind::kInterfaceBlock:
            case ProgramElement::Kind::kModifiers:
            case ProgramElement::Kind::kStructDefinition:
                // Leaf program elements just return false by default
                return false;

            case ProgramElement::Kind::kFunction:
                return this->visitStatementPtr(pe.template as<FunctionDefinition>().body());

            case ProgramElement::Kind::kGlobalVar:
                return this->visitStatementPtr(
                        pe.template as<GlobalVarDeclaration>().declaration());

            default:
                SkUNREACHABLE;
        }
    }

    virtual bool visitExpressionPtr(typename T::UniquePtrExpression& expr) = 0;
    virtual bool visitStatementPtr(typename T::UniquePtrStatement& stmt) = 0;
};

// ProgramVisitors take const types; ProgramWriters do not.
struct ProgramVisitorTypes {
    using Program = const SkSL::Program;
    using Expression = const SkSL::Expression;
    using Statement = const SkSL::Statement;
    using ProgramElement = const SkSL::ProgramElement;
    using UniquePtrExpression = const std::unique_ptr<SkSL::Expression>;
    using UniquePtrStatement = const std::unique_ptr<SkSL::Statement>;
};

extern template class TProgramVisitor<ProgramVisitorTypes>;

class ProgramVisitor : public TProgramVisitor<ProgramVisitorTypes> {
public:
    bool visit(const Program& program) {
        for (const ProgramElement* pe : program.elements()) {
            if (this->visitProgramElement(*pe)) {
                return true;
            }
        }
        return false;
    }

private:
    // ProgramVisitors shouldn't need access to unique_ptrs, and marking these as final should help
    // these accessors inline away. Use ProgramWriter if you need the unique_ptrs.
    bool visitExpressionPtr(const std::unique_ptr<Expression>& e) final {
        return this->visitExpression(*e);
    }
    bool visitStatementPtr(const std::unique_ptr<Statement>& s) final {
        return this->visitStatement(*s);
    }
};

}  // namespace SkSL

#endif
