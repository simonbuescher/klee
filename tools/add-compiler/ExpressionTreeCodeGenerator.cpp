//
// Created by simon on 03.12.21.
//

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Constants.h>
#include <iostream>
#include "ExpressionTreeCodeGenerator.h"


llvm::Value *ExpressionTreeCodeGenerator::generate() {
    ValueMap *cache = this->options->getExpressionCache();
    std::string cacheKey = this->expressionTree->dump();

    if (cache->contains(cacheKey)) {
        return cache->get(cacheKey);
    }

    llvm::Value *result;
    bool cacheResult = true;
    if (this->isInnerNode()) {
        result = this->generateForInnerNode();
    } else if (this->isFunctionCall()) {
        result = this->generateForFunctionCall();
    } else {
        result = this->generateForLeafNode(&cacheResult);
    }

    if (cacheResult) {
        cache->store(cacheKey, result);
    }
    return result;
}

llvm::Value *ExpressionTreeCodeGenerator::generateForLeafNode(bool *cacheResult) const {
    ValueMap *variables = this->options->getVariables();
    llvm::IRBuilder<> *builder = this->options->getBuilder();

    std::string value = this->expressionTree->get<std::string>();

    if (variables->contains(value)) {
        llvm::Value *variableValue = variables->get(value);

        if (value[0] == 'a') {
            return variableValue;
        } else {
            return builder->CreateLoad(variableValue);
        }
    } else {
        *cacheResult = false;
        uint64_t longValue = std::stoul(value);
        return llvm::ConstantInt::get(llvm::Type::getInt64Ty(*this->options->getContext()), longValue, true);
    }
}

llvm::Value *ExpressionTreeCodeGenerator::generateForFunctionCall() {
    llvm::Module *module = this->options->getModule();
    llvm::IRBuilder<> *builder = this->options->getBuilder();

    std::string functionName = this->getTreeVariable("function-name");
    nlohmann::json argumentTrees = this->getTreeVariable("function-arguments");

    std::vector<llvm::Value *> arguments;
    for (auto argJson : argumentTrees) {
        ExpressionTreeCodeGenerator treeGenerator(&argJson, this->options);
        llvm::Value *argumentResult = treeGenerator.generate();
        arguments.push_back(argumentResult);
    }

    llvm::Function *targetFunction = module->getFunction(functionName);
    return builder->CreateCall(targetFunction, arguments);
}

llvm::Value *ExpressionTreeCodeGenerator::generateForInnerNode() {
    llvm::IRBuilder<> *builder = this->options->getBuilder();

    std::string expressionOperator = this->getTreeVariable("type");
    nlohmann::json leftChild = this->getTreeVariable("left-child");
    nlohmann::json rightChild = this->getTreeVariable("right-child");

    ExpressionTreeCodeGenerator leftGenerator(&leftChild, this->options);
    llvm::Value *leftResult = leftGenerator.generate();

    ExpressionTreeCodeGenerator rightGenerator(&rightChild, this->options);
    llvm::Value *rightResult = rightGenerator.generate();

    if (leftResult->getType() != rightResult->getType()) {
        if (llvm::isa<llvm::Constant>(leftResult)) {
            leftResult->mutateType(rightResult->getType());
        } else if (llvm::isa<llvm::Constant>(rightResult)) {
            rightResult->mutateType(leftResult->getType());
        }
    }

    if (expressionOperator == "+") {
        return builder->CreateAdd(leftResult, rightResult);
    } else if (expressionOperator == "-") {
        return builder->CreateSub(leftResult, rightResult);
    } else if (expressionOperator == "*") {
        return builder->CreateMul(leftResult, rightResult);
    } else if (expressionOperator == "/") {
        return builder->CreateSDiv(leftResult, rightResult);
    } else if (expressionOperator == "%") {
        return builder->CreateSRem(leftResult, rightResult);
    } else if (expressionOperator == "<<") {
        return builder->CreateShl(leftResult, rightResult);
    } else if (expressionOperator == ">>") {
        return builder->CreateAShr(leftResult, rightResult);
    } else if (expressionOperator == "&") {
        return builder->CreateAnd(leftResult, rightResult);
    } else if (expressionOperator == "|") {
        return builder->CreateOr(leftResult, rightResult);
    } else if (expressionOperator == "=") {
        return builder->CreateICmpEQ(leftResult, rightResult);
    } else if (expressionOperator == "<") {
        return builder->CreateICmpSLT(leftResult, rightResult);
    } else if (expressionOperator == "<=") {
        return builder->CreateICmpSLE(leftResult, rightResult);
    } else if (expressionOperator == ">") {
        return builder->CreateICmpSGT(leftResult, rightResult);
    } else if (expressionOperator == ">=") {
        return builder->CreateICmpSGE(leftResult, rightResult);
    }

    std::cout << "unknown json value: " << this->expressionTree->dump(4) << std::endl;
    exit(EXIT_FAILURE);
}

bool ExpressionTreeCodeGenerator::isInnerNode() {
    return this->expressionTree->is_object() && (*this->expressionTree)["type"] != "function-call";
}

bool ExpressionTreeCodeGenerator::isFunctionCall() {
    return this->expressionTree->is_object() && (*this->expressionTree)["type"] == "function-call";
}
