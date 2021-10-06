//
// Created by simon on 03.07.21.
//

#ifndef KLEE_PATH_H
#define KLEE_PATH_H


#include <llvm/IR/BasicBlock.h>

#include <klee/Expr/Constraints.h>
#include <klee/Core/Types.h>


namespace klee {

    class Path {
    private:
        std::vector<llvm::BasicBlock *> blocks;
        bool executeFinishBlock;

        ConstraintSet constraints;
        VariableExpressionMap symbolicValues;

        std::string repr;

    public:
        Path();

        Path(Path const &path);

        void setExecuteFinishBlock(bool value);

        bool shouldExecuteFinishBlock();

        void addBlock(llvm::BasicBlock *block);

        bool containsBlock(llvm::BasicBlock *block);

        void setConstraints(ConstraintSet constraintSet);

        ConstraintSet getConstraints();

        VariableExpressionMap &getSymbolicValues();

        std::string getPathRepr();

        llvm::BasicBlock *front();

        llvm::BasicBlock *back();

        std::vector<llvm::BasicBlock *>::iterator begin();

        std::vector<llvm::BasicBlock *>::iterator end();
    };

}

#endif //KLEE_PATH_H
