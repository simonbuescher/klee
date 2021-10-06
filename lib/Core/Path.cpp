//
// Created by simon on 03.07.21.
//

#include "klee/Core/Path.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"

namespace klee {

    Path::Path()= default;

    Path::Path(Path const &path) {
        blocks = std::vector<llvm::BasicBlock *>(path.blocks);
        executeFinishBlock = path.executeFinishBlock;

        constraints = ConstraintSet(path.constraints);
        symbolicValues = VariableExpressionMap(path.symbolicValues);

        repr = path.repr;
    }

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

    std::vector<llvm::BasicBlock *>::iterator Path::begin() {
        return blocks.begin();
    }

    std::vector<llvm::BasicBlock *>::iterator Path::end() {
        return blocks.end();
    }

}