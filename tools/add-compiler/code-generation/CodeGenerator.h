//
// Created by simon on 03.12.21.
//

#ifndef KLEE_CODEGENERATOR_H
#define KLEE_CODEGENERATOR_H


#include <llvm/IR/Module.h>
#include "ValueMap.h"

class CodeGeneratorOptions {
private:
    llvm::LLVMContext *context;
    std::string outputDirectory;

public:
    CodeGeneratorOptions(llvm::LLVMContext *context, std::string outputDirectory)
            : context(context), outputDirectory(outputDirectory) {}

    llvm::LLVMContext *getContext() { return this->context; }

    std::string getOutputDirectory() { return this->outputDirectory; }
};


class CodeGenerator {
private:
    llvm::Module *module;

    CodeGeneratorOptions *options;

public:
    explicit CodeGenerator(CodeGeneratorOptions *options);

    void addFunction(llvm::Function *function);

    void generateFunction(klee::FunctionEvaluation *functionEvaluation, nlohmann::json *adds);

    void writeModule();

private:
    void storeArguments(llvm::Function *function, ValueMap *variables);

    void createAllocas(klee::FunctionEvaluation *functionEvaluation, llvm::IRBuilder<> *builder, ValueMap *variables);

    void createBranchToEntryBlock(llvm::Function *sourceFunction, llvm::IRBuilder<> *builder, ValueMap *cutpointBlocks);

    void createADDEntryBlocks(nlohmann::json *adds, llvm::Function *function, ValueMap *cutpointBlocks);

    void createReturnBlock(klee::FunctionEvaluation *functionEvaluation, llvm::Function *function, ValueMap *variables, ValueMap *cutpointBlocks);

    void generateForADD(nlohmann::json *add, llvm::Function *function, ValueMap *variables, ValueMap *cutpointBlocks);

    bool verifyModule();
};


#endif //KLEE_CODEGENERATOR_H
