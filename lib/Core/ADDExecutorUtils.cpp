//
// Created by simon on 04.10.21.
//


#include <llvm/IR/Module.h>
#include <klee/Expr/Expr.h>
#include <llvm/IR/Constants.h>
#include <klee/Module/KInstruction.h>
#include <klee/Support/ErrorHandling.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Instructions.h>
#include "klee/Core/ADDExecutor.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"

using namespace klee;

ref<ConstantExpr> ADDExecutor::evalConstant(const llvm::Constant *constant, const KInstruction *kInstruction) {
    if (!kInstruction) {
        KConstant *kConstant = this->kleeModule->getKConstant(constant);
        if (kConstant) {
            kInstruction = kConstant->ki;
        }
    }

    if (const auto *constantExpr = dyn_cast<llvm::ConstantExpr>(constant)) {
        return this->evalConstantExpr(constantExpr, kInstruction);

    } else {
        if (const auto *constantInt = dyn_cast<llvm::ConstantInt>(constant)) {
            return ConstantExpr::alloc(constantInt->getValue());

        } else if (const auto *constantFloat = dyn_cast<llvm::ConstantFP>(constant)) {
            return ConstantExpr::alloc(constantFloat->getValueAPF().bitcastToAPInt());

        } else if (const auto *globalValue = dyn_cast<llvm::GlobalValue>(constant)) {
            auto it = this->globalAddresses.find(globalValue);
            assert(it != this->globalAddresses.end());
            return it->second;

        } else if (isa<llvm::ConstantPointerNull>(constant)) {
            return Expr::createPointer(0);

        } else if (isa<llvm::UndefValue>(constant) || isa<llvm::ConstantAggregateZero>(constant)) {
            if (this->getWidthForLLVMType(constant->getType()) == 0) {
                if (isa<llvm::LandingPadInst>(kInstruction->inst)) {
                    klee_warning_once(0, "Using zero size array fix for landingpad instruction filter");
                    return ConstantExpr::create(0, 1);
                }
            }
            return ConstantExpr::create(0, this->getWidthForLLVMType(constant->getType()));

        } else if (const auto *constantDataSequential = dyn_cast<llvm::ConstantDataSequential>(constant)) {
            // Handle a vector or array: first element has the smallest address,
            // the last element the highest
            std::vector<ref<Expr>> kids;
            for (unsigned i = constantDataSequential->getNumElements(); i != 0; --i) {
                ref<Expr> kid = this->evalConstant(constantDataSequential->getElementAsConstant(i - 1), kInstruction);
                kids.push_back(kid);
            }

            assert(Context::get().isLittleEndian() && "FIXME:Broken for big endian");

            ref<Expr> res = ConcatExpr::createN(kids.size(), kids.data());
            return cast<ConstantExpr>(res);

        } else if (const auto *constantStruct = dyn_cast<llvm::ConstantStruct>(constant)) {
            const llvm::StructLayout *structLayout = this->kleeModule->targetData->getStructLayout(constantStruct->getType());

            llvm::SmallVector<ref<Expr>, 4> kids;
            for (unsigned i = constantStruct->getNumOperands(); i != 0; --i) {
                unsigned op = i - 1;
                ref<Expr> kid = this->evalConstant(constantStruct->getOperand(op), kInstruction);

                uint64_t thisOffset = structLayout->getElementOffsetInBits(op);
                uint64_t nextOffset = (op == constantStruct->getNumOperands() - 1)
                                     ? structLayout->getSizeInBits()
                                     : structLayout->getElementOffsetInBits(op + 1);

                if (nextOffset - thisOffset > kid->getWidth()) {
                    uint64_t paddingWidth = nextOffset - thisOffset - kid->getWidth();
                    kids.push_back(ConstantExpr::create(0, paddingWidth));
                }

                kids.push_back(kid);
            }

            assert(Context::get().isLittleEndian() && "FIXME:Broken for big endian");

            ref<Expr> res = ConcatExpr::createN(kids.size(), kids.data());
            return cast<ConstantExpr>(res);

        } else if (const auto *constantArray = dyn_cast<llvm::ConstantArray>(constant)) {
            llvm::SmallVector<ref<Expr>, 4> kids;

            for (unsigned i = constantArray->getNumOperands(); i != 0; --i) {
                unsigned op = i - 1;
                ref<Expr> kid = this->evalConstant(constantArray->getOperand(op), kInstruction);
                kids.push_back(kid);
            }

            assert(Context::get().isLittleEndian() && "FIXME:Broken for big endian");

            ref<Expr> res = ConcatExpr::createN(kids.size(), kids.data());
            return cast<ConstantExpr>(res);

        } else if (const auto *constantVector = dyn_cast<llvm::ConstantVector>(constant)) {
            llvm::SmallVector<ref<Expr>, 8> kids;

            const size_t numOperands = constantVector->getNumOperands();
            kids.reserve(numOperands);

            for (unsigned i = numOperands; i != 0; --i) {
                kids.push_back(this->evalConstant(constantVector->getOperand(i - 1), kInstruction));
            }

            assert(Context::get().isLittleEndian() && "FIXME:Broken for big endian");

            ref<Expr> res = ConcatExpr::createN(numOperands, kids.data());

            assert(isa<ConstantExpr>(res) && "result of constant vector built is not a constant");

            return cast<ConstantExpr>(res);

        } else if (const auto *blockAddress = dyn_cast<llvm::BlockAddress>(constant)) {
            // return the address of the specified basic block in the specified function
            const auto arg_bb = (llvm::BasicBlock *) blockAddress->getOperand(1);
            const auto res = Expr::createPointer(reinterpret_cast<std::uint64_t>(arg_bb));
            return cast<ConstantExpr>(res);

        } else {
            std::string msg("Cannot handle constant ");
            llvm::raw_string_ostream os(msg);
            os << "'" << *constant << "' at location "
               << (kInstruction ? kInstruction->getSourceLocation() : "[unknown]");
            klee_error("%s", os.str().c_str());
        }
    }
}


