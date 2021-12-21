//
// Created by simon on 29.08.21.
//

#ifndef KLEE_RUNNER_H
#define KLEE_RUNNER_H


#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include <llvm/IR/IRBuilder.h>

#include <klee/Core/Interpreter.h>
#include <klee/Core/FunctionEvaluation.h>
#include "code-generation/CodeGenerator.h"


class Runner {
private:
    int argc;
    char **argv;

    std::string inputFile;
    std::string outputDirectory;

    llvm::LLVMContext llvmContext;
    std::vector<std::unique_ptr<llvm::Module>> loadedModules;
    llvm::SymbolTableList<llvm::Function> *functions;

    CodeGenerator *codeGenerator;

public:
    Runner(int argc, char **argv, std::string outputDirectory);
    ~Runner();

    void init();
    void run();

private:
    void parseArguments();
    void prepareRunDirectory();

    void writeSymbolicExecutionResultsToJson(klee::FunctionEvaluation *functionEvaluation, llvm::StringRef functionName);
    void callJavaLib(llvm::StringRef functionName);
    void readADDsFromJson(nlohmann::json *addJson, llvm::StringRef functionName);
    void generateCode(klee::FunctionEvaluation *functionEvaluation, nlohmann::json *addJson);
};

#endif //KLEE_RUNNER_H
