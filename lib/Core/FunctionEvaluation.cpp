//
// Created by simon on 08.09.21.
//

#include <llvm/IR/CFG.h>

#include <klee/Core/FunctionEvaluation.h>
#include <klee/Core/Types.h>
#include <list>


namespace klee {
    FunctionEvaluation::FunctionEvaluation(llvm::Function *function) {
        this->function = function;

        this->findVariableTypes();
        this->findPaths();
        // this->extendPaths();
    }

    void FunctionEvaluation::findVariableTypes() {
        int variableNumber = 0;
        for (llvm::BasicBlock &basicBlock : *function) {
            for (llvm::Instruction &instruction : basicBlock) {
                if (instruction.getOpcode() == llvm::Instruction::Alloca) {
                    std::string name = "var" + std::to_string(variableNumber);
                    this->variableTypeMap.setVariableType(name, instruction.getType()->getContainedType(0));

                    variableNumber++;
                }
            }
        }
    }

    void FunctionEvaluation::findPaths() {
        std::vector<Path *> pathsToFinish;
        std::vector<llvm::BasicBlock *> cutpoints;

        llvm::BasicBlock &entryBlock = this->function->getEntryBlock();

        Path *startingPath = new Path();
        startingPath->addBlock(&entryBlock);

        pathsToFinish.push_back(startingPath);
        cutpoints.push_back(&entryBlock);

        while (!pathsToFinish.empty()) {
            Path *currentPath = pathsToFinish.front();
            pathsToFinish.erase(pathsToFinish.begin());

            llvm::BasicBlock *currentBlock = currentPath->back();

            if (llvm::succ_empty(currentBlock)) {
                currentPath->setExecuteFinishBlock(true);
                this->pathList.push_back(currentPath);
                continue;
            }

            std::vector<Path *> newPathsToFinish;
            for (auto successor : successors(currentBlock)) {
                Path *newPath = new Path();
                currentPath->copy(newPath);

                if (std::find(cutpoints.begin(), cutpoints.end(), successor) != cutpoints.end()) {
                    // if successor is a cutpoint, our path ends there
                    newPath->addBlock(successor);
                    newPath->setExecuteFinishBlock(false);
                    this->pathList.push_back(newPath);
                    continue;
                }

                if (newPath->containsBlock(successor)) {
                    // if successor is already in the path, this block has to be a cutpoint because there cannot be loops in
                    // a path. also we break out of of the successor iteration because we need to start new paths from this
                    // block
                    cutpoints.push_back(currentBlock);

                    newPathsToFinish.clear();

                    Path *newPathFromCutpoint = new Path();
                    newPathFromCutpoint->addBlock(currentBlock);
                    newPathsToFinish.push_back(newPathFromCutpoint);

                    newPath->setExecuteFinishBlock(false);
                    this->pathList.push_back(newPath);

                    // we also need to terminate each path that currently ends in this block
                    std::vector<Path *> removeList;
                    for (Path *path : pathsToFinish) {
                        if (path->back() == currentBlock) {
                            removeList.push_back(path);
                            path->setExecuteFinishBlock(false);
                            this->pathList.push_back(path);
                        }
                    }

                    for (Path *path : removeList) {
                        pathsToFinish.erase(std::find(pathsToFinish.begin(), pathsToFinish.end(), path));
                    }

                    break;
                }

                newPath->addBlock(successor);
                newPathsToFinish.push_back(newPath);
            }

            for (Path *newPathToFinish : newPathsToFinish) {
                pathsToFinish.push_back(newPathToFinish);
            }
        }
    }

    void FunctionEvaluation::extendPaths() {
        std::vector<Path *> originalPaths(this->pathList);

        for (int i = 0; i < 8; i++) {
            std::vector<Path *> newPaths;
            std::vector<Path *> removePaths;

            for (auto *path : this->pathList) {
                if (path->front() == path->back() && path->size() > 1) {
                    for (auto *other : originalPaths) {
                        if (other->front() == path->back()) {
                            Path *newPath = new Path();
                            for (auto &block : *path) {
                                newPath->addBlock(block);
                            }
                            for (auto blockIt = ++other->begin(); blockIt != other->end(); blockIt++) {
                                newPath->addBlock(*blockIt);
                            }

                            newPaths.push_back(newPath);
                        }
                    }
                    removePaths.push_back(path);
                }
            }

            for (auto *path : removePaths) {
                this->pathList.erase(std::remove(this->pathList.begin(), this->pathList.end(), path),
                                     this->pathList.end());
            }

            for (auto *path : newPaths) {
                this->pathList.push_back(path);
            }
        }
    }
}