ref<ConstantExpr> ADDExecutor::evalConstantExpr(const llvm::ConstantExpr *constantExpr, const KInstruction *kInstruction) {
    llvm::Type *type = constantExpr->getType();

    ref<ConstantExpr> op1(0), op2(0), op3(0);
    unsigned numOperands = constantExpr->getNumOperands();

    if (numOperands > 0) op1 = this->evalConstant(constantExpr->getOperand(0), kInstruction);
    if (numOperands > 1) op2 = this->evalConstant(constantExpr->getOperand(1), kInstruction);
    if (numOperands > 2) op3 = this->evalConstant(constantExpr->getOperand(2), kInstruction);

    /* Checking for possible errors during constant folding */
    switch (constantExpr->getOpcode()) {
        case llvm::Instruction::SDiv:
        case llvm::Instruction::UDiv:
        case llvm::Instruction::SRem:
        case llvm::Instruction::URem:
            if (op2->getLimitedValue() == 0) {
                std::string msg("Division/modulo by zero during constant folding at location ");
                llvm::raw_string_ostream os(msg);
                os << (kInstruction ? kInstruction->getSourceLocation() : "[unknown]");
                klee_error("%s", os.str().c_str());
            }
            break;
        case llvm::Instruction::Shl:
        case llvm::Instruction::LShr:
        case llvm::Instruction::AShr:
            if (op2->getLimitedValue() >= op1->getWidth()) {
                std::string msg("Overshift during constant folding at location ");
                llvm::raw_string_ostream os(msg);
                os << (kInstruction ? kInstruction->getSourceLocation() : "[unknown]");
                klee_error("%s", os.str().c_str());
            }
    }

    std::string msg("Unknown ConstantExpr type");
    llvm::raw_string_ostream os(msg);

    switch (constantExpr->getOpcode()) {
        default :
            os << "'" << *constantExpr << "' at location "
               << (kInstruction ? kInstruction->getSourceLocation() : "[unknown]");
            klee_error("%s", os.str().c_str());

        case llvm::Instruction::Trunc:
            return op1->Extract(0, getWidthForLLVMType(type));
        case llvm::Instruction::ZExt:
            return op1->ZExt(getWidthForLLVMType(type));
        case llvm::Instruction::SExt:
            return op1->SExt(getWidthForLLVMType(type));
        case llvm::Instruction::Add:
            return op1->Add(op2);
        case llvm::Instruction::Sub:
            return op1->Sub(op2);
        case llvm::Instruction::Mul:
            return op1->Mul(op2);
        case llvm::Instruction::SDiv:
            return op1->SDiv(op2);
        case llvm::Instruction::UDiv:
            return op1->UDiv(op2);
        case llvm::Instruction::SRem:
            return op1->SRem(op2);
        case llvm::Instruction::URem:
            return op1->URem(op2);
        case llvm::Instruction::And:
            return op1->And(op2);
        case llvm::Instruction::Or:
            return op1->Or(op2);
        case llvm::Instruction::Xor:
            return op1->Xor(op2);
        case llvm::Instruction::Shl:
            return op1->Shl(op2);
        case llvm::Instruction::LShr:
            return op1->LShr(op2);
        case llvm::Instruction::AShr:
            return op1->AShr(op2);
        case llvm::Instruction::BitCast:
            return op1;

        case llvm::Instruction::IntToPtr:
            return op1->ZExt(getWidthForLLVMType(type));

        case llvm::Instruction::PtrToInt:
            return op1->ZExt(getWidthForLLVMType(type));

        case llvm::Instruction::GetElementPtr: {
            ref<ConstantExpr> base = op1->ZExt(Context::get().getPointerWidth());
            for (llvm::gep_type_iterator ii = gep_type_begin(constantExpr), ie = gep_type_end(constantExpr);
                 ii != ie; ++ii) {
                ref<ConstantExpr> indexOp = this->evalConstant(cast<llvm::Constant>(ii.getOperand()), kInstruction);
                if (indexOp->isZero())
                    continue;

                // Handle a struct index, which adds its field offset to the pointer.
#if LLVM_VERSION_CODE >= LLVM_VERSION(4, 0)
                if (auto STy = ii.getStructTypeOrNull()) {
#else
                    if (StructType *STy = dyn_cast<StructType>(*ii)) {
#endif
                    unsigned ElementIdx = indexOp->getZExtValue();
                    const llvm::StructLayout *SL = this->kleeModule->targetData->getStructLayout(STy);
                    base = base->Add(
                            ConstantExpr::alloc(llvm::APInt(Context::get().getPointerWidth(),
                                                            SL->getElementOffset(ElementIdx))));
                    continue;
                }

                // For array or vector indices, scale the index by the size of the type.
                // Indices can be negative
                base = base->Add(indexOp->SExt(Context::get().getPointerWidth())
                                         ->Mul(ConstantExpr::alloc(
                                                 llvm::APInt(Context::get().getPointerWidth(),
                                                             this->kleeModule->targetData->getTypeAllocSize(
                                                                     ii.getIndexedType())))));
            }
            return base;
        }

        case llvm::Instruction::ICmp: {
            switch (constantExpr->getPredicate()) {
                default:
                    assert(0 && "unhandled ICmp predicate");
                case llvm::ICmpInst::ICMP_EQ:
                    return op1->Eq(op2);
                case llvm::ICmpInst::ICMP_NE:
                    return op1->Ne(op2);
                case llvm::ICmpInst::ICMP_UGT:
                    return op1->Ugt(op2);
                case llvm::ICmpInst::ICMP_UGE:
                    return op1->Uge(op2);
                case llvm::ICmpInst::ICMP_ULT:
                    return op1->Ult(op2);
                case llvm::ICmpInst::ICMP_ULE:
                    return op1->Ule(op2);
                case llvm::ICmpInst::ICMP_SGT:
                    return op1->Sgt(op2);
                case llvm::ICmpInst::ICMP_SGE:
                    return op1->Sge(op2);
                case llvm::ICmpInst::ICMP_SLT:
                    return op1->Slt(op2);
                case llvm::ICmpInst::ICMP_SLE:
                    return op1->Sle(op2);
            }
        }

        case llvm::Instruction::Select:
            return op1->isTrue() ? op2 : op3;

        case llvm::Instruction::FAdd:
        case llvm::Instruction::FSub:
        case llvm::Instruction::FMul:
        case llvm::Instruction::FDiv:
        case llvm::Instruction::FRem:
        case llvm::Instruction::FPTrunc:
        case llvm::Instruction::FPExt:
        case llvm::Instruction::UIToFP:
        case llvm::Instruction::SIToFP:
        case llvm::Instruction::FPToUI:
        case llvm::Instruction::FPToSI:
        case llvm::Instruction::FCmp:
            assert(0 && "floating point ConstantExprs unsupported");
    }
    llvm_unreachable("Unsupported expression in evalConstantExpr");
    return op1;
}