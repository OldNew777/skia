/*
 * Copyright 2020 Google LLC.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/sksl/SkSLAnalysis.h"

#include "include/core/SkSpan.h"
#include "include/core/SkTypes.h"
#include "include/private/SkSLSampleUsage.h"
#include "include/private/base/SkTArray.h"
#include "src/base/SkEnumBitMask.h"
#include "src/core/SkTHash.h"
#include "src/sksl/SkSLBuiltinTypes.h"
#include "src/sksl/SkSLCompiler.h"
#include "src/sksl/SkSLConstantFolder.h"
#include "src/sksl/SkSLContext.h"
#include "src/sksl/SkSLDefines.h"
#include "src/sksl/SkSLErrorReporter.h"
#include "src/sksl/SkSLIntrinsicList.h"
#include "src/sksl/SkSLOperator.h"
#include "src/sksl/analysis/SkSLNoOpErrorReporter.h"
#include "src/sksl/analysis/SkSLProgramUsage.h"
#include "src/sksl/analysis/SkSLProgramVisitor.h"
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
#include "src/sksl/transform/SkSLProgramWriter.h"

#include <optional>
#include <string>
#include <string_view>

namespace SkSL {

namespace {

// Visitor that determines the merged SampleUsage for a given child in the program.
class MergeSampleUsageVisitor : public ProgramVisitor {
public:
    MergeSampleUsageVisitor(const Context& context,
                            const Variable& child,
                            bool writesToSampleCoords)
            : fContext(context), fChild(child), fWritesToSampleCoords(writesToSampleCoords) {}

    SampleUsage visit(const Program& program) {
        fUsage = SampleUsage(); // reset to none
        INHERITED::visit(program);
        return fUsage;
    }

    int elidedSampleCoordCount() const { return fElidedSampleCoordCount; }

protected:
    const Context& fContext;
    const Variable& fChild;
    const Variable* fMainCoordsParam = nullptr;
    const bool fWritesToSampleCoords;
    SampleUsage fUsage;
    int fElidedSampleCoordCount = 0;

    bool visitProgramElement(const ProgramElement& pe) override {
        fMainCoordsParam = pe.is<FunctionDefinition>()
                               ? pe.as<FunctionDefinition>().declaration().getMainCoordsParameter()
                               : nullptr;
        return INHERITED::visitProgramElement(pe);
    }

    bool visitExpression(const Expression& e) override {
        switch (e.kind()) {
            case ExpressionKind::kChildCall: {
                const ChildCall& cc = e.as<ChildCall>();
                if (&cc.child() == &fChild) {
                    // Determine the type of call at this site, and merge it with the accumulated
                    // state
                    const ExpressionArray& arguments = cc.arguments();
                    SkASSERT(!arguments.empty());

                    const Expression* maybeCoords = arguments[0].get();
                    if (maybeCoords->type().matches(*fContext.fTypes.fFloat2)) {
                        // If the coords are a direct reference to the program's sample-coords, and
                        // those coords are never modified, we can conservatively turn this into
                        // PassThrough sampling. In all other cases, we consider it Explicit.
                        if (!fWritesToSampleCoords && maybeCoords->is<VariableReference>() &&
                            maybeCoords->as<VariableReference>().variable() == fMainCoordsParam) {
                            fUsage.merge(SampleUsage::PassThrough());
                            ++fElidedSampleCoordCount;
                        } else {
                            fUsage.merge(SampleUsage::Explicit());
                        }
                    } else {
                        // child(inputColor) or child(srcColor, dstColor) -> PassThrough
                        fUsage.merge(SampleUsage::PassThrough());
                    }
                }
                break;
            }
            case ExpressionKind::kFunctionCall: {
                // If this child effect is ever passed via a function call...
                const FunctionCall& call = e.as<FunctionCall>();
                for (const std::unique_ptr<Expression>& arg : call.arguments()) {
                    if (arg->is<VariableReference>() &&
                        arg->as<VariableReference>().variable() == &fChild) {
                        // ... we must treat it as explicitly sampled, since the program's
                        // sample-coords only exist as a parameter to `main`.
                        fUsage.merge(SampleUsage::Explicit());
                        break;
                    }
                }
                break;
            }
            default:
                break;
        }
        return INHERITED::visitExpression(e);
    }

    using INHERITED = ProgramVisitor;
};

// Visitor that searches for child calls from a function other than main()
class SampleOutsideMainVisitor : public ProgramVisitor {
public:
    SampleOutsideMainVisitor() {}

    bool visitExpression(const Expression& e) override {
        if (e.is<ChildCall>()) {
            return true;
        }
        return INHERITED::visitExpression(e);
    }

    bool visitProgramElement(const ProgramElement& p) override {
        return p.is<FunctionDefinition>() &&
               !p.as<FunctionDefinition>().declaration().isMain() &&
               INHERITED::visitProgramElement(p);
    }

    using INHERITED = ProgramVisitor;
};

class ReturnsNonOpaqueColorVisitor : public ProgramVisitor {
public:
    ReturnsNonOpaqueColorVisitor() {}

    bool visitStatement(const Statement& s) override {
        if (s.is<ReturnStatement>()) {
            const Expression* e = s.as<ReturnStatement>().expression().get();
            bool knownOpaque = e && e->type().slotCount() == 4 &&
                               ConstantFolder::GetConstantValueForVariable(*e)
                                               ->getConstantValue(/*n=*/3)
                                               .value_or(0) == 1;
            return !knownOpaque;
        }
        return INHERITED::visitStatement(s);
    }

    bool visitExpression(const Expression& e) override {
        // No need to recurse into expressions, these can never contain return statements
        return false;
    }

    using INHERITED = ProgramVisitor;
    using INHERITED::visitProgramElement;
};

