//
// Created by simon on 03.12.21.
//

#ifndef KLEE_EXPRESSIONTREECODEGENERATOR_H
#define KLEE_EXPRESSIONTREECODEGENERATOR_H


#include <llvm/IR/Value.h>
#include <nlohmann/json.hpp>
#include <llvm/IR/IRBuilder.h>
#include "ValueMap.h"


class ExpressionTreeCodeGeneratorOptions {
private:
    llvm::LLVMContext *context;
    llvm::Module *module;
    llvm::IRBuilder<> *irBuilder;

    ValueMap *variables;
    ValueMap *expressionCache;

public:
    ExpressionTreeCodeGeneratorOptions(
            llvm::LLVMContext *context,
            llvm::Module *module,
            llvm::IRBuilder<> *irBuilder,
            ValueMap *variables,
            ValueMap *expressionCache
    ) :
            context(context),
            module(module),
            irBuilder(irBuilder),
            variables(variables),
            expressionCache(expressionCache) {}

    llvm::LLVMContext *getContext() { return this->context; }

    llvm::Module *getModule() { return this->module; }

    llvm::IRBuilder<> *getBuilder() { return this->irBuilder; }

    ValueMap *getVariables() { return this->variables; }

    ValueMap *getExpressionCache() { return this->expressionCache; }
};


class ExpressionTreeCodeGenerator {
private:
    nlohmann::json *expressionTree;
    ExpressionTreeCodeGeneratorOptions *options;

public:
    ExpressionTreeCodeGenerator(
            nlohmann::json *expressionTree,
            ExpressionTreeCodeGeneratorOptions *options
    ) :
            expressionTree(expressionTree),
            options(options) {}

    llvm::Value *generate();

private:
    bool isInnerNode();

    bool isFunctionCall();

    llvm::Value *generateForInnerNode();

    llvm::Value *generateForFunctionCall();

    llvm::Value *generateForLeafNode() const;

    nlohmann::json getTreeVariable(const std::string &key) { return (*this->expressionTree)[key]; };
};


#endif //KLEE_EXPRESSIONTREECODEGENERATOR_H
