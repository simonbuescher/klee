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

        std::string returnValueName;

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

        std::string &getReturnValueName() {
            return this->returnValueName;
        };

        bool setReturnValueName(std::string name) {
            if (this->returnValueName.empty()) {
                this->returnValueName = name;
                return true;
            } else {
                return this->returnValueName.compare(name) == 0;
            }
        }

    private:
        void findVariableTypes();

        void findPaths();

    };

}

#endif //KLEE_FUNCTIONEVALUATION_H
