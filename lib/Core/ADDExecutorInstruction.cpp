//
// Created by simon on 04.10.21.
//

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

#include <klee/Module/Cell.h>
#include <klee/Module/KInstruction.h>
#include <klee/Module/KModule.h>

#include "ADDExecutor.h"
#include "ExecutionState.h"


namespace klee {

    void ADDExecutor::executeInstruction(ExecutionState &state,
                                         KInstruction *kInstruction,
                                         FunctionEvaluation *functionEvaluation,
                                         KFunction *kFunction,
                                         std::vector<llvm::BasicBlock *>::iterator blockInPathIt) {
        llvm::Instruction *instruction = kInstruction->inst;
        switch (instruction->getOpcode()) {
            // Control flow
            case llvm::Instruction::Ret: {
                auto *ri = cast<llvm::ReturnInst>(instruction);
                bool isVoidReturn = (ri->getNumOperands() == 0);

                if (isVoidReturn) {
                    break;
                }

                // get local number of return value (ki->operators[0] should do)
                int n = kInstruction->operands[0];

                KInstruction *loadInstruction;
                for (unsigned i = 0; i < kFunction->numInstructions; i++) {
                    if ((int) kFunction->instructions[i]->dest == n) {
                        loadInstruction = kFunction->instructions[i];
                        break;
                    }
                }

                // get instruction of that number
                llvm::Instruction *returnValueInstruction = loadInstruction->inst;

                // if its not a load, assert(false) for now
                if (returnValueInstruction->getOpcode() != llvm::Instruction::Load) {
                    assert(false && "what should i do now?");
                }

                // if its a load, get what value we are loading
                ref<Expr> result = this->eval(loadInstruction, 0, state).value;

                // get variable name out of it
                bool resolveSuccess;
                ObjectPair objectPair;
                state.addressSpace.resolveOne(state, solver, result, objectPair, resolveSuccess);

                std::string name = objectPair.first->name;

                // save variable name in function evaluation
                bool success = functionEvaluation->setReturnValueName(name);
                if (!success) {
                    assert(false && "different return values on different paths are not allowed");
                }

                break;
            }
            case llvm::Instruction::Br: {
                auto *branchInstruction = cast<llvm::BranchInst>(instruction);

                llvm::BasicBlock *successorInPath = *(blockInPathIt + 1);

                if (!branchInstruction->isUnconditional()) {
                    ref<Expr> condition = this->eval(kInstruction, 0, state).value;
                    condition = this->optimizer.optimizeExpr(condition, false);

                    if (branchInstruction->getSuccessor(0) == successorInPath) {
                        state.constraints.push_back(condition);
                    }
                    if (branchInstruction->getSuccessor(1) == successorInPath) {
                        state.constraints.push_back(Expr::createIsZero(condition));
                    }
                }

                // todo sbuescher do we need this?
                this->transferToBasicBlock(successorInPath, *blockInPathIt, state);

                break;
            }
            case llvm::Instruction::IndirectBr: {
                assert(false && "indirect branch currently not supported");
            }
            case llvm::Instruction::Switch: {
                auto *switchInstruction = cast<llvm::SwitchInst>(instruction);

                llvm::BasicBlock *successorInPath = *(blockInPathIt + 1);

                ref<Expr> switchExpression = this->eval(kInstruction, 0, state).value;
                switchExpression = this->toUnique(state, switchExpression);

                ref<Expr> defaultCondition;

                std::vector<ref<Expr>> targets;
                for (auto switchCase : switchInstruction->cases()) {
                    ref<Expr> caseExpression = this->evalConstant(switchCase.getCaseValue());
                    llvm::BasicBlock *caseSuccessor = switchCase.getCaseSuccessor();

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

                state.constraints.push_back(branchCondition);

                // todo sbuescher do we need this?
                this->transferToBasicBlock(successorInPath, *blockInPathIt, state);

                break;
            }
            case llvm::Instruction::Unreachable: {
                assert(false && "unreachable instruction reached");
            }
            case llvm::Instruction::Invoke:
            case llvm::Instruction::Call: {
                assert(false && "calls are currently not supported");
            }
            case llvm::Instruction::PHI: {
                assert(false && "phi instructions are currently not supported");
            }
            case llvm::Instruction::Select: {
                ref<Expr> cond = this->eval(kInstruction, 0, state).value;
                ref<Expr> tExpr = this->eval(kInstruction, 1, state).value;
                ref<Expr> fExpr = this->eval(kInstruction, 2, state).value;
                ref<Expr> result = SelectExpr::create(cond, tExpr, fExpr);
                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::VAArg: {
                assert(false && "unexpected vaarg instruction");
            }

            case llvm::Instruction::Add: {
                ref<Expr> left = this->eval(kInstruction, 0, state).value;
                ref<Expr> right = this->eval(kInstruction, 1, state).value;
                this->bindLocal(kInstruction, state, AddExpr::create(left, right));
                break;
            }
            case llvm::Instruction::Sub: {
                ref<Expr> left = this->eval(kInstruction, 0, state).value;
                ref<Expr> right = this->eval(kInstruction, 1, state).value;
                this->bindLocal(kInstruction, state, SubExpr::create(left, right));
                break;
            }
            case llvm::Instruction::Mul: {
                ref<Expr> left = this->eval(kInstruction, 0, state).value;
                ref<Expr> right = this->eval(kInstruction, 1, state).value;
                this->bindLocal(kInstruction, state, MulExpr::create(left, right));
                break;
            }
            case llvm::Instruction::UDiv: {
                ref<Expr> left = this->eval(kInstruction, 0, state).value;
                ref<Expr> right = this->eval(kInstruction, 1, state).value;
                ref<Expr> result = UDivExpr::create(left, right);
                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::SDiv: {
                ref<Expr> left = this->eval(kInstruction, 0, state).value;
                ref<Expr> right = this->eval(kInstruction, 1, state).value;
                ref<Expr> result = SDivExpr::create(left, right);
                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::URem: {
                ref<Expr> left = this->eval(kInstruction, 0, state).value;
                ref<Expr> right = this->eval(kInstruction, 1, state).value;
                ref<Expr> result = URemExpr::create(left, right);
                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::SRem: {
                ref<Expr> left = this->eval(kInstruction, 0, state).value;
                ref<Expr> right = this->eval(kInstruction, 1, state).value;
                ref<Expr> result = SRemExpr::create(left, right);
                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::And: {
                ref<Expr> left = this->eval(kInstruction, 0, state).value;
                ref<Expr> right = this->eval(kInstruction, 1, state).value;
                ref<Expr> result = AndExpr::create(left, right);
                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::Or: {
                ref<Expr> left = this->eval(kInstruction, 0, state).value;
                ref<Expr> right = this->eval(kInstruction, 1, state).value;
                ref<Expr> result = OrExpr::create(left, right);
                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::Xor: {
                ref<Expr> left = this->eval(kInstruction, 0, state).value;
                ref<Expr> right = this->eval(kInstruction, 1, state).value;
                ref<Expr> result = XorExpr::create(left, right);
                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::Shl: {
                ref<Expr> left = this->eval(kInstruction, 0, state).value;
                ref<Expr> right = this->eval(kInstruction, 1, state).value;
                ref<Expr> result = ShlExpr::create(left, right);
                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::LShr: {
                ref<Expr> left = this->eval(kInstruction, 0, state).value;
                ref<Expr> right = this->eval(kInstruction, 1, state).value;
                ref<Expr> result = LShrExpr::create(left, right);
                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::AShr: {
                ref<Expr> left = this->eval(kInstruction, 0, state).value;
                ref<Expr> right = this->eval(kInstruction, 1, state).value;
                ref<Expr> result = AShrExpr::create(left, right);
                this->bindLocal(kInstruction, state, result);
                break;
            }

            case llvm::Instruction::ICmp: {
                auto *ci = cast<llvm::CmpInst>(instruction);
                auto *ii = cast<llvm::ICmpInst>(ci);

                switch (ii->getPredicate()) {
                    case llvm::ICmpInst::ICMP_EQ: {
                        ref<Expr> left = this->eval(kInstruction, 0, state).value;
                        ref<Expr> right = this->eval(kInstruction, 1, state).value;
                        ref<Expr> result = EqExpr::create(left, right);
                        this->bindLocal(kInstruction, state, result);
                        break;
                    }

                    case llvm::ICmpInst::ICMP_NE: {
                        ref<Expr> left = this->eval(kInstruction, 0, state).value;
                        ref<Expr> right = this->eval(kInstruction, 1, state).value;
                        ref<Expr> result = NeExpr::create(left, right);
                        this->bindLocal(kInstruction, state, result);
                        break;
                    }

                    case llvm::ICmpInst::ICMP_UGT: {
                        ref<Expr> left = this->eval(kInstruction, 0, state).value;
                        ref<Expr> right = this->eval(kInstruction, 1, state).value;
                        ref<Expr> result = UgtExpr::create(left, right);
                        this->bindLocal(kInstruction, state, result);
                        break;
                    }

                    case llvm::ICmpInst::ICMP_UGE: {
                        ref<Expr> left = this->eval(kInstruction, 0, state).value;
                        ref<Expr> right = this->eval(kInstruction, 1, state).value;
                        ref<Expr> result = UgeExpr::create(left, right);
                        this->bindLocal(kInstruction, state, result);
                        break;
                    }

                    case llvm::ICmpInst::ICMP_ULT: {
                        ref<Expr> left = this->eval(kInstruction, 0, state).value;
                        ref<Expr> right = this->eval(kInstruction, 1, state).value;
                        ref<Expr> result = UltExpr::create(left, right);
                        this->bindLocal(kInstruction, state, result);
                        break;
                    }

                    case llvm::ICmpInst::ICMP_ULE: {
                        ref<Expr> left = this->eval(kInstruction, 0, state).value;
                        ref<Expr> right = this->eval(kInstruction, 1, state).value;
                        ref<Expr> result = UleExpr::create(left, right);
                        this->bindLocal(kInstruction, state, result);
                        break;
                    }

                    case llvm::ICmpInst::ICMP_SGT: {
                        ref<Expr> left = this->eval(kInstruction, 0, state).value;
                        ref<Expr> right = this->eval(kInstruction, 1, state).value;
                        ref<Expr> result = SgtExpr::create(left, right);
                        this->bindLocal(kInstruction, state, result);
                        break;
                    }

                    case llvm::ICmpInst::ICMP_SGE: {
                        ref<Expr> left = this->eval(kInstruction, 0, state).value;
                        ref<Expr> right = this->eval(kInstruction, 1, state).value;
                        ref<Expr> result = SgeExpr::create(left, right);
                        this->bindLocal(kInstruction, state, result);
                        break;
                    }

                    case llvm::ICmpInst::ICMP_SLT: {
                        ref<Expr> left = this->eval(kInstruction, 0, state).value;
                        ref<Expr> right = this->eval(kInstruction, 1, state).value;
                        ref<Expr> result = SltExpr::create(left, right);
                        this->bindLocal(kInstruction, state, result);
                        break;
                    }

                    case llvm::ICmpInst::ICMP_SLE: {
                        ref<Expr> left = this->eval(kInstruction, 0, state).value;
                        ref<Expr> right = this->eval(kInstruction, 1, state).value;
                        ref<Expr> result = SleExpr::create(left, right);
                        this->bindLocal(kInstruction, state, result);
                        break;
                    }

                    default:
                        assert(false && "invalid ICmp predicate");
                }
                break;
            }

            case llvm::Instruction::Alloca: {
                // allocas are done first in path execution
                break;
            }

            case llvm::Instruction::Load: {
                ref<Expr> base = this->eval(kInstruction, 0, state).value;
                this->executeMemoryOperation(state, false, base, nullptr, kInstruction);
                break;
            }
            case llvm::Instruction::Store: {
                ref<Expr> base = this->eval(kInstruction, 1, state).value;
                ref<Expr> value = this->eval(kInstruction, 0, state).value;
                this->executeMemoryOperation(state, true, base, value, nullptr);
                break;
            }

            case llvm::Instruction::GetElementPtr: {
                auto *kgepi = static_cast<KGEPInstruction *>(kInstruction);
                ref<Expr> base = this->eval(kInstruction, 0, state).value;

                for (auto it = kgepi->indices.begin(), ie = kgepi->indices.end();
                     it != ie; ++it) {
                    uint64_t elementSize = it->second;
                    ref<Expr> index = this->eval(kInstruction, it->first, state).value;
                    base = AddExpr::create(base,
                                           MulExpr::create(Expr::createSExtToPointerWidth(index),
                                                           Expr::createPointer(elementSize)));
                }
                if (kgepi->offset)
                    base = AddExpr::create(base,
                                           Expr::createPointer(kgepi->offset));
                this->bindLocal(kInstruction, state, base);
                break;
            }

            case llvm::Instruction::Trunc: {
                auto *ci = cast<llvm::CastInst>(instruction);
                ref<Expr> result = ExtractExpr::create(this->eval(kInstruction, 0, state).value,
                                                       0,
                                                       this->getWidthForLLVMType(ci->getType()));
                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::ZExt: {
                auto *ci = cast<llvm::CastInst>(instruction);
                ref<Expr> result = ZExtExpr::create(this->eval(kInstruction, 0, state).value,
                                                    this->getWidthForLLVMType(ci->getType()));
                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::SExt: {
                auto *ci = cast<llvm::CastInst>(instruction);
                ref<Expr> result = SExtExpr::create(this->eval(kInstruction, 0, state).value,
                                                    this->getWidthForLLVMType(ci->getType()));
                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::IntToPtr: {
                auto *ci = cast<llvm::CastInst>(instruction);
                Expr::Width pType = this->getWidthForLLVMType(ci->getType());
                ref<Expr> arg = this->eval(kInstruction, 0, state).value;
                this->bindLocal(kInstruction, state, ZExtExpr::create(arg, pType));
                break;
            }
            case llvm::Instruction::PtrToInt: {
                auto *ci = cast<llvm::CastInst>(instruction);
                Expr::Width iType = this->getWidthForLLVMType(ci->getType());
                ref<Expr> arg = this->eval(kInstruction, 0, state).value;
                this->bindLocal(kInstruction, state, ZExtExpr::create(arg, iType));
                break;
            }
            case llvm::Instruction::BitCast: {
                ref<Expr> result = this->eval(kInstruction, 0, state).value;
                this->bindLocal(kInstruction, state, result);
                break;
            }

            case llvm::Instruction::FNeg:
            case llvm::Instruction::FAdd:
            case llvm::Instruction::FSub:
            case llvm::Instruction::FMul:
            case llvm::Instruction::FDiv:
            case llvm::Instruction::FRem:
            case llvm::Instruction::FPTrunc:
            case llvm::Instruction::FPExt:
            case llvm::Instruction::FPToUI:
            case llvm::Instruction::FPToSI:
            case llvm::Instruction::UIToFP:
            case llvm::Instruction::SIToFP:
            case llvm::Instruction::FCmp: {
                assert(false && "floating point instructions are currently not supported");
            }

            case llvm::Instruction::InsertValue: {
                auto *kgepi = static_cast<KGEPInstruction *>(kInstruction);

                ref<Expr> agg = this->eval(kInstruction, 0, state).value;
                ref<Expr> val = this->eval(kInstruction, 1, state).value;

                ref<Expr> l = nullptr, r = nullptr;
                unsigned lOffset = kgepi->offset * 8, rOffset = kgepi->offset * 8 + val->getWidth();

                if (lOffset > 0)
                    l = ExtractExpr::create(agg, 0, lOffset);
                if (rOffset < agg->getWidth())
                    r = ExtractExpr::create(agg, rOffset, agg->getWidth() - rOffset);

                ref<Expr> result;
                if (l && r)
                    result = ConcatExpr::create(r, ConcatExpr::create(val, l));
                else if (l)
                    result = ConcatExpr::create(val, l);
                else if (r)
                    result = ConcatExpr::create(r, val);
                else
                    result = val;

                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::ExtractValue: {
                auto *kgepi = static_cast<KGEPInstruction *>(kInstruction);

                ref<Expr> agg = this->eval(kInstruction, 0, state).value;

                ref<Expr> result = ExtractExpr::create(agg, kgepi->offset * 8,
                                                       getWidthForLLVMType(instruction->getType()));

                this->bindLocal(kInstruction, state, result);
                break;
            }
            case llvm::Instruction::Fence: {
                assert(false && "fence instruction not implemented");
            }
            case llvm::Instruction::InsertElement: {
                auto *iei = cast<llvm::InsertElementInst>(instruction);
                ref<Expr> vec = this->eval(kInstruction, 0, state).value;
                ref<Expr> newElt = this->eval(kInstruction, 1, state).value;
                ref<Expr> idx = this->eval(kInstruction, 2, state).value;

                auto *cIdx = dyn_cast<ConstantExpr>(idx);
                if (cIdx == nullptr) {
                    assert(false && "InsertElement, support for symbolic index not implemented");
                }

                uint64_t iIdx = cIdx->getZExtValue();
                const llvm::VectorType *vt = iei->getType();
                unsigned EltBits = this->getWidthForLLVMType(vt->getElementType());

                if (iIdx >= vt->getNumElements()) {
                    assert(false && "Out of bounds write when inserting element");
                }

                const unsigned elementCount = vt->getNumElements();
                llvm::SmallVector<ref<Expr>, 8> elems;
                elems.reserve(elementCount);
                for (unsigned i = elementCount; i != 0; --i) {
                    auto of = i - 1;
                    unsigned bitOffset = EltBits * of;
                    elems.push_back(
                            of == iIdx ? newElt : ExtractExpr::create(vec, bitOffset, EltBits));
                }

                assert(Context::get().isLittleEndian() && "FIXME:Broken for big endian");
                ref<Expr> Result = ConcatExpr::createN(elementCount, elems.data());
                this->bindLocal(kInstruction, state, Result);
                break;
            }
            case llvm::Instruction::ExtractElement: {
                auto *eei = cast<llvm::ExtractElementInst>(instruction);
                ref<Expr> vec = this->eval(kInstruction, 0, state).value;
                ref<Expr> idx = this->eval(kInstruction, 1, state).value;

                auto *cIdx = dyn_cast<ConstantExpr>(idx);
                if (cIdx == nullptr) {
                    assert(false && "ExtractElement, support for symbolic index not implemented");
                }

                uint64_t iIdx = cIdx->getZExtValue();
                const llvm::VectorType *vt = eei->getVectorOperandType();
                unsigned EltBits = getWidthForLLVMType(vt->getElementType());

                if (iIdx >= vt->getNumElements()) {
                    assert(false && "Out of bounds read when extracting element");
                }

                unsigned bitOffset = EltBits * iIdx;
                ref<Expr> Result = ExtractExpr::create(vec, bitOffset, EltBits);
                this->bindLocal(kInstruction, state, Result);
                break;
            }

            case llvm::Instruction::ShuffleVector:
                assert(false && "Unexpected ShuffleVector instruction");

            case llvm::Instruction::AtomicRMW:
                assert(false && "Unexpected Atomic instruction, should be lowered by LowerAtomicInstructionPass");

            case llvm::Instruction::AtomicCmpXchg:
                assert(false &&
                       "Unexpected AtomicCmpXchg instruction, should be lowered by LowerAtomicInstructionPass");

            default:
                assert(false && "illegal instruction");
        }
    }

}