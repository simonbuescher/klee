//
// Created by simon on 04.10.21.
//

#include <klee/Core/ADDInterpreter.h>

#include <klee/Module/Cell.h>
#include <klee/Module/KInstruction.h>
#include <klee/Support/ModuleUtil.h>
#include <llvm/Support/Path.h>
#include <klee/Support/ErrorHandling.h>
#include <llvm/IR/Instructions.h>
#include <iostream>
#include "ADDExecutor.h"
#include "SpecialFunctionHandler.h"
#include "klee/Core/Path.h"
#include "ExecutionState.h"
#include "klee/Core/FunctionEvaluation.h"
#include "GetElementPtrTypeIterator.h"
// #include <llvm/IR/GetElementPtrTypeIterator.h>


namespace klee {

    ADDInterpreter *ADDInterpreter::create(llvm::LLVMContext &context) {
        return new ADDExecutor(context);
    }

    ADDExecutor::ADDExecutor(llvm::LLVMContext &context) {
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

    void ADDExecutor::runFunction(FunctionEvaluation *functionEvaluation) {
        llvm::Function *function = functionEvaluation->getFunction();
        KFunction *kFunction = this->kleeModule->functionMap[function];

        for (Path *path : functionEvaluation->getPathList()) {
            auto *state = new ExecutionState(kFunction);

            this->initializeGlobals(*state);
            this->bindModuleConstants();

            this->createArguments(function, kFunction, state);
            this->runAllocas(kFunction, state);

            // run path without allocas (if it has any)
            // at branches, fork into two states and pick the one on our path as the new current state
            for (auto blockInPathIt = path->begin(); blockInPathIt != path->end(); blockInPathIt++) {
                if ((blockInPathIt + 1) == path->end() && !path->shouldExecuteFinishBlock()) {
                    break;
                }

                llvm::BasicBlock *block = *blockInPathIt;
                // todo sbuescher what if path start is phi block?
                this->transferToBasicBlock(block, nullptr, *state);

                for (llvm::Instruction &instruction : *block) {
                    KInstruction *ki = state->pc;
                    this->stepInstruction(*state);

                    this->executeInstruction(*state, ki, functionEvaluation, kFunction, blockInPathIt);
                }
            }

            path->setConstraints(state->constraints);
            this->addSymbolicValuesToPath(*state, functionEvaluation, path);

            std::cout << "PATH FINISHED: ["
                      << path->getPathRepr()
                      << "] ["
                      << (path->shouldExecuteFinishBlock() ? "execute last" : "dont execute last")
                      << "]"
                      << std::endl;

            // hack to clear memory objects
            delete this->memory;
            this->memory = new MemoryManager(&this->arrayCache);

            this->globalObjects.clear();
            this->globalAddresses.clear();
        }
    }

    llvm::Module *ADDExecutor::setModule(std::vector<std::unique_ptr<llvm::Module>> &modules,
                                         const Interpreter::ModuleOptions &opts) {
        this->kleeModule = std::unique_ptr<KModule>(new KModule());

        llvm::SmallString<128> libPath(opts.LibraryDir);
        llvm::sys::path::append(libPath, "libkleeRuntimeIntrinsic" + opts.OptSuffix + ".bca");

        std::string error;
        if (!klee::loadFile(libPath.c_str(), modules[0]->getContext(), modules, error)) {
            klee_error("Could not load KLEE intrinsic file %s", libPath.c_str());
        }

        while (this->kleeModule->link(modules, opts.EntryPoint)) {
            this->kleeModule->instrument(opts);
        }

        std::vector<const char *> preservedFunctions;
        preservedFunctions.push_back(opts.EntryPoint.c_str());
        preservedFunctions.push_back("memset");
        preservedFunctions.push_back("memcpy");
        preservedFunctions.push_back("memcmp");
        preservedFunctions.push_back("memmove");
        this->kleeModule->optimiseAndPrepare(opts, preservedFunctions);
        this->kleeModule->checkModule();

        this->kleeModule->manifest(nullptr, false);

        // Initialize the context.
        llvm::DataLayout *dataLayout = this->kleeModule->targetData.get();
        Context::initialize(dataLayout->isLittleEndian(), (Expr::Width) dataLayout->getPointerSizeInBits());

        return this->kleeModule->module.get();
    }


    // execution

    void ADDExecutor::createArguments(llvm::Function *f, KFunction *kFunction, ExecutionState *state) {
        // create symbolic arguments for state
        llvm::Instruction *firstInstruction = &*(f->begin()->begin());

        int currentArgumentNumber = 0;
        for (llvm::Function::arg_iterator argument = f->arg_begin(), ae = f->arg_end();
             argument != ae; argument++, currentArgumentNumber++) {
            // creating symbolic arguments

            llvm::Type *argumentType = argument->getType();
            std::string argumentName = "arg" + llvm::itostr(currentArgumentNumber);

            // todo sbuescher support array arguments?? which size
            unsigned argumentSizeBytes = this->kleeModule->targetData->getTypeAllocSize(argumentType);
            unsigned argumentSizeBits = this->kleeModule->targetData->getTypeAllocSizeInBits(argumentType);
            ref<ConstantExpr> size = ConstantExpr::createPointer(argumentSizeBytes);

            MemoryObject *memoryObject = this->memory->allocate(size->getZExtValue(), false, false, firstInstruction,
                                                                4);
            memoryObject->setName(argumentName);

            executeMakeSymbolic(*state, memoryObject, argumentName);

            // loading value of symbolic arguments into locals
            bool success;
            ObjectPair objectPair;
            state->addressSpace.resolveOne(*state, this->solver, memoryObject->getBaseExpr(), objectPair, success);

            ref<Expr> offset = memoryObject->getOffsetExpr(memoryObject->getBaseExpr());
            ref<Expr> result = objectPair.second->read(offset, argumentSizeBits);

            // bind argument to function
            this->bindArgument(kFunction, currentArgumentNumber, *state, result);
        }
    }


    void ADDExecutor::runAllocas(KFunction *kFunction, ExecutionState *state) {
        int variableNumber = 0;

        for (unsigned int i = 0; i < kFunction->numInstructions; i++) {
            KInstruction *ki = kFunction->instructions[i];

            if (ki->inst->getOpcode() == llvm::Instruction::Alloca) {
                auto *allocaInst = cast<llvm::AllocaInst>(ki->inst);

                llvm::Type *type = allocaInst->getAllocatedType();
                unsigned count = 1;

                if (type->isArrayTy()) {
                    count = type->getArrayNumElements();
                    type = type->getArrayElementType();
                }

                unsigned elementSize = this->kleeModule->targetData->getTypeStoreSize(type);
                ref<Expr> elementSizeExpr = Expr::createPointer(elementSize);
                ref<Expr> countExpr = Expr::createPointer(count);

                ref<Expr> size = MulExpr::create(elementSizeExpr, countExpr);
                // ConstantExpr::create(count, Expr::Int32);
                // this does not work like expected, but array allocations still work over the ArrayTy type
                // if (allocaInst->isArrayAllocation()) {
                //     ref<Expr> count = this->eval(ki, 0, *state).value;
                //     count = Expr::createZExtToPointerWidth(count);
                //     size = MulExpr::create(size, count);
                // }

                this->executeAlloc(*state, size, true, ki);

                std::string variableName = "var" + llvm::itostr(variableNumber);

                const MemoryObject *memoryObject = state->stack.back().allocas.back();
                memoryObject->setName(variableName);

                this->executeMakeSymbolic(*state, memoryObject, variableName);

                variableNumber++;
            }
        }
    }

    void ADDExecutor::bindArgument(KFunction *kFunction, unsigned index, ExecutionState &state, ref<Expr> value) {
        this->getArgumentCell(state, kFunction, index).value = value;
    }

    void ADDExecutor::bindLocal(KInstruction *target, ExecutionState &state,
                                ref<Expr> value) {
        this->getDestCell(state, target).value = value;
    }

    ObjectState *ADDExecutor::bindObjectInState(ExecutionState &state,
                                                const MemoryObject *memoryObject,
                                                bool isLocal,
                                                const Array *array) {
        ObjectState *objectState = array ? new ObjectState(memoryObject, array) : new ObjectState(memoryObject);
        state.addressSpace.bindObject(memoryObject, objectState);

        // Its possible that multiple bindings of the same memoryObject in the state
        // will put multiple copies on this list, but it doesn't really
        // matter because all we use this list for is to unbind the object
        // on function return.
        if (isLocal)
            state.stack.back().allocas.push_back(memoryObject);

        return objectState;
    }

    Cell &ADDExecutor::getArgumentCell(ExecutionState &state, KFunction *kf, unsigned index) {
        return state.stack.back().locals[kf->getArgRegister(index)];
    }

    Cell &ADDExecutor::getDestCell(ExecutionState &state, KInstruction *target) {
        return state.stack.back().locals[target->dest];
    }

    const Cell &ADDExecutor::eval(KInstruction *ki, unsigned operatorIndex, ExecutionState &state) const {
        assert(operatorIndex < ki->inst->getNumOperands());
        int varNumber = ki->operands[operatorIndex];

        assert(varNumber != -1 && "Invalid operand to eval(), not a value or constant!");

        // Determine if this is a constant or not.
        if (varNumber < 0) {
            unsigned index = -varNumber - 2;
            return this->kleeModule->constantTable[index];
        } else {
            unsigned index = varNumber;
            StackFrame &sf = state.stack.back();
            return sf.locals[index];
        }
    }

    void ADDExecutor::stepInstruction(ExecutionState &state) {
        ++state.steppedInstructions;
        state.prevPC = state.pc;
        ++state.pc;
    }

    void ADDExecutor::executeAlloc(ExecutionState &state,
                                   ref<Expr> size,
                                   bool isLocal,
                                   KInstruction *target,
                                   bool zeroMemory,
                                   const ObjectState *reallocFrom,
                                   size_t allocationAlignment) {
        size = this->toUnique(state, size);

        if (auto *constantExpr = dyn_cast<ConstantExpr>(size)) {
            const llvm::Value *allocSite = state.prevPC->inst;

            if (allocationAlignment == 0) {
                allocationAlignment = this->getAllocationAlignment(allocSite);
            }

            MemoryObject *memoryObject = this->memory->allocate(constantExpr->getZExtValue(), isLocal, false, allocSite,
                                                                allocationAlignment);
            if (!memoryObject) {
                this->bindLocal(target, state, ConstantExpr::alloc(0, Context::get().getPointerWidth()));

            } else {
                ObjectState *objectState = this->bindObjectInState(state, memoryObject, isLocal);

                if (zeroMemory) {
                    objectState->initializeToZero();
                } else {
                    objectState->initializeToRandom();
                }

                this->bindLocal(target, state, memoryObject->getBaseExpr());

                if (reallocFrom) {
                    unsigned count = std::min(reallocFrom->size, objectState->size);
                    for (unsigned i = 0; i < count; i++)
                        objectState->write(i, reallocFrom->read8(i));
                    state.addressSpace.unbindObject(reallocFrom->getObject());
                }
            }

        } else {
            assert(false && "alloca of symbolic size not implemented");
        }
    }

    void ADDExecutor::executeMemoryOperation(ExecutionState &state,
                                             bool isWrite,
                                             ref<Expr> address,
                                             ref<Expr> value /* undef if read */,
                                             KInstruction *target /* undef if write */) {
        Expr::Width type = (isWrite ? value->getWidth() : getWidthForLLVMType(target->inst->getType()));
        unsigned bytes = Expr::getMinBytesForWidth(type);

        address = this->optimizer.optimizeExpr(address, true);

        // fast path: single in-bounds resolution
        bool success;
        ObjectPair objectPair;

        this->solver->setTimeout(this->coreSolverTimeout);
        if (!state.addressSpace.resolveOne(state, this->solver, address, objectPair, success)) {
            assert(false && "resolveOne failure");
        }
        solver->setTimeout(time::Span());

        if (success) {
            const MemoryObject *memoryObject = objectPair.first;

            ref<Expr> offset = memoryObject->getOffsetExpr(address);
            ref<Expr> check = memoryObject->getBoundsCheckOffset(offset, bytes);
            check = this->optimizer.optimizeExpr(check, true);

            bool inBounds;
            this->solver->setTimeout(coreSolverTimeout);
            bool success = solver->mustBeTrue(state.constraints, check, inBounds, state.queryMetaData);
            this->solver->setTimeout(time::Span());

            if (!success) {
                state.pc = state.prevPC;
                assert(false && "Query timed out (bounds check).");
            }

            if (inBounds) {
                const ObjectState *os = objectPair.second;
                if (isWrite) {
                    if (os->readOnly) {
                        assert(false && "memory error: write on read only object");
                    } else {
                        ObjectState *wos = state.addressSpace.getWriteable(memoryObject, os);
                        wos->write(offset, value);
                    }
                } else {
                    ref<Expr> result = os->read(offset, type);
                    this->bindLocal(target, state, result);
                }

                return;
            }
        }

        assert(false && "symbolic index lookup in memory operation is not allowed");

        address = this->optimizer.optimizeExpr(address, true);

        ResolutionList resolutionList;

        this->solver->setTimeout(this->coreSolverTimeout);
        state.addressSpace.resolve(state, this->solver, address, resolutionList, 0, this->coreSolverTimeout);
        this->solver->setTimeout(time::Span());

        for (auto & i : resolutionList) {
            const MemoryObject *mo = i.first;
            const ObjectState *os = i.second;
            ref<Expr> inBounds = mo->getBoundsCheckPointer(address, bytes);

            state.constraints.push_back(inBounds);
            if (isWrite) {
                if (os->readOnly) {
                    assert(false && "memory error: object read only");
                } else {
                    ObjectState *wos = state.addressSpace.getWriteable(mo, os);
                    wos->write(mo->getOffsetExpr(address), value);
                }
            } else {
                ref<Expr> result = os->read(mo->getOffsetExpr(address), type);
                bindLocal(target, state, result);
            }
        }
    }

    void ADDExecutor::executeMakeSymbolic(ExecutionState &state,
                                          const MemoryObject *memoryObject,
                                          const std::string &name) {
        unsigned id = 0;
        std::string uniqueName = name;

        while (!state.arrayNames.insert(uniqueName).second) {
            uniqueName = name + "_" + llvm::utostr(++id);
        }

        const Array *array = this->arrayCache.CreateArray(uniqueName, memoryObject->size);
        this->bindObjectInState(state, memoryObject, false, array);
        state.addSymbolic(memoryObject, array);
    }

    void ADDExecutor::transferToBasicBlock(llvm::BasicBlock *dst, llvm::BasicBlock *src, ExecutionState &state) {
        KFunction *kFunction = state.stack.back().kf;

        unsigned entry = kFunction->basicBlockEntry[dst];
        state.pc = &kFunction->instructions[entry];

        if (state.pc->inst->getOpcode() == llvm::Instruction::PHI) {
            auto *first = static_cast<llvm::PHINode *>(state.pc->inst);
            state.incomingBBIndex = first->getBasicBlockIndex(src);
        }
    }

    void ADDExecutor::addSymbolicValuesToPath(const ExecutionState &state,
                                              FunctionEvaluation *functionEvaluation,
                                              Path *path) {
        // we only have one stack frame because we do not allow subroutine calls
        StackFrame stackFrame = state.stack.back();

        for (const MemoryObject *memoryObject : stackFrame.allocas) {
            std::string variableName = memoryObject->name;
            llvm::Type *variableType = functionEvaluation->getVariableTypeMap().getVariableType(variableName);

            ObjectPair objectPair;
            state.addressSpace.resolveOne(memoryObject->getBaseExpr(), objectPair);

            ref<Expr> offset = memoryObject->getOffsetExpr(memoryObject->getBaseExpr());
            ref<Expr> result = objectPair.second->read(offset, this->getWidthForLLVMType(variableType));

            path->getSymbolicValues()[variableName] = result;
        }
    }

}