// Visitor that counts the number of nodes visited
class NodeCountVisitor : public ProgramVisitor {
public:
    NodeCountVisitor(int limit) : fLimit(limit) {}

    int visit(const Statement& s) {
        this->visitStatement(s);
        return fCount;
    }

    bool visitExpression(const Expression& e) override {
        ++fCount;
        return (fCount >= fLimit) || INHERITED::visitExpression(e);
    }

    bool visitProgramElement(const ProgramElement& p) override {
        ++fCount;
        return (fCount >= fLimit) || INHERITED::visitProgramElement(p);
    }

    bool visitStatement(const Statement& s) override {
        ++fCount;
        return (fCount >= fLimit) || INHERITED::visitStatement(s);
    }

private:
    int fCount = 0;
    int fLimit;

    using INHERITED = ProgramVisitor;
};

class VariableWriteVisitor : public ProgramVisitor {
public:
    VariableWriteVisitor(const Variable* var)
        : fVar(var) {}

    bool visit(const Statement& s) {
        return this->visitStatement(s);
    }

    bool visitExpression(const Expression& e) override {
        if (e.is<VariableReference>()) {
            const VariableReference& ref = e.as<VariableReference>();
            if (ref.variable() == fVar &&
                (ref.refKind() == VariableReference::RefKind::kWrite ||
                 ref.refKind() == VariableReference::RefKind::kReadWrite ||
                 ref.refKind() == VariableReference::RefKind::kPointer)) {
                return true;
            }
        }
        return INHERITED::visitExpression(e);
    }

private:
    const Variable* fVar;

    using INHERITED = ProgramVisitor;
};

// This isn't actually using ProgramVisitor, because it only considers a subset of the fields for
// any given expression kind. For instance, when indexing an array (e.g. `x[1]`), we only want to
// know if the base (`x`) is assignable; the index expression (`1`) doesn't need to be.
class IsAssignableVisitor {
public:
    IsAssignableVisitor(ErrorReporter* errors) : fErrors(errors) {}

    bool visit(Expression& expr, Analysis::AssignmentInfo* info) {
        int oldErrorCount = fErrors->errorCount();
        this->visitExpression(expr);
        if (info) {
            info->fAssignedVar = fAssignedVar;
        }
        return fErrors->errorCount() == oldErrorCount;
    }

