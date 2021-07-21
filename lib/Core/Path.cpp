//
// Created by simon on 03.07.21.
//

#include "Path.h"

#include <utility>

#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"

namespace klee {

    Path::Path() {
        // empty
    }

    Path::Path(Path const &path) {
        blocks = std::vector<llvm::BasicBlock *>(path.blocks);
        executeFinishBlock = path.executeFinishBlock;

        constraints = ConstraintSet(path.constraints);
        symbolicValues = std::map<std::string, ref<Expr>>(path.symbolicValues);

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

    void Path::addSymbolicValue(const std::string& name, ref<Expr> symbolicValue) {
        symbolicValues[name] = symbolicValue;
    }

    std::map<std::string, ref<Expr>> Path::getSymbolicValues() {
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

    void findPaths(llvm::Function *function, std::vector<Path> *results) {
        std::vector<Path> pathsToFinish;
        std::vector<llvm::BasicBlock *> cutpoints;

        llvm::BasicBlock &entryBlock = function->getEntryBlock();

        Path startingPath;
        startingPath.addBlock(&entryBlock);

        pathsToFinish.push_back(startingPath);
        cutpoints.push_back(&entryBlock);

        while (!pathsToFinish.empty()) {
            Path currentPath = pathsToFinish.front();
            pathsToFinish.erase(pathsToFinish.begin());

            llvm::BasicBlock *currentBlock = currentPath.back();

            if (succ_empty(currentBlock)) {
                currentPath.setExecuteFinishBlock(true);
                results->push_back(currentPath);
                continue;
            }

            std::vector<Path> newPathsToFinish;
            for (auto successor : successors(currentBlock)) {
                Path newPath = Path(currentPath);

                if (std::find(cutpoints.begin(), cutpoints.end(), successor) != cutpoints.end()) {
                    // if successor is a cutpoint, our path ends there
                    newPath.addBlock(successor);
                    newPath.setExecuteFinishBlock(false);
                    results->push_back(newPath);
                    continue;
                }

                if (newPath.containsBlock(successor)) {
                    // if successor is already in the path, this block has to be a cutpoint because there cannot be loops in
                    // a path. also we break out of of the successor iteration because we need to start new paths from this
                    // block
                    cutpoints.push_back(currentBlock);

                    newPathsToFinish.clear();

                    Path newPathFromCutpoint;
                    newPathFromCutpoint.addBlock(currentBlock);
                    newPathsToFinish.push_back(newPathFromCutpoint);

                    newPath.setExecuteFinishBlock(false);
                    results->push_back(newPath);
                    break;
                }

                newPath.addBlock(successor);
                newPathsToFinish.push_back(newPath);
            }

            for (const Path &newPathToFinish : newPathsToFinish) {
                pathsToFinish.push_back(newPathToFinish);
            }
        }
    }

}