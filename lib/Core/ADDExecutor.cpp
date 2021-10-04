//
// Created by simon on 04.10.21.
//

#include <klee/Module/Cell.h>
#include <klee/Module/KInstruction.h>
#include <klee/Support/ModuleUtil.h>
#include <llvm/Support/Path.h>
#include <klee/Support/ErrorHandling.h>
#include <llvm/IR/Instructions.h>
#include <iostream>
#include "klee/Core/ADDExecutor.h"
#include "SpecialFunctionHandler.h"
#include "GetElementPtrTypeIterator.h"


using namespace klee;

void ADDExecutor::runFunction(FunctionEvaluation *functionEvaluation) {
    llvm::Function *function = functionEvaluation->getFunction();
    KFunction *kFunction = this->kleeModule->functionMap[function];

    for (Path &path : functionEvaluation->getPathList()) {
        auto *state = new ExecutionState(kFunction);

        this->initializeGlobals(*state);
        this->bindModuleConstants();

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

                this->executeInstruction(*state, ki, functionEvaluation, kFunction, blockInPathIt);
            }
        }

        path.setConstraints(state->constraints);
        this->addSymbolicValuesToPath(*state, functionEvaluation, path);

        std::cout << "PATH FINISHED: ["
                  << path.getPathRepr()
                  << "] ["
                  << (path.shouldExecuteFinishBlock() ? "execute last" : "dont execute last")
                  << "]"
                  << std::endl;
        jsonPrinter->print(path);

        // hack to clear memory objects
        delete this->memory;
        this->memory = new MemoryManager(&this->arrayCache);

        this->globalObjects.clear();
        this->globalAddresses.clear();
    }
}

void ADDExecutor::runAllocas(KFunction *kFunction, ExecutionState *state) {
    int variableNumber = 0;

    for (unsigned int i = 0; i < kFunction->numInstructions; i++) {
        KInstruction *ki = kFunction->instructions[i];

        if (ki->inst->getOpcode() == llvm::Instruction::Alloca) {
            auto *allocaInst = cast<llvm::AllocaInst>(ki->inst);

            unsigned elementSize = this->kleeModule->targetData->getTypeStoreSize(allocaInst->getAllocatedType());

            ref<Expr> size = Expr::createPointer(elementSize);
            if (allocaInst->isArrayAllocation()) {
                ref<Expr> count = this->eval(ki, 0, *state).value;
                count = Expr::createZExtToPointerWidth(count);
                size = MulExpr::create(size, count);
            }

            this->executeAlloc(*state, size, true, ki);

            std::string variableName = "var" + llvm::itostr(variableNumber);

            const MemoryObject *memoryObject = state->stack.back().allocas.back();
            memoryObject->setName(variableName);

            this->executeMakeSymbolic(*state, memoryObject, variableName);

            variableNumber++;
        }
    }
}

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

        MemoryObject *memoryObject = this->memory->allocate(size->getZExtValue(), false, false, firstInstruction, 4);
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

void
ADDExecutor::addSymbolicValuesToPath(const ExecutionState &state, FunctionEvaluation *functionEvaluation, Path &path) {
    // we only have one stack frame because we do not allow subroutine calls
    StackFrame stackFrame = state.stack.back();

    for (const MemoryObject *memoryObject : stackFrame.allocas) {
        std::string variableName = memoryObject->name;
        llvm::Type *variableType = functionEvaluation->getVariableTypeMap().getVariableType(variableName);

        ObjectPair objectPair;
        state.addressSpace.resolveOne(memoryObject->getBaseExpr(), objectPair);

        ref<Expr> offset = memoryObject->getOffsetExpr(memoryObject->getBaseExpr());
        ref<Expr> result = objectPair.second->read(offset, this->getWidthForLLVMType(variableType));

        path.getSymbolicValues()[variableName] = result;
    }
}


