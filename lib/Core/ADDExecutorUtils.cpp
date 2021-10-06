//
// Created by simon on 04.10.21.
//

#include <llvm/IR/Instructions.h>

#include <klee/Module/KInstruction.h>
#include <klee/Support/ErrorHandling.h>
#include <klee/Support/ModuleUtil.h>

#include "ADDExecutor.h"
#include "ExecutionState.h"


namespace klee {

    int *ADDExecutor::getErrnoLocation(const ExecutionState &state) const {
#if !defined(__APPLE__) && !defined(__FreeBSD__)
        /* From /usr/include/errno.h: it [errno] is a per-thread variable. */
        return __errno_location();
#else
        return __error();
#endif
    }

    size_t ADDExecutor::getAllocationAlignment(const llvm::Value *allocSite) const {
        const size_t forcedAlignment = 8;
        size_t alignment = 0;
        llvm::Type *type = nullptr;
        std::string allocationSiteName(allocSite->getName().str());

        if (const auto *globalObject = dyn_cast<llvm::GlobalObject>(allocSite)) {
            alignment = globalObject->getAlignment();
            if (const auto *globalVar = dyn_cast<llvm::GlobalVariable>(globalObject)) {
                auto *ptrType = dyn_cast<llvm::PointerType>(globalVar->getType());
                assert(ptrType && "globalVar's type is not a pointer");
                type = ptrType->getElementType();
            } else {
                type = globalObject->getType();
            }

        } else if (const auto *allocaInst = dyn_cast<llvm::AllocaInst>(allocSite)) {
            alignment = allocaInst->getAlignment();
            type = allocaInst->getAllocatedType();

        } else if (isa<llvm::InvokeInst>(allocSite) || isa<llvm::CallInst>(allocSite)) {
            // FIXME: Model the semantics of the call to use the right alignment
#if LLVM_VERSION_CODE >= LLVM_VERSION(8, 0)
            const auto &callBase = cast<llvm::CallBase>(*allocSite);
#else
            llvm::Value *allocSiteNonConst = const_cast<llvm::Value *>(allocSite);
    const CallSite callBase(isa<InvokeInst>(allocSiteNonConst)
                          ? CallSite(cast<InvokeInst>(allocSiteNonConst))
                          : CallSite(cast<CallInst>(allocSiteNonConst)));
#endif
            llvm::Function *function = klee::getDirectCallTarget(callBase, true);
            if (function)
                allocationSiteName = function->getName().str();

            klee_warning_once(function != nullptr ? function : allocSite,
                              "Alignment of memory from call \"%s\" is not "
                              "modelled. Using alignment of %zu.",
                              allocationSiteName.c_str(), forcedAlignment);
            alignment = forcedAlignment;

        } else {
            llvm_unreachable("Unhandled allocation site");
        }

        if (alignment == 0) {
            assert(type != nullptr);

            if (type->isSized()) {
                alignment = this->kleeModule->targetData->getPrefTypeAlignment(type);

            } else {
                klee_warning_once(allocSite, "Cannot determine memory alignment for "
                                             "\"%s\". Using alignment of %zu.",
                                  allocationSiteName.c_str(), forcedAlignment);
                alignment = forcedAlignment;
            }
        }

        // Currently we require alignment be a power of 2
        if (!bits64::isPowerOfTwo(alignment)) {
            klee_warning_once(allocSite, "Alignment of %zu requested for %s but this "
                                         "not supported. Using alignment of %zu",
                              alignment, allocSite->getName().str().c_str(),
                              forcedAlignment);
            alignment = forcedAlignment;
        }
        assert(bits64::isPowerOfTwo(alignment) && "Returned alignment must be a power of two");

        return alignment;
    }

    ref<Expr> ADDExecutor::toUnique(const ExecutionState &state, ref<Expr> &expression) {
        ref<Expr> result = expression;

        if (!isa<ConstantExpr>(expression)) {
            ref<ConstantExpr> value;
            bool isTrue = false;
            expression = this->optimizer.optimizeExpr(expression, true);
            this->solver->setTimeout(this->coreSolverTimeout);
            if (this->solver->getValue(state.constraints, expression, value, state.queryMetaData)) {
                ref<Expr> cond = EqExpr::create(expression, value);
                cond = this->optimizer.optimizeExpr(cond, false);
                if (this->solver->mustBeTrue(state.constraints, cond, isTrue,
                                             state.queryMetaData) &&
                    isTrue)
                    result = value;
            }
            this->solver->setTimeout(time::Span());
        }

        return result;
    }

    Expr::Width ADDExecutor::getWidthForLLVMType(llvm::Type *type) const {
        return this->kleeModule->targetData->getTypeSizeInBits(type);
    }
}
