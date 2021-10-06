//
// Created by simon on 04.10.21.
//

#ifndef KLEE_ADDINTERPRETER_H
#define KLEE_ADDINTERPRETER_H

#include "Interpreter.h"
#include "FunctionEvaluation.h"

namespace klee {

    class ExecutionState;

    class ADDInterpreter {
    public:
        virtual ~ADDInterpreter() {}

        virtual void runFunction(FunctionEvaluation *functionEvaluation) = 0;

        virtual llvm::Module *setModule(
                std::vector<std::unique_ptr<llvm::Module>> &modules,
                const Interpreter::ModuleOptions &opts
        ) = 0;

        static ADDInterpreter *create(llvm::LLVMContext &context);
    };

}

#endif //KLEE_ADDINTERPRETER_H
