//
// Created by simon on 08.09.21.
//

#ifndef KLEE_FUNCTIONEVALUATION_H
#define KLEE_FUNCTIONEVALUATION_H


#include <map>

#include "Path.h"
#include "VariableStores.h"

#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <klee/Expr/Expr.h>


namespace klee {

    typedef std::vector<Path> PathList;


    class FunctionEvaluation {
    private:
        llvm::Function *function;

        VariableTypeMap variableTypeMap;
        PathList pathList;

    public:
        explicit FunctionEvaluation(llvm::Function *function);

        llvm::Function *getFunction() {
            return this->function;
        }

        VariableTypeMap &getVariableTypeMap() {
            return this->variableTypeMap;
        }

        PathList &getPathList() {
            return this->pathList;
        }

    private:
        void findVariableTypes();

        void findPaths();

    };

}

#endif //KLEE_FUNCTIONEVALUATION_H
