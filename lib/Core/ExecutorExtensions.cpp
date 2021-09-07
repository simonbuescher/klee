//
// Created by simon on 07.09.21.
//

#include "Executor.h"
#include "MemoryManager.h"
#include "PTree.h"
#include "StatsTracker.h"

#include <llvm/IR/Instructions.h>
#include <llvm/IR/DataLayout.h>



using namespace klee;
using namespace llvm;

void Executor::runFunctionAsSymbolic(Function *f) {
    KFunction *kFunction = kmodule->functionMap[f];

    std::vector<Path> paths;
    findPaths(f, &paths);

    for (Path path : paths) {
        ExecutionState *state = new ExecutionState(kFunction);

        if (statsTracker)
            statsTracker->framePushed(*state, nullptr);

        initializeGlobals(*state);
        bindModuleConstants();

        processTree = std::make_unique<PTree>(state);

        std::map<std::string, llvm::Type *> variableTypes;

        createArguments(f, kFunction, state, &variableTypes);
        runAllocas(kFunction, state);

        // run path without allocas (if it has any)
        // at branches, fork into two states and pick the one on our path as the new current state
        for (auto blockInPathIt = path.begin(); blockInPathIt != path.end(); blockInPathIt++) {
            if ((blockInPathIt + 1) == path.end() && !path.shouldExecuteFinishBlock()) {
                break;
            }

            llvm::BasicBlock *block = *blockInPathIt;
            // todo sbuescher what if path start is phi block?
            transferToBasicBlock(block, nullptr, *state);

            for (llvm::Instruction &instruction : *block) {
                KInstruction *ki = state->pc;
                stepInstruction(*state);

                // todo sbuescher other block terminators
                switch (instruction.getOpcode()) {
                    case Instruction::Alloca:
                        break;
                    case Instruction::Br: {
                        BranchInst &branchInstruction = cast<BranchInst>(instruction);

                        BasicBlock *successorInPath = *(blockInPathIt + 1);

                        if (!branchInstruction.isUnconditional()) {
                            ref<Expr> condition = eval(ki, 0, *state).value;
                            condition = optimizer.optimizeExpr(condition, false);

                            if (branchInstruction.getSuccessor(0) == successorInPath) {
                                state->constraints.push_back(condition);
                            }
                            if (branchInstruction.getSuccessor(1) == successorInPath) {
                                state->constraints.push_back(Expr::createIsZero(condition));
                            }
                        }

                        transferToBasicBlock(successorInPath, *blockInPathIt, *state);

                        break;
                    }
                    case Instruction::Invoke:
                    case Instruction::Call: {
                        assert(false && "calls are currently not supported");
                        break;
                    }
                    case Instruction::Ret:
                        // no return needed as we just run our own function
                        break;
                    case Instruction::Switch: {
                        SwitchInst *switchInstruction = cast<SwitchInst>(&instruction);

                        BasicBlock *successorInPath = *(blockInPathIt + 1);

                        ref<Expr> switchExpression = eval(ki, 0, *state).value;
                        switchExpression = toUnique(*state, switchExpression);

                        ref<Expr> defaultCondition;

                        std::vector<ref<Expr>> targets;
                        for (auto switchCase : switchInstruction->cases()) {
                            ref<Expr> caseExpression = evalConstant(switchCase.getCaseValue());
                            BasicBlock *caseSuccessor = switchCase.getCaseSuccessor();

                            ref<Expr> caseCondition = EqExpr::create(switchExpression, caseExpression);
                            caseCondition = optimizer.optimizeExpr(caseCondition, false);

                            ref<Expr> notCaseCondition = Expr::createIsZero(caseCondition);
                            if (!defaultCondition) {
                                defaultCondition = notCaseCondition;
                            } else {
                                defaultCondition = AndExpr::create(defaultCondition, notCaseCondition);
                            }

                            if (caseSuccessor != successorInPath) {
                                continue;
                            }

                            targets.push_back(caseCondition);
                        }


                        ref<Expr> branchCondition;
                        if (targets.empty()) {
                            branchCondition = defaultCondition;
                        } else {
                            branchCondition = targets.front();
                            for (auto exprIt = ++targets.begin(); exprIt != targets.end(); exprIt++) {
                                ref<Expr> expr = *exprIt;
                                branchCondition = OrExpr::create(branchCondition, expr);
                            }
                        }
                        branchCondition = optimizer.optimizeExpr(branchCondition, false);

                        state->constraints.push_back(branchCondition);

                        transferToBasicBlock(successorInPath, block, *state);

                        break;
                    }
                    case Instruction::Unreachable:
                        assert(false && "unreachable instruction reached");
                        break;
                    default:
                        executeInstruction(*state, ki);
                        break;
                }
            }
        }

        path.setConstraints(state->constraints);

        std::vector<ref<Expr>> symbolicValues;
        getReturnValues(*state, symbolicValues);
        for (unsigned long i = 0; i < symbolicValues.size(); i++) {
            std::string valueName = "%" + std::to_string(i + kFunction->numArgs);
            path.addSymbolicValue(valueName, symbolicValues[i]);
        }

        interpreterHandler->processPathExecution(path);


        // copied from run method
        processTree = nullptr;

        // hack to clear memory objects
        delete memory;
        memory = new MemoryManager(NULL);

        globalObjects.clear();
        globalAddresses.clear();

        if (statsTracker)
            statsTracker->done();
    }
}

