//
// Created by simon on 04.10.21.
//

#ifndef KLEE_ADDEXECUTOR_H
#define KLEE_ADDEXECUTOR_H

#include <klee/Module/KModule.h>
#include <klee/Expr/ArrayCache.h>
#include <klee/Solver/Common.h>
#include <klee/Expr/ArrayExprOptimizer.h>
#include <klee/Module/Cell.h>
#include <klee/Module/KInstruction.h>
#include "../../../lib/Core/FunctionEvaluation.h"
#include "../../../lib/Core/ExternalDispatcher.h"
#include "../../../lib/Core/MemoryManager.h"
#include "../../../lib/Core/TimingSolver.h"
#include "../../../tools/klee-symbolic-optimizer/JsonPrinter.h"

namespace klee {
    class ADDExecutor {
    private:
        JsonPrinter *jsonPrinter;

        std::unique_ptr<KModule> kleeModule;

        ExternalDispatcher *externalDispatcher;
        MemoryManager *memory;
        TimingSolver *solver;

        ArrayCache arrayCache;
        ExprOptimizer optimizer;

        std::map<const llvm::GlobalValue *, MemoryObject *> globalObjects;
        std::map<const llvm::GlobalValue *, ref<ConstantExpr> > globalAddresses;

        time::Span coreSolverTimeout;


    private:
        void initializeGlobals(ExecutionState &state);

        void allocateGlobalObjects(ExecutionState &state);

        void initializeGlobalAliases();

        void initializeGlobalAlias(const llvm::Constant *constant);

        void initializeGlobalObjects(ExecutionState &state);

        void
        initializeGlobalObject(ExecutionState &state, ObjectState *os, const llvm::Constant *constant, unsigned offset);

        void bindModuleConstants();

        void bindInstructionConstants(KInstruction *kInstruction);

        int *getErrnoLocation(const ExecutionState &state) const;

        MemoryObject *addExternalObject(ExecutionState &state, void *addr, unsigned size, bool isReadOnly);

        size_t getAllocationAlignment(const llvm::Value *allocSite) const;

        ref<klee::ConstantExpr> evalConstant(const llvm::Constant *constant, const KInstruction *kInstruction = NULL);

        ref<klee::ConstantExpr> evalConstantExpr(const llvm::ConstantExpr *constantExpr,
                                                 const KInstruction *kInstruction = NULL);

        ObjectState *bindObjectInState(ExecutionState &state, const MemoryObject *memoryObject, bool isLocal,
                                       const Array *array = 0);

        void bindArgument(KFunction *kFunction, unsigned index, ExecutionState &state, ref<Expr> value);

        void bindLocal(KInstruction *target, ExecutionState &state, ref<Expr> value);

        Cell &getArgumentCell(ExecutionState &state, KFunction *kf, unsigned index) {
            return state.stack.back().locals[kf->getArgRegister(index)];
        }

        Cell &getDestCell(ExecutionState &state, KInstruction *target) {
            return state.stack.back().locals[target->dest];
        }

        void stepInstruction(ExecutionState &state);

        template<typename TypeIt>
        void computeOffsets(KGEPInstruction *kgepInstruction, TypeIt iteratorBegin, TypeIt iteratorEnd);

        template<typename SqType, typename TypeIt>
        void computeOffsetsSeqTy(KGEPInstruction *kgepInstruction, ref<ConstantExpr> &constantOffset, uint64_t index,
                                 const TypeIt iterator);

        void
        executeInstruction(ExecutionState &state, KInstruction *kInstruction, FunctionEvaluation *functionEvaluation,
                           KFunction *kFunction, std::vector<llvm::BasicBlock *>::iterator blockInPathIt);

        const Cell &eval(KInstruction *ki, unsigned index, ExecutionState &state) const;

        void executeMakeSymbolic(ExecutionState &state, const MemoryObject *memoryObject, const std::string &name);

        void transferToBasicBlock(llvm::BasicBlock *dst, llvm::BasicBlock *src, ExecutionState &state);

        ref<Expr> toUnique(const ExecutionState &state, ref<Expr> &e);

        void executeMemoryOperation(ExecutionState &state, bool isWrite, ref<Expr> address,
                                    ref<Expr> value /* undef if read */, KInstruction *target /* undef if write */);

        Expr::Width getWidthForLLVMType(llvm::Type *type) const;

        void executeAlloc(ExecutionState &state,
                          ref<Expr> size,
                          bool isLocal,
                          KInstruction *target,
                          bool zeroMemory = false,
                          const ObjectState *reallocFrom = 0,
                          size_t allocationAlignment = 0);

        void createArguments(llvm::Function *function, KFunction *kFunction, ExecutionState *state);

        void runAllocas(KFunction *kFunction, ExecutionState *state);

        void addSymbolicValuesToPath(const ExecutionState &state, FunctionEvaluation *functionEvaluation, Path &path);

    public:
        explicit ADDExecutor(llvm::LLVMContext &context, JsonPrinter *jsonPrinter) {
            this->jsonPrinter = jsonPrinter;

            this->externalDispatcher = new ExternalDispatcher(context);
            this->memory = new MemoryManager(&this->arrayCache);

            this->coreSolverTimeout = time::Span{MaxCoreSolverTime};
            if (this->coreSolverTimeout) UseForkedCoreSolver = true;

            Solver *coreSolver = klee::createCoreSolver(CoreSolverToUse);
            if (!coreSolver) {
                assert(false && "Failed to create core solver");
            }

            Solver *solverChain = constructSolverChain(
                    coreSolver,
                    "klee/solver/all-queries.smt2",
                    "klee/solver/solver-queries.smt2",
                    "klee/solver/all-queries.kquery",
                    "klee/solver/solver-queries.kquery");
            this->solver = new TimingSolver(solverChain, true);
        }

        void runFunction(FunctionEvaluation *functionEvaluation);

        llvm::Module *setModule(std::vector<std::unique_ptr<llvm::Module>> &modules, const Interpreter::ModuleOptions &opts);
    };

}

#endif //KLEE_ADDEXECUTOR_H
