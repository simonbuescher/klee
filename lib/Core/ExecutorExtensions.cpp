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

void Executor::runFunctionAsSymbolic(Function *function) {
    KFunction *kFunction = this->kmodule->functionMap[function];

    std::vector<Path> paths;
    findPaths(function, &paths);

    std::map<std::string, llvm::Type *> variableTypes;
    this->getVariableTypes(function, &variableTypes);

    for (Path path : paths) {
        auto *state = new ExecutionState(kFunction);

        if (this->statsTracker)
            this->statsTracker->framePushed(*state, nullptr);

        this->initializeGlobals(*state);
        this->bindModuleConstants();

        this->processTree = std::make_unique<PTree>(state);


        this->createArguments(function, kFunction, state);
        this->runAllocas(kFunction, state);

        // run path without allocas (if it has any)
        // at branches, fork into two states and pick the one on our path as the new current state
        for (auto blockInPathIt = path.begin(); blockInPathIt != path.end(); blockInPathIt++) {
            if ((blockInPathIt + 1) == path.end() && !path.shouldExecuteFinishBlock()) {
                break;
            }

            llvm::BasicBlock *block = *blockInPathIt;
            // todo sbuescher what if path start is phi block?
            this->transferToBasicBlock(block, nullptr, *state);

            for (llvm::Instruction &instruction : *block) {
                KInstruction *ki = state->pc;
                this->stepInstruction(*state);

                // todo sbuescher other block terminators
                switch (instruction.getOpcode()) {
                    case Instruction::Alloca:
                        break;
                    case Instruction::Br: {
                        auto &branchInstruction = cast<BranchInst>(instruction);

                        BasicBlock *successorInPath = *(blockInPathIt + 1);

                        if (!branchInstruction.isUnconditional()) {
                            ref<Expr> condition = this->eval(ki, 0, *state).value;
                            condition = this->optimizer.optimizeExpr(condition, false);

                            if (branchInstruction.getSuccessor(0) == successorInPath) {
                                state->constraints.push_back(condition);
                            }
                            if (branchInstruction.getSuccessor(1) == successorInPath) {
                                state->constraints.push_back(Expr::createIsZero(condition));
                            }
                        }

                        // todo sbuescher do we need this?
                        this->transferToBasicBlock(successorInPath, *blockInPathIt, *state);

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
                        auto *switchInstruction = cast<SwitchInst>(&instruction);

                        BasicBlock *successorInPath = *(blockInPathIt + 1);

                        ref<Expr> switchExpression = this->eval(ki, 0, *state).value;
                        switchExpression = this->toUnique(*state, switchExpression);

                        ref<Expr> defaultCondition;

                        std::vector<ref<Expr>> targets;
                        for (auto switchCase : switchInstruction->cases()) {
                            ref<Expr> caseExpression = this->evalConstant(switchCase.getCaseValue());
                            BasicBlock *caseSuccessor = switchCase.getCaseSuccessor();

                            ref<Expr> caseCondition = EqExpr::create(switchExpression, caseExpression);
                            caseCondition = this->optimizer.optimizeExpr(caseCondition, false);

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
                        branchCondition = this->optimizer.optimizeExpr(branchCondition, false);

                        state->constraints.push_back(branchCondition);

                        // todo sbuescher do we need this?
                        this->transferToBasicBlock(successorInPath, block, *state);

                        break;
                    }
                    case Instruction::Unreachable:
                        assert(false && "unreachable instruction reached");
                        break;
                    default:
                        this->executeInstruction(*state, ki);
                        break;
                }
            }
        }

        path.setConstraints(state->constraints);

        // todo sbuescher move addSymbolicVariable into function
        std::vector<ref<Expr>> symbolicValues;
        this->getSymbolicValues(*state, &variableTypes, symbolicValues);
        for (unsigned long i = 0; i < symbolicValues.size(); i++) {
            std::string valueName = "%" + std::to_string(i + kFunction->numArgs);
            path.addSymbolicValue(valueName, symbolicValues[i]);
        }

        this->interpreterHandler->processPathExecution(path);


        // copied from run method
        this->processTree = nullptr;

        // hack to clear memory objects
        delete this->memory;
        this->memory = new MemoryManager(nullptr);

        this->globalObjects.clear();
        this->globalAddresses.clear();

        if (this->statsTracker)
            this->statsTracker->done();
    }
}

void Executor::getSymbolicValues(const ExecutionState &state, std::map<std::string, llvm::Type *> *variableTypes,
                                 std::vector<ref<Expr>> &results) {
    // we only have one stack frame because we do not allow subroutine calls
    StackFrame stackFrame = state.stack.back();

    for (const MemoryObject *memoryObject : stackFrame.allocas) {
        std::string variableName = memoryObject->name;
        llvm::Type *variableType = (*variableTypes)[variableName];

        ObjectPair objectPair;
        state.addressSpace.resolveOne(memoryObject->getBaseExpr(), objectPair);

        ref<Expr> offset = memoryObject->getOffsetExpr(memoryObject->getBaseExpr());
        ref<Expr> result = objectPair.second->read(offset, this->getWidthForLLVMType(variableType));

        results.push_back(result);
    }
}

void Executor::getVariableTypes(llvm::Function *function, std::map<std::string, llvm::Type *> *variableTypes) {
    int argumentNumber = 0;
    for (llvm::Argument &argument : function->args()) {
        (*variableTypes)["arg" + itostr(argumentNumber)] = argument.getType();

        argumentNumber++;
    }

    int variableNumber = 0;
    for (llvm::BasicBlock &basicBlock : *function) {
        for (llvm::Instruction &instruction : basicBlock) {
            if (instruction.getOpcode() == Instruction::Alloca) {
                (*variableTypes)["var" + itostr(variableNumber)] = instruction.getType();

                variableNumber++;
            }
        }
    }
}

void Executor::runAllocas(const KFunction *kFunction, ExecutionState *state) {
    int variableNumber = 0;

    for (unsigned int i = 0; i < kFunction->numInstructions; i++) {
        KInstruction *ki = kFunction->instructions[i];

        if (ki->inst->getOpcode() == Instruction::Alloca) {
            executeInstruction(*state, ki);

            std::string variableName = "var" + itostr(variableNumber);

            const MemoryObject *memoryObject = state->stack.back().allocas.back();
            memoryObject->setName(variableName);

            executeMakeSymbolic(*state, memoryObject, variableName);

            variableNumber++;
        }
    }
}

void Executor::createArguments(Function *f, KFunction *kFunction, ExecutionState *state) {
    // create symbolic arguments for state
    Instruction *firstInstruction = &*(f->begin()->begin());

    int currentArgumentNumber = 0;
    for (Function::arg_iterator argument = f->arg_begin(), ae = f->arg_end();
         argument != ae; argument++, currentArgumentNumber++) {
        // creating symbolic arguments

        llvm::Type *argumentType = argument->getType();
        std::string argumentName = "arg" + itostr(currentArgumentNumber);

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