void Executor::getReturnValues(const ExecutionState &state, std::vector<ref<Expr>> &results) {
    StackFrame stackFrame = state.stack.back();

    for (const MemoryObject *memoryObject : stackFrame.allocas) {

        ObjectPair objectPair;
        state.addressSpace.resolveOne(memoryObject->getBaseExpr(), objectPair);

        unsigned exprWidth = Expr::Int32;
        unsigned sizeWidth = memoryObject->size;
        unsigned otherWidth = memoryObject->getSizeExpr()->getZExtValue();

        ref<Expr> offset = memoryObject->getOffsetExpr(memoryObject->getBaseExpr());
        ref<Expr> result = objectPair.second->read(offset, Expr::Int32);

        results.push_back(result);
    }
}

void Executor::runAllocas(const KFunction *kFunction, ExecutionState *state) {
    // run all allocas in function
    for (unsigned int i = 0; i < kFunction->numInstructions; i++) {
        KInstruction *ki = kFunction->instructions[i];
        if (ki->inst->getOpcode() == Instruction::Alloca) {
            executeInstruction(*state, ki);

            std::string destName = "%" + itostr(ki->dest);
            const MemoryObject *memoryObject = state->stack.back().allocas.back();
            executeMakeSymbolic(*state, memoryObject, destName);
        }
    }
}

void Executor::createArguments(Function *f, KFunction *kFunction, ExecutionState *state,
                               std::map<std::string, llvm::Type *> *variableTypes) {
    // create symbolic arguments for state
    Instruction *firstInstruction = &*(f->begin()->begin());

    int currentArgumentNumber = 0;
    for (Function::arg_iterator argument = f->arg_begin(), ae = f->arg_end();
         argument != ae; argument++, currentArgumentNumber++) {
        // creating symbolic arguments

        llvm::Type *argumentType = argument->getType();
        std::string argumentName = "arg" + itostr(currentArgumentNumber);

        // save argument type
        (*variableTypes)[argumentName] = argumentType;


        // todo sbuescher support array arguments?? which size
        unsigned argumentSizeBytes = kmodule->targetData->getTypeAllocSize(argumentType);
        unsigned argumentSizeBits = kmodule->targetData->getTypeAllocSizeInBits(argumentType);
        ref<ConstantExpr> size = ConstantExpr::createPointer(argumentSizeBytes);

        MemoryObject *memoryObject = memory->allocate(size->getZExtValue(), false, false, firstInstruction, 4);
        memoryObject->setName(argumentName);

        executeMakeSymbolic(*state, memoryObject, argumentName);

        // loading value of symbolic arguments into locals
        bool success;
        ObjectPair objectPair;
        state->addressSpace.resolveOne(*state, solver, memoryObject->getBaseExpr(), objectPair, success);

        ref<Expr> offset = memoryObject->getOffsetExpr(memoryObject->getBaseExpr());
        ref<Expr> result = objectPair.second->read(offset, argumentSizeBits);

        // bind argument to function
        bindArgument(kFunction, currentArgumentNumber, *state, result);
    }
}
