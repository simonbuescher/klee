//
// Created by simon on 03.12.21.
//

#include <klee/Core/FunctionEvaluation.h>
#include <nlohmann/json.hpp>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <iostream>
#include <llvm/Bitcode/BitcodeWriter.h>
#include "CodeGenerator.h"
#include "ValueMap.h"
#include "ADDCodeGenerator.h"


CodeGenerator::CodeGenerator(CodeGeneratorOptions *options) {
    this->options = options;
    this->module = new llvm::Module("generated-llvm", *this->options->getContext());
}

void CodeGenerator::addFunction(llvm::Function *function) {
    this->module->getOrInsertFunction(function->getName(), function->getFunctionType());
}

void CodeGenerator::generateFunction(klee::FunctionEvaluation *functionEvaluation, nlohmann::json *adds) {
    llvm::LLVMContext *context = this->options->getContext();
    llvm::Function *sourceFunction = functionEvaluation->getFunction();

    llvm::FunctionCallee functionCallee = this->module->getOrInsertFunction(sourceFunction->getName(), sourceFunction->getFunctionType());
    auto *function = llvm::cast<llvm::Function>(functionCallee.getCallee());
    function->setCallingConv(llvm::CallingConv::C);

    ValueMap cutpointBlocks;
    ValueMap variables;

    llvm::BasicBlock *mainBlock = llvm::BasicBlock::Create(*context, "main", function);
    llvm::IRBuilder<> builder(mainBlock);

    this->storeArguments(function, &variables);
    this->createAllocas(functionEvaluation, &builder, &variables);
    this->createADDEntryBlocks(adds, function, &cutpointBlocks);
    this->createBranchToEntryBlock(sourceFunction, &builder, &cutpointBlocks);
    this->createReturnBlock(functionEvaluation, function, &variables, &cutpointBlocks);


    for (nlohmann::json add : *adds) {
        this->generateForADD(&add, function, &variables, &cutpointBlocks);
    }

    if (!this->verifyModule()) {
        std::cout << "verify module failed for function " << function->getName().str() << std::endl;
        this->writeModule();
        exit(EXIT_FAILURE);
    }
}

void CodeGenerator::writeModule() {
    std::error_code error;
    llvm::raw_fd_ostream moduleOutputFile(
            this->options->getOutputDirectory() + "/generated-llvm.bc",
            error
    );

    llvm::WriteBitcodeToFile(*this->module, moduleOutputFile);

    moduleOutputFile.flush();
}

void CodeGenerator::storeArguments(llvm::Function *function, ValueMap *variables) {
    int i = 0;
    for (llvm::Value &value : function->args()) {
        variables->store("arg" + std::to_string(i++), &value);
    }
}


void CodeGenerator::createAllocas(klee::FunctionEvaluation *functionEvaluation, llvm::IRBuilder<> *builder, ValueMap *variables) {
    for (const auto &variableTypePair : functionEvaluation->getVariableTypeMap()) {
        llvm::Value *allocaResult = builder->CreateAlloca(variableTypePair.second);
        variables->store(variableTypePair.first, allocaResult);
    }
}

void CodeGenerator::createBranchToEntryBlock(llvm::Function *sourceFunction, llvm::IRBuilder<> *builder, ValueMap *cutpointBlocks) {
    llvm::BasicBlock &entryBlock = sourceFunction->front();

    std::string blockName = entryBlock.getName();
    if (blockName.empty()) {
        blockName = std::to_string((long) &entryBlock);
    }

    builder->CreateBr(llvm::cast<llvm::BasicBlock>(cutpointBlocks->get(blockName)));
}

void CodeGenerator::createADDEntryBlocks(nlohmann::json *adds, llvm::Function *function, ValueMap *cutpointBlocks) {
    for (nlohmann::json add : *adds) {
        std::string cutpointName = add["start-cutpoint"];

        llvm::BasicBlock *block = llvm::BasicBlock::Create(*this->options->getContext(), cutpointName, function);
        cutpointBlocks->store(cutpointName, block);
    }
}

void CodeGenerator::createReturnBlock(klee::FunctionEvaluation *functionEvaluation, llvm::Function *function, ValueMap *variables, ValueMap *cutpointBlocks) {
    llvm::BasicBlock *returnCutpoint = llvm::BasicBlock::Create(*this->options->getContext(), "end", function);
    cutpointBlocks->store("end", returnCutpoint);

    llvm::IRBuilder<> endBlockBuilder(returnCutpoint);
    llvm::Value *functionResult = endBlockBuilder.CreateLoad(variables->get(functionEvaluation->getReturnValueName()));
    endBlockBuilder.CreateRet(functionResult);
}


void CodeGenerator::generateForADD(nlohmann::json *add, llvm::Function *function, ValueMap *variables, ValueMap *cutpointBlocks) {
    std::string cutpointName = (*add)["start-cutpoint"];
    nlohmann::json decisionDiagram = (*add)["decision-diagram"];

    auto *block = llvm::cast<llvm::BasicBlock>(cutpointBlocks->get(cutpointName));
    llvm::IRBuilder<> addBlockBuilder(block);

    ValueMap expressionCache;

    ADDCodeGeneratorOptions generatorOptions(
            this->options->getContext(),
            this->module,
            function,
            block,
            &addBlockBuilder,
            cutpointBlocks,
            variables,
            &expressionCache
    );
    ADDCodeGenerator generator(&decisionDiagram, &generatorOptions);
    generator.generate();
}

bool CodeGenerator::verifyModule() {
    std::error_code error;
    llvm::raw_fd_ostream errorStream(this->options->getOutputDirectory() + "/verify.txt", error);
    bool failed = llvm::verifyModule(*this->module, &errorStream);
    return !failed;
}