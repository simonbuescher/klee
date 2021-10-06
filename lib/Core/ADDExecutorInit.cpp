//
// Created by simon on 06.10.21.
//

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

#include <klee/Module/Cell.h>
#include <klee/Module/KInstruction.h>
#include <klee/Support/ErrorHandling.h>

#include "ADDExecutor.h"
#include "ExecutionState.h"
#include "GetElementPtrTypeIterator.h"


namespace klee {

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

            if (function.hasExternalWeakLinkage() &&
                !this->externalDispatcher->resolveSymbol(function.getName().str())) {
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

            MemoryObject *memoryObject = this->memory->allocate(size, false, true, &globalVariable,
                                                                globalObjectAlignment);
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

    /*void ADDExecutor::bindInstructionConstants(KInstruction *kInstruction) {
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
    }*/

    void ADDExecutor::bindInstructionConstants(KInstruction *KI) {
        if (llvm::GetElementPtrInst *gepi = dyn_cast<llvm::GetElementPtrInst>(KI->inst)) {
            KGEPInstruction *kgepi = static_cast<KGEPInstruction *>(KI);
            computeOffsets(kgepi, gep_type_begin(gepi), gep_type_end(gepi));
        } else if (llvm::InsertValueInst *ivi = dyn_cast<llvm::InsertValueInst>(KI->inst)) {
            KGEPInstruction *kgepi = static_cast<KGEPInstruction *>(KI);
            computeOffsets(kgepi, iv_type_begin(ivi), iv_type_end(ivi));
            assert(kgepi->indices.empty() && "InsertValue constant offset expected");
        } else if (llvm::ExtractValueInst *evi = dyn_cast<llvm::ExtractValueInst>(KI->inst)) {
            KGEPInstruction *kgepi = static_cast<KGEPInstruction *>(KI);
            computeOffsets(kgepi, ev_type_begin(evi), ev_type_end(evi));
            assert(kgepi->indices.empty() && "ExtractValue constant offset expected");
        }
    }


    MemoryObject *ADDExecutor::addExternalObject(ExecutionState &state, void *address, unsigned size, bool isReadOnly) {
        auto memoryObject = this->memory->allocateFixed(reinterpret_cast<std::uint64_t>(address), size, nullptr);
        ObjectState *objectState = this->bindObjectInState(state, memoryObject, false);

        for (unsigned i = 0; i < size; i++) {
            objectState->write8(i, ((uint8_t *) address)[i]);
        }

        if (isReadOnly) {
            objectState->setReadOnly(true);
        }

        return memoryObject;
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
    void ADDExecutor::computeOffsetsSeqTy(KGEPInstruction *kgepInstruction,
                                          ref<ConstantExpr> &constantOffset,
                                          uint64_t index,
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
}

