//
// Created by simon on 06.10.21.
//

#ifndef KLEE_ADDEXECUTOR_H
#define KLEE_ADDEXECUTOR_H

#include <klee/Core/ADDInterpreter.h>
#include <klee/Expr/ArrayCache.h>
#include <klee/Expr/ArrayExprOptimizer.h>
#include <klee/Module/KModule.h>
#include <klee/Solver/Common.h>

#include "ExecutionState.h"
#include "ExternalDispatcher.h"
#include "klee/Core/FunctionEvaluation.h"
#include "MemoryManager.h"
#include "klee/Core/Path.h"
#include "TimingSolver.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace klee {
    class ADDExecutor : public ADDInterpreter {
    private:
        std::unique_ptr<KModule> kleeModule;

        ExternalDispatcher *externalDispatcher;
        MemoryManager *memory;
        TimingSolver *solver;

        ArrayCache arrayCache;
        ExprOptimizer optimizer;

        std::map<const llvm::GlobalValue *, MemoryObject *> globalObjects;
        std::map<const llvm::GlobalValue *, ref<ConstantExpr> > globalAddresses;

        time::Span coreSolverTimeout;

    public:
        explicit ADDExecutor(llvm::LLVMContext &context);

        void runFunction(FunctionEvaluation *functionEvaluation) override;

        llvm::Module *setModule(
                std::vector<std::unique_ptr<llvm::Module>> &modules,
                const Interpreter::ModuleOptions &opts
        ) override;

    private:
        // initializations

        void initializeGlobals(ExecutionState &state);

        void allocateGlobalObjects(ExecutionState &state);

        void initializeGlobalAliases();

        void initializeGlobalAlias(const llvm::Constant *constant);

        void initializeGlobalObjects(ExecutionState &state);

        void initializeGlobalObject(
                ExecutionState &state,
                ObjectState *os,
                const llvm::Constant *constant,
                unsigned offset
        );

        void bindModuleConstants();

        void bindInstructionConstants(KInstruction *kInstruction);

        MemoryObject *addExternalObject(ExecutionState &state, void *address, unsigned size, bool isReadOnly);


        // utils

        int *getErrnoLocation(const ExecutionState &state) const;

        size_t getAllocationAlignment(const llvm::Value *allocSite) const;

        ref<Expr> toUnique(const ExecutionState &state, ref<Expr> &expression);

        Expr::Width getWidthForLLVMType(llvm::Type *type) const;

        template<typename TypeIt>
        void computeOffsets(KGEPInstruction *kgepInstruction, TypeIt iteratorBegin, TypeIt iteratorEnd);

        template<typename SqType, typename TypeIt>
        void computeOffsetsSeqTy(
                KGEPInstruction *kgepInstruction,
                ref<ConstantExpr> &constantOffset,
                uint64_t index,
                TypeIt iterator
        );


        // execution

        void createArguments(llvm::Function *function, KFunction *kFunction, ExecutionState *state);

        void runAllocas(KFunction *kFunction, ExecutionState *state);

        void bindArgument(KFunction *kFunction, unsigned index, ExecutionState &state, ref<Expr> value);

        void bindLocal(KInstruction *target, ExecutionState &state, ref<Expr> value);

        ObjectState *bindObjectInState(
                ExecutionState &state,
                const MemoryObject *memoryObject,
                bool isLocal,
                const Array *array = nullptr
        );

        Cell &getArgumentCell(ExecutionState &state, KFunction *kf, unsigned index);

        Cell &getDestCell(ExecutionState &state, KInstruction *target);

        const Cell &eval(KInstruction *ki, unsigned index, ExecutionState &state) const;

        ref<klee::ConstantExpr> evalConstant(
                const llvm::Constant *constant,
                const KInstruction *kInstruction = NULL
        );

        ref<klee::ConstantExpr> evalConstantExpr(
                const llvm::ConstantExpr *constantExpr,
                const KInstruction *kInstruction = NULL
        );

        void stepInstruction(ExecutionState &state);

        void executeInstruction(
                ExecutionState &state,
                KInstruction *kInstruction,
                FunctionEvaluation *functionEvaluation,
                KFunction *kFunction,
                std::vector<llvm::BasicBlock *>::iterator blockInPathIt
        );

        void executeAlloc(
                ExecutionState &state,
                ref<Expr> size,
                bool isLocal,
                KInstruction *target,
                bool zeroMemory = false,
                const ObjectState *reallocFrom = nullptr,
                size_t allocationAlignment = 0
        );

        void executeMemoryOperation(
                ExecutionState &state,
                bool isWrite,
                ref<Expr> address,
                ref<Expr> value,
                KInstruction *target
        );

        void executeMakeSymbolic(ExecutionState &state, const MemoryObject *memoryObject, const std::string &name);

        void transferToBasicBlock(llvm::BasicBlock *dst, llvm::BasicBlock *src, ExecutionState &state);

        void addSymbolicValuesToPath(const ExecutionState &state, FunctionEvaluation *functionEvaluation, Path &path);
    };
}


#endif //KLEE_ADDEXECUTOR_H