    void visitExpression(Expression& expr, const FieldAccess* fieldAccess = nullptr) {
        switch (expr.kind()) {
            case Expression::Kind::kVariableReference: {
                VariableReference& varRef = expr.as<VariableReference>();
                const Variable* var = varRef.variable();
                auto fieldName = [&] {
                    return fieldAccess ? fieldAccess->description(OperatorPrecedence::kExpression)
                                       : std::string(var->name());
                };
                if (var->modifierFlags().isConst() || var->modifierFlags().isUniform()) {
                    fErrors->error(expr.fPosition,
                                   "cannot modify immutable variable '" + fieldName() + "'");
                } else if (var->storage() == Variable::Storage::kGlobal &&
                           (var->modifierFlags() & ModifierFlag::kIn)) {
                    fErrors->error(expr.fPosition,
                                   "cannot modify pipeline input variable '" + fieldName() + "'");
                } else {
                    SkASSERT(fAssignedVar == nullptr);
                    fAssignedVar = &varRef;
                }
                break;
            }
            case Expression::Kind::kFieldAccess: {
                const FieldAccess& f = expr.as<FieldAccess>();
                this->visitExpression(*f.base(), &f);
                break;
            }
            case Expression::Kind::kSwizzle: {
                const Swizzle& swizzle = expr.as<Swizzle>();
                this->checkSwizzleWrite(swizzle);
                this->visitExpression(*swizzle.base(), fieldAccess);
                break;
            }
            case Expression::Kind::kIndex:
                this->visitExpression(*expr.as<IndexExpression>().base(), fieldAccess);
                break;

            case Expression::Kind::kPoison:
                break;

            default:
                fErrors->error(expr.fPosition, "cannot assign to this expression");
                break;
        }
    }

private:
    void checkSwizzleWrite(const Swizzle& swizzle) {
        int bits = 0;
        for (int8_t idx : swizzle.components()) {
            SkASSERT(idx >= SwizzleComponent::X && idx <= SwizzleComponent::W);
            int bit = 1 << idx;
            if (bits & bit) {
                fErrors->error(swizzle.fPosition,
                               "cannot write to the same swizzle field more than once");
                break;
            }
            bits |= bit;
        }
    }

    ErrorReporter* fErrors;
    VariableReference* fAssignedVar = nullptr;

    using INHERITED = ProgramVisitor;
};

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// Analysis

SK_API SampleUsage Analysis::GetSampleUsage(const Program& program,
                                     const Variable& child,
                                     bool writesToSampleCoords,
                                     int* elidedSampleCoordCount) {
    MergeSampleUsageVisitor visitor(*program.fContext, child, writesToSampleCoords);
    SampleUsage result = visitor.visit(program);
    if (elidedSampleCoordCount) {
        *elidedSampleCoordCount += visitor.elidedSampleCoordCount();
    }
    return result;
}

SK_API bool Analysis::ReferencesBuiltin(const Program& program, int builtin) {
    SkASSERT(program.fUsage);
    for (const auto& [variable, counts] : program.fUsage->fVariableCounts) {
        if (counts.fRead > 0 && variable->layout().fBuiltin == builtin) {
            return true;
        }
    }
    return false;
}

SK_API bool Analysis::ReferencesSampleCoords(const Program& program) {
    // Look for main().
    for (const std::unique_ptr<ProgramElement>& pe : program.fOwnedElements) {
        if (pe->is<FunctionDefinition>()) {
            const FunctionDeclaration& func = pe->as<FunctionDefinition>().declaration();
            if (func.isMain()) {
                // See if main() has a coords parameter that is read from anywhere.
                if (const Variable* coords = func.getMainCoordsParameter()) {
                    ProgramUsage::VariableCounts counts = program.fUsage->get(*coords);
                    return counts.fRead > 0;
                }
            }
        }
    }
    // The program is missing a main().
    return false;
}

SK_API bool Analysis::ReferencesFragCoords(const Program& program) {
    return Analysis::ReferencesBuiltin(program, SK_FRAGCOORD_BUILTIN);
}

SK_API bool Analysis::CallsSampleOutsideMain(const Program& program) {
    SampleOutsideMainVisitor visitor;
    return visitor.visit(program);
}

SK_API bool Analysis::CallsColorTransformIntrinsics(const Program& program) {
    for (auto [symbol, count] : program.usage()->fCallCounts) {
        const FunctionDeclaration& fn = symbol->as<FunctionDeclaration>();
        if (count != 0 && (fn.intrinsicKind() == k_toLinearSrgb_IntrinsicKind ||
                           fn.intrinsicKind() == k_fromLinearSrgb_IntrinsicKind)) {
            return true;
        }
    }
    return false;
}

SK_API bool Analysis::ReturnsOpaqueColor(const FunctionDefinition& function) {
    ReturnsNonOpaqueColorVisitor visitor;
    return !visitor.visitProgramElement(function);
}

SK_API bool Analysis::ContainsRTAdjust(const Expression& expr) {
    class ContainsRTAdjustVisitor : public ProgramVisitor {
    public:
        bool visitExpression(const Expression& expr) override {
            if (expr.is<VariableReference>() &&
                expr.as<VariableReference>().variable()->name() == Compiler::RTADJUST_NAME) {
                return true;
            }
            return INHERITED::visitExpression(expr);
        }

        using INHERITED = ProgramVisitor;
    };

    ContainsRTAdjustVisitor visitor;
    return visitor.visitExpression(expr);
}

