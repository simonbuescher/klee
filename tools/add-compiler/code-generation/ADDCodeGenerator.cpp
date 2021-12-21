//
// Created by simon on 03.12.21.
//

#include <iostream>
#include "ADDCodeGenerator.h"
#include "ExpressionTreeCodeGenerator.h"


void ADDCodeGenerator::generate() {
    if (this->rootNodeIsCondition()) {
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

    // create calculations for all right hand sides of the assignments, but do not store the results
    ValueMap results;
    for (nlohmann::json assignment : parallelAssignment) {
        std::string targetVariableName = assignment["variable"];
        nlohmann::json expressionJson = assignment["expression"];

        if (targetVariableName == expressionJson) {
            // skip self assignments (var1 = var1)
            continue;
        }

        // generate code for assignment
        ExpressionTreeCodeGeneratorOptions *generatorOptions = this->createExpressionGeneratorOptions();
        ExpressionTreeCodeGenerator generator(&expressionJson, generatorOptions);
        llvm::Value *result = generator.generate();
        delete generatorOptions;

        // target variable needs to be an symbolic pointer variable, so we can store the results there.
        // if its a concrete value, something went wrong.
        llvm::Type *expectedPointerType = variables->get(targetVariableName)->getType();
        if (!expectedPointerType->isPointerTy()) {
            std::cout << "Target variable " << targetVariableName << " is not a symbolic variable, can not store results." << std::endl;
            exit(EXIT_FAILURE);
        }

        results.store(targetVariableName, result);
    }

    // create the store instructions that store the new results in the symbolic variables
    for (const auto& resultPair : results) {
        builder->CreateStore(resultPair.second, variables->get(resultPair.first));
    }

    // create the branch to the next ADD / the end of the function.
    std::string cutpointName = cutpointBlocks->contains(targetCutpointName) ? targetCutpointName : "end";
    auto *targetCutpoint = llvm::cast<llvm::BasicBlock>(cutpointBlocks->get(cutpointName));
    builder->CreateBr(targetCutpoint);
}

void ADDCodeGenerator::generateForCondition() {
    llvm::IRBuilder<> *builder = this->options->getBuilder();

    nlohmann::json condition = this->getADDVariable("condition");
    nlohmann::json trueChild = this->getADDVariable("true-child");
    nlohmann::json falseChild = this->getADDVariable("false-child");

    // generate code for the condition
    ExpressionTreeCodeGeneratorOptions *conditionOptions = this->createExpressionGeneratorOptions();
    ExpressionTreeCodeGenerator conditionGenerator(&condition, conditionOptions);
    llvm::Value *compareResult = conditionGenerator.generate();
    delete conditionOptions;

    // generate code for left and right children.
    // these are ADDs themselves, and code generation can be handled recursively.
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

    // create a new source BB for child.
    llvm::BasicBlock *childBlock = llvm::BasicBlock::Create(*context, block->getName() + "." + blockNameAppendix,
                                                            function);
    llvm::IRBuilder<> childBuilder(childBlock);

    // create a copy of the current expression cache as the generation forks at this point
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

bool ADDCodeGenerator::rootNodeIsCondition() {
    return this->add->contains("condition");
}


