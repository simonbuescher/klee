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


#define KLEE_LIB_PATH "/home/simon/Libraries/klee/build/runtime/lib"


class Runner {
private:
    int argc;
    char **argv;

    std::string outputDirectory;

    std::string inputFile;

    llvm::LLVMContext llvmContext;

    std::vector<std::unique_ptr<llvm::Module>> loadedModules;
    std::map<llvm::StringRef, llvm::Function *> allFunctions;

    llvm::Module *generatedModule;

public:
    Runner(int argc, char **argv, std::string outputDirectory);
    ~Runner();

    void init();
    void run();

private:
    void parseArguments();
    void prepareFiles();

    void createLLVMModule();

    void outputPathResults(klee::FunctionEvaluation &functionEvaluation, llvm::StringRef functionName);
    void callJavaLib(llvm::StringRef functionName);
    void readADDs(nlohmann::json *addJson, llvm::StringRef functionName);
    void generateLLVMCode(klee::FunctionEvaluation &functionEvaluation, nlohmann::json *addJson);
    void generateLLVMCodePreparation(llvm::BasicBlock *block, llvm::IRBuilder<> *blockBuilder, klee::FunctionEvaluation *functionEvaluation, llvm::Function *function, std::map<std::string, llvm::Value *> *variableMap, std::map<std::string, llvm::BasicBlock *> *cutpointBlockMap);
    void generateLLVMCodeForDecisionDiagram(nlohmann::json *ddJson, llvm::BasicBlock *block, llvm::IRBuilder<> *builder, llvm::Function *function, std::map<std::string, llvm::Value *> *variableMap, std::map<std::string, llvm::BasicBlock *> *cutpointBlockMap);
    llvm::Value * generateLLVMCodeForExpressionTree(nlohmann::json *expressionTree, llvm::BasicBlock *block, llvm::IRBuilder<> *builder, std::map<std::string, llvm::Value *> *variableMap);
    void optimizeFunction(llvm::Function *function);
    bool verifyModule(llvm::Module &module);
};

#endif //KLEE_RUNNER_H
