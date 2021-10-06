//
// Created by simon on 08.09.21.
//

#include <llvm/IR/CFG.h>

#include <klee/Core/FunctionEvaluation.h>
#include <klee/Core/Types.h>


namespace klee {
    FunctionEvaluation::FunctionEvaluation(llvm::Function *function) {
        this->function = function;

        this->findVariableTypes();
        this->findPaths();
    }

    void FunctionEvaluation::findVariableTypes() {
        for (size_t i = 0; i < this->function->arg_size(); i++) {
            std::string name = "arg" + std::to_string(i);
            // this->variableTypeMap.setVariableType(name, this->function->getArg(i)->getType());
        }

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
        std::vector<Path> pathsToFinish;
        std::vector<llvm::BasicBlock *> cutpoints;

        llvm::BasicBlock &entryBlock = this->function->getEntryBlock();

        Path startingPath;
        startingPath.addBlock(&entryBlock);

        pathsToFinish.push_back(startingPath);
        cutpoints.push_back(&entryBlock);

        while (!pathsToFinish.empty()) {
            Path currentPath = pathsToFinish.front();
            pathsToFinish.erase(pathsToFinish.begin());

            llvm::BasicBlock *currentBlock = currentPath.back();

            if (llvm::succ_empty(currentBlock)) {
                currentPath.setExecuteFinishBlock(true);
                this->pathList.push_back(currentPath);
                continue;
            }

            std::vector<Path> newPathsToFinish;
            for (auto successor : successors(currentBlock)) {
                Path newPath = Path(currentPath);

                if (std::find(cutpoints.begin(), cutpoints.end(), successor) != cutpoints.end()) {
                    // if successor is a cutpoint, our path ends there
                    newPath.addBlock(successor);
                    newPath.setExecuteFinishBlock(false);
                    this->pathList.push_back(newPath);
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
                    this->pathList.push_back(newPath);
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