llvm::Module *ADDExecutor::setModule(std::vector<std::unique_ptr<llvm::Module>> &modules, const Interpreter::ModuleOptions &opts) {
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

void
ADDExecutor::executeMakeSymbolic(ExecutionState &state, const MemoryObject *memoryObject, const std::string &name) {
    unsigned id = 0;
    std::string uniqueName = name;

    while (!state.arrayNames.insert(uniqueName).second) {
        uniqueName = name + "_" + llvm::utostr(++id);
    }

    const Array *array = this->arrayCache.CreateArray(uniqueName, memoryObject->size);
    this->bindObjectInState(state, memoryObject, false, array);
    state.addSymbolic(memoryObject, array);

    /*
    std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = seedMap.find(&state);
    if (it!=seedMap.end()) { // In seed mode we need to add this as a
        // binding.
        for (std::vector<SeedInfo>::iterator siit = it->second.begin(),
                     siie = it->second.end(); siit != siie; ++siit) {
            SeedInfo &si = *siit;
            KTestObject *obj = si.getNextInput(memoryObject, NamedSeedMatching);

            if (!obj) {
                if (ZeroSeedExtension) {
                    std::vector<unsigned char> &values = si.assignment.bindings[array];
                    values = std::vector<unsigned char>(memoryObject->size, '\0');
                } else if (!AllowSeedExtension) {
                    terminateStateOnError(state, "ran out of inputs during seeding",
                                          User);
                    break;
                }
            } else {
                if (obj->numBytes != memoryObject->size &&
                    ((!(AllowSeedExtension || ZeroSeedExtension)
                      && obj->numBytes < memoryObject->size) ||
                     (!AllowSeedTruncation && obj->numBytes > memoryObject->size))) {
                    std::stringstream msg;
                    msg << "replace size mismatch: "
                        << memoryObject->name << "[" << memoryObject->size << "]"
                        << " vs " << obj->name << "[" << obj->numBytes << "]"
                        << " in test\n";

                    terminateStateOnError(state, msg.str(), User);
                    break;
                } else {
                    std::vector<unsigned char> &values = si.assignment.bindings[array];
                    values.insert(values.begin(), obj->bytes,
                                  obj->bytes + std::min(obj->numBytes, memoryObject->size));
                    if (ZeroSeedExtension) {
                        for (unsigned i=obj->numBytes; i<memoryObject->size; ++i)
                            values.push_back('\0');
                    }
                }
            }
        }
    }
     */
}

void ADDExecutor::bindArgument(KFunction *kFunction, unsigned index, ExecutionState &state, ref<Expr> value) {
    this->getArgumentCell(state, kFunction, index).value = value;
}

const Cell &ADDExecutor::eval(KInstruction *ki, unsigned index, ExecutionState &state) const {
    assert(index < ki->inst->getNumOperands());
    int vnumber = ki->operands[index];

    assert(vnumber != -1 &&
           "Invalid operand to eval(), not a value or constant!");

    // Determine if this is a constant or not.
    if (vnumber < 0) {
        unsigned index = -vnumber - 2;
        return this->kleeModule->constantTable[index];
    } else {
        unsigned index = vnumber;
        StackFrame &sf = state.stack.back();
        return sf.locals[index];
    }
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

void ADDExecutor::stepInstruction(ExecutionState &state) {
    ++state.steppedInstructions;
    state.prevPC = state.pc;
    ++state.pc;
}


void ADDExecutor::initializeGlobals(ExecutionState &state) {
    this->allocateGlobalObjects(state);
    this->initializeGlobalAliases();
    this->initializeGlobalObjects(state);
}

void ADDExecutor::allocateGlobalObjects(ExecutionState &state) {
    llvm::Module *module = this->kleeModule->module.get();

    if (!module->getModuleInlineAsm().empty())
        klee_warning("executable has module level assembly (ignoring)");

    for (llvm::Function &function : *module) {
        ref<ConstantExpr> address;

        if (function.hasExternalWeakLinkage() && !this->externalDispatcher->resolveSymbol(function.getName().str())) {
            address = Expr::createPointer(0);
        } else {
            auto memoryObject = this->memory->allocate(8, false, true, &function, 8);
            address = Expr::createPointer(memoryObject->address);
        }

        this->globalAddresses.emplace(&function, address);
    }

#ifndef WINDOWS
    int *errnoAddress = this->getErrnoLocation(state);
    MemoryObject *errnoObject = this->addExternalObject(state, (void *) errnoAddress, sizeof *errnoAddress, false);
    errnoObject->isUserSpecified = true;
#endif

#ifdef HAVE_CTYPE_EXTERNALS
#ifndef WINDOWS
#ifndef DARWIN
    const uint16_t **address = __ctype_b_loc();
    this->addExternalObject(state, const_cast<uint16_t *>(*address - 128),
                            384 * sizeof **address, true);
    this->addExternalObject(state, address, sizeof(*address), true);

    const int32_t **lowerAddress = __ctype_tolower_loc();
    this->addExternalObject(state, const_cast<int32_t *>(*lowerAddress - 128),
                            384 * sizeof **lowerAddress, true);
    this->addExternalObject(state, lowerAddress, sizeof(*lowerAddress), true);

    const int32_t **upperAddress = __ctype_toupper_loc();
    this->addExternalObject(state, const_cast<int32_t *>(*upperAddress - 128),
                            384 * sizeof **upperAddress, true);
    this->addExternalObject(state, upperAddress, sizeof(*upperAddress), true);
#endif
#endif
#endif

    for (const llvm::GlobalVariable &globalVariable : module->globals()) {
        std::size_t globalObjectAlignment = this->getAllocationAlignment(&globalVariable);

        llvm::Type *type = globalVariable.getType()->getElementType();

        std::uint64_t size = 0;
        if (type->isSized())
            size = this->kleeModule->targetData->getTypeStoreSize(type);

        if (globalVariable.isDeclaration()) {
            if (!type->isSized()) {
                klee_warning("Type for %.*s is not sized",
                             static_cast<int>(globalVariable.getName().size()), globalVariable.getName().data());
            }

            // XXX - DWD - hardcode some things until we decide how to fix.
#ifndef WINDOWS
            if (globalVariable.getName() == "_ZTVN10__cxxabiv117__class_type_infoE") {
                size = 0x2C;
            } else if (globalVariable.getName() == "_ZTVN10__cxxabiv120__si_class_type_infoE") {
                size = 0x2C;
            } else if (globalVariable.getName() == "_ZTVN10__cxxabiv121__vmi_class_type_infoE") {
                size = 0x2C;
            }
#endif

            if (size == 0) {
                klee_warning("Unable to find size for global variable: %.*s (use will "
                             "result in out of bounds access)",
                             static_cast<int>(globalVariable.getName().size()), globalVariable.getName().data());
            }
        }

        MemoryObject *memoryObject = this->memory->allocate(size, false, true, &globalVariable, globalObjectAlignment);
        if (!memoryObject)
            klee_error("out of memory");

        this->globalObjects.emplace(&globalVariable, memoryObject);
        this->globalAddresses.emplace(&globalVariable, memoryObject->getBaseExpr());
    }
}

void ADDExecutor::initializeGlobalAlias(const llvm::Constant *constant) {
    const auto *globalAlias = dyn_cast<llvm::GlobalAlias>(constant);

    if (globalAlias) {
        if (this->globalAddresses.count(globalAlias)) {
            return;
        }

        const llvm::Constant *aliasee = globalAlias->getAliasee();
        if (const auto *globalValue = dyn_cast<llvm::GlobalValue>(aliasee)) {
            auto it = this->globalAddresses.find(globalValue);
            if (it != this->globalAddresses.end()) {
                this->globalAddresses.emplace(globalAlias, it->second);
                return;
            }
        }
    }

#if LLVM_VERSION_CODE >= LLVM_VERSION(4, 0)
    for (const auto *operandValue : constant->operand_values()) {
#else
        for (auto &it : constant->operands()) {
    const auto *operandValue = &*it;
#endif
        this->initializeGlobalAlias(cast<llvm::Constant>(operandValue));
    }

    if (globalAlias) {
        this->globalAddresses.emplace(globalAlias, this->evalConstant(globalAlias->getAliasee()));
    }
}

void ADDExecutor::initializeGlobalAliases() {
    const llvm::Module *module = this->kleeModule->module.get();
    for (const llvm::GlobalAlias &alias : module->aliases()) {
        initializeGlobalAlias(&alias);
    }
}

extern void *__dso_handle __attribute__ ((__weak__));

void ADDExecutor::initializeGlobalObjects(ExecutionState &state) {
    const llvm::Module *module = this->kleeModule->module.get();

    std::vector<ObjectState *> constantObjects;

    for (const llvm::GlobalVariable &globalVariable : module->globals()) {
        MemoryObject *memoryObject = this->globalObjects.find(&globalVariable)->second;
        ObjectState *objectState = this->bindObjectInState(state, memoryObject, false);

        if (globalVariable.isDeclaration() && memoryObject->size) {
            void *address;
            if (globalVariable.getName() == "__dso_handle") {
                address = &__dso_handle;
            } else {
                address = this->externalDispatcher->resolveSymbol(globalVariable.getName().str());
            }

            if (!address) {
                klee_error("Unable to load symbol(%.*s) while initializing globals",
                           static_cast<int>(globalVariable.getName().size()), globalVariable.getName().data());
            }

            for (unsigned offset = 0; offset < memoryObject->size; offset++) {
                objectState->write8(offset, static_cast<unsigned char *>(address)[offset]);
            }

        } else if (globalVariable.hasInitializer()) {
            this->initializeGlobalObject(state, objectState, globalVariable.getInitializer(), 0);

            if (globalVariable.isConstant()) {
                constantObjects.emplace_back(objectState);
            }

        } else {
            objectState->initializeToRandom();
        }
    }

    // initialise constant memory that is potentially used with external calls
    if (!constantObjects.empty()) {
        // initialise the actual memory with constant values
        state.addressSpace.copyOutConcretes();

        // mark constant objects as read-only
        for (auto obj : constantObjects)
            obj->setReadOnly(true);
    }
}

void ADDExecutor::initializeGlobalObject(ExecutionState &state, ObjectState *os, const llvm::Constant *constant,
                                         unsigned offset) {
    const auto targetData = this->kleeModule->targetData.get();

    if (const auto *constantVector = dyn_cast<llvm::ConstantVector>(constant)) {
        unsigned elementSize = targetData->getTypeStoreSize(constantVector->getType()->getElementType());
        for (unsigned i = 0; i != constantVector->getNumOperands(); ++i) {
            this->initializeGlobalObject(state, os, constantVector->getOperand(i), offset + i * elementSize);
        }

    } else if (isa<llvm::ConstantAggregateZero>(constant)) {
        unsigned size = targetData->getTypeStoreSize(constant->getType());
        for (unsigned i = 0; i < size; i++)
            os->write8(offset + i, (uint8_t) 0);

    } else if (const auto *constantArray = dyn_cast<llvm::ConstantArray>(constant)) {
        unsigned elementSize = targetData->getTypeStoreSize(constantArray->getType()->getElementType());
        for (unsigned i = 0; i != constantArray->getNumOperands(); ++i) {
            this->initializeGlobalObject(state, os, constantArray->getOperand(i), offset + i * elementSize);
        }

    } else if (const auto *constantStruct = dyn_cast<llvm::ConstantStruct>(constant)) {
        const llvm::StructLayout *structLayout = targetData->getStructLayout(
                cast<llvm::StructType>(constantStruct->getType()));
        for (unsigned i = 0; i != constantStruct->getNumOperands(); ++i) {
            this->initializeGlobalObject(state, os, constantStruct->getOperand(i),
                                         offset + structLayout->getElementOffset(i));
        }

    } else if (const auto *constantDataSequential = dyn_cast<llvm::ConstantDataSequential>(constant)) {
        unsigned elementSize = targetData->getTypeStoreSize(constantDataSequential->getElementType());
        for (unsigned i = 0; i != constantDataSequential->getNumElements(); ++i) {
            this->initializeGlobalObject(state, os, constantDataSequential->getElementAsConstant(i),
                                         offset + i * elementSize);
        }

    } else if (!isa<llvm::UndefValue>(constant) && !isa<llvm::MetadataAsValue>(constant)) {
        unsigned storeBits = targetData->getTypeStoreSizeInBits(constant->getType());
        ref<ConstantExpr> constExpr = this->evalConstant(constant);

        // Extend the constant if necessary;
        assert(storeBits >= constExpr->getWidth() && "Invalid store size!");
        if (storeBits > constExpr->getWidth())
            constExpr = constExpr->ZExt(storeBits);

        os->write(offset, constExpr);
    }
}

int *ADDExecutor::getErrnoLocation(const ExecutionState &state) const {
#if !defined(__APPLE__) && !defined(__FreeBSD__)
    /* From /usr/include/errno.h: it [errno] is a per-thread variable. */
    return __errno_location();
#else
    return __error();
#endif
}

MemoryObject *ADDExecutor::addExternalObject(ExecutionState &state,
                                             void *addr, unsigned size,
                                             bool isReadOnly) {
    auto mo = memory->allocateFixed(reinterpret_cast<std::uint64_t>(addr),
                                    size, nullptr);
    ObjectState *os = bindObjectInState(state, mo, false);
    for (unsigned i = 0; i < size; i++)
        os->write8(i, ((uint8_t *) addr)[i]);
    if (isReadOnly)
        os->setReadOnly(true);
    return mo;
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

ObjectState *ADDExecutor::bindObjectInState(ExecutionState &state, const MemoryObject *memoryObject, bool isLocal,
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

void ADDExecutor::bindModuleConstants() {
    for (auto &kFunctionPointer : this->kleeModule->functions) {
        KFunction *kFunction = kFunctionPointer.get();
        for (unsigned i = 0; i < kFunction->numInstructions; ++i)
            this->bindInstructionConstants(kFunction->instructions[i]);
    }

    this->kleeModule->constantTable = std::unique_ptr<Cell[]>(new Cell[this->kleeModule->constants.size()]);
    for (unsigned i = 0; i < this->kleeModule->constants.size(); ++i) {
        Cell &c = this->kleeModule->constantTable[i];
        c.value = this->evalConstant(this->kleeModule->constants[i]);
    }
}

void ADDExecutor::bindInstructionConstants(KInstruction *kInstruction) {
    if (auto *getElementPtrInst = dyn_cast<llvm::GetElementPtrInst>(kInstruction->inst)) {
        auto *kgepInstruction = static_cast<KGEPInstruction *>(kInstruction);
        this->computeOffsets(kgepInstruction, gep_type_begin(getElementPtrInst), gep_type_end(getElementPtrInst));

    } else if (auto *insertValueInst = dyn_cast<llvm::InsertValueInst>(kInstruction->inst)) {
        auto *kgepInstruction = static_cast<KGEPInstruction *>(kInstruction);
        this->computeOffsets(kgepInstruction, iv_type_begin(insertValueInst), iv_type_end(insertValueInst));
        assert(kgepInstruction->indices.empty() && "InsertValue constant offset expected");

    } else if (auto *extractValueInst = dyn_cast<llvm::ExtractValueInst>(kInstruction->inst)) {
        auto *kgepInstruction = static_cast<KGEPInstruction *>(kInstruction);
        this->computeOffsets(kgepInstruction, ev_type_begin(extractValueInst), ev_type_end(extractValueInst));
        assert(kgepInstruction->indices.empty() && "ExtractValue constant offset expected");
    }
}

template<typename TypeIt>
void ADDExecutor::computeOffsets(KGEPInstruction *kgepInstruction, TypeIt iteratorBegin, TypeIt iteratorEnd) {
    ref<ConstantExpr> constantOffset = ConstantExpr::alloc(0, Context::get().getPointerWidth());

    uint64_t index = 1;
    for (TypeIt typeIterator = iteratorBegin; typeIterator != iteratorEnd; ++typeIterator) {
        if (llvm::StructType *structType = dyn_cast<llvm::StructType>(*typeIterator)) {
            const llvm::StructLayout *structLayout = this->kleeModule->targetData->getStructLayout(structType);
            const llvm::ConstantInt *constantInt = cast<llvm::ConstantInt>(typeIterator.getOperand());
            uint64_t addend = structLayout->getElementOffset((unsigned) constantInt->getZExtValue());
            constantOffset = constantOffset->Add(ConstantExpr::alloc(addend, Context::get().getPointerWidth()));

#if LLVM_VERSION_CODE >= LLVM_VERSION(4, 0)
        } else if (isa<llvm::ArrayType>(*typeIterator)) {
            this->computeOffsetsSeqTy<llvm::ArrayType>(kgepInstruction, constantOffset, index, typeIterator);

        } else if (isa<llvm::VectorType>(*typeIterator)) {
            this->computeOffsetsSeqTy<llvm::VectorType>(kgepInstruction, constantOffset, index, typeIterator);

        } else if (isa<llvm::PointerType>(*typeIterator)) {
            this->computeOffsetsSeqTy<llvm::PointerType>(kgepInstruction, constantOffset, index, typeIterator);
#else
            } else if (isa<SequentialType>(*typeIterator)) {
      computeOffsetsSeqTy<SequentialType>(kgepInstruction, constantOffset, index, typeIterator);
#endif

        } else {
            assert("invalid type" && 0);
        }

        index++;
    }

    kgepInstruction->offset = constantOffset->getZExtValue();
}

template<typename SqType, typename TypeIt>
void
ADDExecutor::computeOffsetsSeqTy(KGEPInstruction *kgepInstruction, ref<ConstantExpr> &constantOffset, uint64_t index,
                                 const TypeIt iterator) {
    const auto *sq = cast<SqType>(*iterator);

    uint64_t elementSize = this->kleeModule->targetData->getTypeStoreSize(sq->getElementType());
    const llvm::Value *operand = iterator.getOperand();

    if (const auto *constant = dyn_cast<llvm::Constant>(operand)) {
        ref<ConstantExpr> index = evalConstant(constant)->SExt(Context::get().getPointerWidth());
        ref<ConstantExpr> addend = index->Mul(ConstantExpr::alloc(elementSize, Context::get().getPointerWidth()));
        constantOffset = constantOffset->Add(addend);

    } else {
        kgepInstruction->indices.emplace_back(index, elementSize);
    }
}

ref<Expr> ADDExecutor::toUnique(const ExecutionState &state, ref<Expr> &e) {
    ref<Expr> result = e;

    if (!isa<ConstantExpr>(e)) {
        ref<ConstantExpr> value;
        bool isTrue = false;
        e = this->optimizer.optimizeExpr(e, true);
        this->solver->setTimeout(this->coreSolverTimeout);
        if (this->solver->getValue(state.constraints, e, value, state.queryMetaData)) {
            ref<Expr> cond = EqExpr::create(e, value);
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

void ADDExecutor::bindLocal(KInstruction *target, ExecutionState &state,
                            ref<Expr> value) {
    this->getDestCell(state, target).value = value;
}

void ADDExecutor::executeMemoryOperation(ExecutionState &state, bool isWrite, ref<Expr> address,
                                         ref<Expr> value /* undef if read */,
                                         KInstruction *target /* undef if write */) {
    Expr::Width type = (isWrite ? value->getWidth() : getWidthForLLVMType(target->inst->getType()));
    unsigned bytes = Expr::getMinBytesForWidth(type);

    address = this->optimizer.optimizeExpr(address, true);

    // fast path: single in-bounds resolution
    ObjectPair objectPair;
    bool success;
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

    assert(false && "error path");
    // we are on an error path (no resolution, multiple resolution, one
    // resolution with out of bounds)
    /*
    address = optimizer.optimizeExpr(address, true);
    ResolutionList rl;
    solver->setTimeout(coreSolverTimeout);
    bool incomplete = state.addressSpace.resolve(state, solver, address, rl,
                                                 0, coreSolverTimeout);
    solver->setTimeout(time::Span());

    // XXX there is some query wasteage here. who cares?
    ExecutionState *unbound = &state;

    for (ResolutionList::iterator i = rl.begin(), ie = rl.end(); i != ie; ++i) {
        const MemoryObject *mo = i->first;
        const ObjectState *os = i->second;
        ref<Expr> inBounds = mo->getBoundsCheckPointer(address, bytes);

        StatePair branches = fork(*unbound, inBounds, true);
        ExecutionState *bound = branches.first;

        // bound can be 0 on failure or overlapped
        if (bound) {
            if (isWrite) {
                if (os->readOnly) {
                    terminateStateOnError(*bound, "memory error: object read only",
                                          ReadOnly);
                } else {
                    ObjectState *wos = bound->addressSpace.getWriteable(mo, os);
                    wos->write(mo->getOffsetExpr(address), value);
                }
            } else {
                ref<Expr> result = os->read(mo->getOffsetExpr(address), type);
                bindLocal(target, *bound, result);
            }
        }

        unbound = branches.second;
        if (!unbound)
            break;
    }

    // XXX should we distinguish out of bounds and overlapped cases?
    if (unbound) {
        if (incomplete) {
            terminateStateEarly(*unbound, "Query timed out (resolve).");
        } else {
            terminateStateOnError(*unbound, "memory error: out of bound pointer", Ptr,
                                  NULL, getAddressInfo(*unbound, address));
        }
    }
    */
}

Expr::Width ADDExecutor::getWidthForLLVMType(llvm::Type *type) const {
    return this->kleeModule->targetData->getTypeSizeInBits(type);
}

void
ADDExecutor::executeAlloc(ExecutionState &state, ref<Expr> size, bool isLocal, KInstruction *target, bool zeroMemory,
                          const ObjectState *reallocFrom, size_t allocationAlignment) {
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