SK_API bool Analysis::ContainsVariable(const Expression& expr, const Variable& var) {
    class ContainsVariableVisitor : public ProgramVisitor {
    public:
        ContainsVariableVisitor(const Variable* v) : fVariable(v) {}

        bool visitExpression(const Expression& expr) override {
            if (expr.is<VariableReference>() &&
                expr.as<VariableReference>().variable() == fVariable) {
                return true;
            }
            return INHERITED::visitExpression(expr);
        }

        using INHERITED = ProgramVisitor;
        const Variable* fVariable;
    };

    ContainsVariableVisitor visitor{&var};
    return visitor.visitExpression(expr);
}

SK_API bool Analysis::IsCompileTimeConstant(const Expression& expr) {
    class IsCompileTimeConstantVisitor : public ProgramVisitor {
    public:
        bool visitExpression(const Expression& expr) override {
            switch (expr.kind()) {
                case Expression::Kind::kLiteral:
                    // Literals are compile-time constants.
                    return false;

                case Expression::Kind::kConstructorArray:
                case Expression::Kind::kConstructorCompound:
                case Expression::Kind::kConstructorDiagonalMatrix:
                case Expression::Kind::kConstructorMatrixResize:
                case Expression::Kind::kConstructorSplat:
                case Expression::Kind::kConstructorStruct:
                    // Constructors might be compile-time constants, if they are composed entirely
                    // of literals and constructors. (Casting constructors are intentionally omitted
                    // here. If the value inside was a compile-time constant, we would have not have
                    // generated a cast at all.)
                    return INHERITED::visitExpression(expr);

                default:
                    // This expression isn't a compile-time constant.
                    fIsConstant = false;
                    return true;
            }
        }

        bool fIsConstant = true;
        using INHERITED = ProgramVisitor;
    };

    IsCompileTimeConstantVisitor visitor;
    visitor.visitExpression(expr);
    return visitor.fIsConstant;
}

SK_API bool Analysis::DetectVarDeclarationWithoutScope(const Statement& stmt, ErrorReporter* errors) {
    // A variable declaration can create either a lone VarDeclaration or an unscoped Block
    // containing multiple VarDeclaration statements. We need to detect either case.
    const Variable* var;
    if (stmt.is<VarDeclaration>()) {
        // The single-variable case. No blocks at all.
        var = stmt.as<VarDeclaration>().var();
    } else if (stmt.is<Block>()) {
        // The multiple-variable case: an unscoped, non-empty block...
        const Block& block = stmt.as<Block>();
        if (block.isScope() || block.children().empty()) {
            return false;
        }
        // ... holding a variable declaration.
        const Statement& innerStmt = *block.children().front();
        if (!innerStmt.is<VarDeclaration>()) {
            return false;
        }
        var = innerStmt.as<VarDeclaration>().var();
    } else {
        // This statement wasn't a variable declaration. No problem.
        return false;
    }

    // Report an error.
    SkASSERT(var);
    if (errors) {
        errors->error(var->fPosition,
                      "variable '" + std::string(var->name()) + "' must be created in a scope");
    }
    return true;
}

SK_API int Analysis::NodeCountUpToLimit(const FunctionDefinition& function, int limit) {
    return NodeCountVisitor{limit}.visit(*function.body());
}

SK_API bool Analysis::StatementWritesToVariable(const Statement& stmt, const Variable& var) {
    return VariableWriteVisitor(&var).visit(stmt);
}

SK_API bool Analysis::IsAssignable(Expression& expr, AssignmentInfo* info, ErrorReporter* errors) {
    NoOpErrorReporter unusedErrors;
    return IsAssignableVisitor{errors ? errors : &unusedErrors}.visit(expr, info);
}

SK_API bool Analysis::UpdateVariableRefKind(Expression* expr,
                                     VariableReference::RefKind kind,
                                     ErrorReporter* errors) {
    Analysis::AssignmentInfo info;
    if (!Analysis::IsAssignable(*expr, &info, errors)) {
        return false;
    }
    if (!info.fAssignedVar) {
        if (errors) {
            errors->error(expr->fPosition, "can't assign to expression '" + expr->description() +
                    "'");
        }
        return false;
    }
    info.fAssignedVar->setRefKind(kind);
    return true;
}

template class TProgramVisitor<ProgramVisitorTypes>;
template class TProgramVisitor<ProgramWriterTypes>;

}  // namespace SkSL
