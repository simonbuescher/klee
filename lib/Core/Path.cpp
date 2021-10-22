//
// Created by simon on 03.07.21.
//

#include "klee/Core/Path.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"

namespace klee {

    Path::Path()= default;

    void Path::setExecuteFinishBlock(bool value) {
        executeFinishBlock = value;
    }

    bool Path::shouldExecuteFinishBlock() {
        return executeFinishBlock;
    }

    void Path::addBlock(llvm::BasicBlock *block) {
        blocks.push_back(block);

        if (!repr.empty()) {
            repr.append(" -> ");
        }
        repr.append(block->getName().str());
    }

    bool Path::containsBlock(llvm::BasicBlock *block) {
        return std::find(blocks.begin(), blocks.end(), block) != blocks.end();
    }

    void Path::setConstraints(ConstraintSet constraintSet) {
        constraints = ConstraintSet(constraintSet);
    }

    ConstraintSet Path::getConstraints() {
        return constraints;
    }

    VariableExpressionMap &Path::getSymbolicValues() {
        return symbolicValues;
    }

    std::string Path::getPathRepr() {
        return repr;
    }

    llvm::BasicBlock *Path::front() {
        return blocks.front();
    }

    llvm::BasicBlock *Path::back() {
        return blocks.back();
    }

    int Path::size() {
        return blocks.size();
    }

    std::vector<llvm::BasicBlock *>::iterator Path::begin() {
        return blocks.begin();
    }

    std::vector<llvm::BasicBlock *>::iterator Path::end() {
        return blocks.end();
    }

    void Path::copy(Path *result) {
        result->blocks = std::vector<llvm::BasicBlock *>(this->blocks);
        result->executeFinishBlock = this->executeFinishBlock;

        result->constraints = ConstraintSet(this->constraints);
        result->symbolicValues = VariableExpressionMap(this->symbolicValues);

        result->repr = std::string(this->repr);
    }

}