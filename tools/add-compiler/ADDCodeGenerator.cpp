//
// Created by simon on 03.12.21.
//

#include <iostream>
#include "ADDCodeGenerator.h"
#include "ExpressionTreeCodeGenerator.h"


void ADDCodeGenerator::generate() {
    std::string tree = this->add->dump();
    if (this->isCondition()) {
        this->generateForCondition();
    } else {
        this->generateForParallelAssignment();
    }
}

void ADDCodeGenerator::generateForParallelAssignment() {
    llvm::IRBuilder<> *builder = this->options->getBuilder();
    ValueMap *cutpointBlocks = this->options->getCutpointBlocks();
    ValueMap *variables = this->options->getVariables();

    std::string targetCutpointName = this->getADDVariable("target-cutpoint");
    nlohmann::json parallelAssignment = this->getADDVariable("parallel-assignments");

    ValueMap results;
    // calculations for variables
    for (nlohmann::json assignment : parallelAssignment) {
        std::string targetVariableName = assignment["variable"];
        nlohmann::json expressionJson = assignment["expression"];

        if (targetVariableName == expressionJson) {
            // skip self assignments
            continue;
        }

        ExpressionTreeCodeGeneratorOptions *generatorOptions = this->createExpressionGeneratorOptions();
        ExpressionTreeCodeGenerator generator(&expressionJson, generatorOptions);
        llvm::Value *result = generator.generate();
        delete generatorOptions;

        llvm::Type *expectedPointerType = variables->get(targetVariableName)->getType();
        if (!expectedPointerType->isPointerTy()) {
            assert(false && "target variable is not pointer, can not create store instruction to concrete type");
        }

        results.store(targetVariableName, result);
    }

    for (const auto& resultPair : results) {
        builder->CreateStore(resultPair.second, variables->get(resultPair.first));
    }

    std::string cutpointName = cutpointBlocks->contains(targetCutpointName) ? targetCutpointName : "end";
    auto *targetCutpoint = llvm::cast<llvm::BasicBlock>(cutpointBlocks->get(cutpointName));
    builder->CreateBr(targetCutpoint);
}

void ADDCodeGenerator::generateForCondition() {
    llvm::IRBuilder<> *builder = this->options->getBuilder();

    nlohmann::json condition = this->getADDVariable("condition");
    nlohmann::json trueChild = this->getADDVariable("true-child");
    nlohmann::json falseChild = this->getADDVariable("false-child");

    // create comparison
    ExpressionTreeCodeGeneratorOptions *conditionOptions = this->createExpressionGeneratorOptions();
    ExpressionTreeCodeGenerator conditionGenerator(&condition, conditionOptions);
    llvm::Value *compareResult = conditionGenerator.generate();
    delete conditionOptions;

    // generate code for children
    llvm::BasicBlock *trueBlock = this->generateForChildADD(&trueChild, "then");
    llvm::BasicBlock *falseBlock = this->generateForChildADD(&falseChild, "else");

    // create branch statement to true and false blocks
    builder->CreateCondBr(compareResult, trueBlock, falseBlock);
}

llvm::BasicBlock *
ADDCodeGenerator::generateForChildADD(nlohmann::json *childADD, const std::string &blockNameAppendix) {
    llvm::LLVMContext *context = this->options->getContext();
    llvm::Function *function = this->options->getFunction();
    llvm::BasicBlock *block = this->options->getBlock();

    llvm::BasicBlock *childBlock = llvm::BasicBlock::Create(*context, block->getName() + "." + blockNameAppendix,
                                                            function);
    llvm::IRBuilder<> childBuilder(childBlock);
    ValueMap childCache(*this->options->getCache());
    ADDCodeGeneratorOptions *childOptions = this->createADDGeneratorOptions(childBlock, &childBuilder, &childCache);

    ADDCodeGenerator generator(childADD, childOptions);
    generator.generate();

    delete childOptions;

    return childBlock;
}

ExpressionTreeCodeGeneratorOptions *ADDCodeGenerator::createExpressionGeneratorOptions() {
    return new ExpressionTreeCodeGeneratorOptions(
            this->options->getContext(),
            this->options->getModule(),
            this->options->getBuilder(),
            this->options->getVariables(),
            this->options->getCache()
    );
}

ADDCodeGeneratorOptions *ADDCodeGenerator::createADDGeneratorOptions(
        llvm::BasicBlock *block,
        llvm::IRBuilder<> *builder,
        ValueMap *expressionCache
) {
    return new ADDCodeGeneratorOptions(
            this->options->getContext(),
            this->options->getModule(),
            this->options->getFunction(),
            block,
            builder,
            this->options->getCutpointBlocks(),
            this->options->getVariables(),
            expressionCache
    );
}

bool ADDCodeGenerator::isCondition() {
    return this->add->contains("condition");
}


