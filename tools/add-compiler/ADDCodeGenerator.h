//
// Created by simon on 03.12.21.
//

#ifndef KLEE_ADDCODEGENERATOR_H
#define KLEE_ADDCODEGENERATOR_H


#include <nlohmann/json.hpp>
#include <llvm/IR/Value.h>
#include <llvm/IR/IRBuilder.h>
#include "ValueMap.h"
#include "ExpressionTreeCodeGenerator.h"


class ADDCodeGeneratorOptions {
private:
    llvm::LLVMContext *context;
    llvm::Module *module;
    llvm::Function *function;
    llvm::BasicBlock *block;
    llvm::IRBuilder<> *irBuilder;

    ValueMap *cutpointBlocks;
    ValueMap *variables;
    ValueMap *expressionCache;

public:
    ADDCodeGeneratorOptions(
            llvm::LLVMContext *context,
            llvm::Module *module,
            llvm::Function *function,
            llvm::BasicBlock *block,
            llvm::IRBuilder<> *irBuilder,
            ValueMap *cutpointBlocks,
            ValueMap *variables,
            ValueMap *expressionCache
    ) :
            context(context),
            module(module),
            function(function), block(block),
            irBuilder(irBuilder),
            cutpointBlocks(cutpointBlocks),
            variables(variables),
            expressionCache(expressionCache) {}

    llvm::LLVMContext *getContext() { return this->context; }

    llvm::Module *getModule() { return this->module; }

    llvm::Function *getFunction() { return this->function; }

    llvm::BasicBlock *getBlock() { return this->block; }

    llvm::IRBuilder<> *getBuilder() { return this->irBuilder; }

    ValueMap *getCutpointBlocks() { return this->cutpointBlocks; }

    ValueMap *getVariables() { return this->variables; }

    ValueMap *getCache() { return this->expressionCache; }
};


class ADDCodeGenerator {
private:
    nlohmann::json *add;
    ADDCodeGeneratorOptions *options;

public:
    ADDCodeGenerator(
            nlohmann::json *add,
            ADDCodeGeneratorOptions *options
    ) :
            add(add),
            options(options) {}

    void generate();

private:
    bool isCondition();

    void generateForCondition();

    void generateForParallelAssignment();

    llvm::BasicBlock *generateForChildADD(nlohmann::json *childADD, const std::string &blockNameAppendix);

    ExpressionTreeCodeGeneratorOptions *createExpressionGeneratorOptions();

    ADDCodeGeneratorOptions *createADDGeneratorOptions(
            llvm::BasicBlock *block,
            llvm::IRBuilder<> *builder,
            ValueMap *expressionCache
    );

    nlohmann::json getADDVariable(const std::string &key) { return (*this->add)[key]; };
};


#endif //KLEE_ADDCODEGENERATOR_H
