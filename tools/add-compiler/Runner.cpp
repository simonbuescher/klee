//
// Created by simon on 29.08.21.
//

#include <iostream>
#include <fstream>
#include <sys/stat.h>

#include <nlohmann/json.hpp>

#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/Verifier.h>

#include <klee/Support/ErrorHandling.h>
#include <klee/Support/FileHandling.h>
#include <klee/Support/ModuleUtil.h>
#include <klee/Core/ADDInterpreter.h>
#include "klee/Core/FunctionEvaluation.h"

#include "Runner.h"
#include "JsonPrinter.h"


Runner::Runner(int argc, char **argv, std::string outputDirectory) {
    this->argc = argc;
    this->argv = argv;
    this->outputDirectory = outputDirectory;
}

Runner::~Runner() = default;

void Runner::init() {
    this->parseArguments();
    this->prepareFiles();

    llvm::InitializeNativeTarget();

    std::string error;
    if (!klee::loadFile(this->inputFile, this->llvmContext, this->loadedModules, error)) {
        std::cout << "error loading program '"
                  << this->inputFile.c_str()
                  << "': "
                  << error.c_str()
                  << std::endl;
        exit(EXIT_FAILURE);
    }

    std::unique_ptr<llvm::Module> module(klee::linkModules(this->loadedModules, "", error));
    if (!module) {
        std::cout << "error loading program '"
                  << this->inputFile.c_str()
                  << "': "
                  << error.c_str()
                  << std::endl;
        exit(EXIT_FAILURE);
    }

    // Push the module as the first entry
    this->loadedModules.emplace_back(std::move(module));
}

void Runner::run() {
    auto *executor = klee::ADDInterpreter::create(this->llvmContext);

    klee::Interpreter::ModuleOptions moduleOptions(
            KLEE_LIB_PATH,
            this->mainFunction,
            "64_Debug+Asserts",
            false,
            false,
            false
    );
    llvm::Module *finalModule = executor->setModule(this->loadedModules, moduleOptions);
    llvm::Function *function = finalModule->getFunction(this->mainFunction);

    klee::FunctionEvaluation functionEvaluation(function);
    executor->runFunction(&functionEvaluation);

    this->outputPathResults(functionEvaluation);

    this->callJavaLib();

    nlohmann::json addJson;
    this->readADDs(&addJson);

    this->generateLLVMCode(functionEvaluation, &addJson);

    delete executor;
}

void Runner::parseArguments() {
    assert(argc == 3 && "invalid arguments");

    this->inputFile = this->argv[1];
    this->mainFunction = this->argv[2];
}

void Runner::prepareFiles() {
    mkdir(this->outputDirectory.c_str(), 0777);
}

void Runner::outputPathResults(klee::FunctionEvaluation &functionEvaluation) {
    JsonPrinter printer;

    for (auto &path : functionEvaluation.getPathList()) {
        printer.print(path);
    }

    printer.writeToFile(this->outputDirectory + "/out.json");
}

void Runner::callJavaLib() {
    char command[128];
    sprintf(command, "java -jar path-to-add.jar -i %s/out.json -o %s/adds.json",
            this->outputDirectory.c_str(), this->outputDirectory.c_str());

    FILE *commandOutput;
    commandOutput = popen(command, "r");

    if (commandOutput == nullptr) {
        std::cout << "error while calling java" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "java output:" << std::endl;

    char outputLine[1024];
    char *success;
    do {
        success = fgets(outputLine, 1024, commandOutput);
        std::cout << outputLine;
    } while (success != nullptr);
}

void Runner::readADDs(nlohmann::json *addJson) {
    std::ifstream addsInputFile(this->outputDirectory + "/adds.json");

    addsInputFile >> *addJson;
}

void Runner::generateLLVMCode(klee::FunctionEvaluation &functionEvaluation, nlohmann::json *addJson) {
    llvm::Module module("generated-llvm", this->llvmContext);

    llvm::FunctionCallee functionCallee = module.getOrInsertFunction(this->mainFunction,
                                                                     functionEvaluation.getFunction()->getFunctionType());

    auto *function = llvm::cast<llvm::Function>(functionCallee.getCallee());
    function->setCallingConv(llvm::CallingConv::C);

    std::map<std::string, llvm::BasicBlock *> cutpointBlockMap;
    std::map<std::string, llvm::Value *> variableMap;

    llvm::BasicBlock *entry = llvm::BasicBlock::Create(this->llvmContext, "main", function);

    for (nlohmann::json add : *addJson) {
        std::string cutpointName = add["start-cutpoint"];

        llvm::BasicBlock *block = llvm::BasicBlock::Create(this->llvmContext, cutpointName, function);
        cutpointBlockMap[cutpointName] = block;
    }

    llvm::IRBuilder<> builder(entry);
    this->generateLLVMCodePreparation(entry, &builder, &functionEvaluation, function, &variableMap, &cutpointBlockMap);

    llvm::BasicBlock *targetCutpoint = llvm::BasicBlock::Create(this->llvmContext, "end", function);
    cutpointBlockMap["end"] = targetCutpoint;

    llvm::IRBuilder<> endBlockBuilder(targetCutpoint);
    llvm::Value *functionResult = endBlockBuilder.CreateLoad(variableMap[functionEvaluation.getReturnValueName()]);
    endBlockBuilder.CreateRet(functionResult);

    for (nlohmann::json add : *addJson) {
        std::string cutpointName = add["start-cutpoint"];
        nlohmann::json decisionDiagram = add["decision-diagram"];

        llvm::BasicBlock *block = cutpointBlockMap[cutpointName];
        llvm::IRBuilder<> addBlockBuilder(block);

        this->generateLLVMCodeForDecisionDiagram(&decisionDiagram, block, &addBlockBuilder, function, &variableMap,
                                                 &cutpointBlockMap);
    }

    if (!this->verifyModule(module)) {
        std::cout << "first verify module failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    // this->optimizeFunction(function);

    if (!this->verifyModule(module)) {
        std::cout << "second verify module failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::error_code error;
    llvm::raw_fd_ostream moduleOutputFile(
            this->outputDirectory + "/generated-llvm.bc",
            error
    );

    llvm::WriteBitcodeToFile(module, moduleOutputFile);

    moduleOutputFile.flush();
}

void Runner::generateLLVMCodePreparation(llvm::BasicBlock *block, llvm::IRBuilder<> *blockBuilder,
                                         klee::FunctionEvaluation *functionEvaluation,
                                         llvm::Function *function,
                                         std::map<std::string, llvm::Value *> *variableMap,
                                         std::map<std::string, llvm::BasicBlock *> *cutpointBlockMap) {
    int i = 0;
    for (llvm::Value &value : function->args()) {
        (*variableMap)["arg" + std::to_string(i++)] = &value;
    }

    for (std::pair<std::string, llvm::Type *> variableTypePair : functionEvaluation->getVariableTypeMap()) {
        (*variableMap)[variableTypePair.first] = blockBuilder->CreateAlloca(variableTypePair.second);
    }

    blockBuilder->CreateBr(cutpointBlockMap->begin()->second);
}

void Runner::generateLLVMCodeForDecisionDiagram(nlohmann::json *ddJson, llvm::BasicBlock *block,
                                                llvm::IRBuilder<> *builder, llvm::Function *function,
                                                std::map<std::string, llvm::Value *> *variableMap,
                                                std::map<std::string, llvm::BasicBlock *> *cutpointBlockMap) {
    if (ddJson->contains("condition")) {
        nlohmann::json conditionJson = (*ddJson)["condition"];
        nlohmann::json trueChildJson = (*ddJson)["true-child"];
        nlohmann::json falseChildJson = (*ddJson)["false-child"];

        // create comparison
        llvm::Value *compareResult = this->generateLLVMCodeForExpressionTree(&conditionJson, block, builder,
                                                                             variableMap);

        // create true basic block and build llvm code
        llvm::BasicBlock *trueBlock = llvm::BasicBlock::Create(this->llvmContext,
                                                               block->getName() + ".then", function);

        // create false basic block and build llvm code
        llvm::BasicBlock *falseBlock = llvm::BasicBlock::Create(this->llvmContext,
                                                                block->getName() + ".else", function);

        // create branch statement to true and false blocks
        builder->CreateCondBr(compareResult, trueBlock, falseBlock);


        llvm::IRBuilder<> trueBlockBuilder(trueBlock);
        this->generateLLVMCodeForDecisionDiagram(&trueChildJson, trueBlock, &trueBlockBuilder, function, variableMap,
                                                 cutpointBlockMap);

        llvm::IRBuilder<> falseBlockBuilder(falseBlock);
        this->generateLLVMCodeForDecisionDiagram(&falseChildJson, falseBlock, &falseBlockBuilder, function, variableMap,
                                                 cutpointBlockMap);

    } else {
        std::string targetCutpointName = (*ddJson)["target-cutpoint"];
        nlohmann::json parallelAssignment = (*ddJson)["parallel-assignments"];

        std::map<std::string, llvm::Value *> results;
        // calculations for variables
        for (nlohmann::json assignment : parallelAssignment) {
            std::string targetVariableName = assignment["variable"];
            nlohmann::json expressionJson = assignment["expression"];

            llvm::Value *result = this->generateLLVMCodeForExpressionTree(&expressionJson, block, builder, variableMap);

            llvm::Type *expectedPointerType = (*variableMap)[targetVariableName]->getType();
            if (!expectedPointerType->isPointerTy()) {
                assert(false && "target variable is not pointer, can not create store instruction to concrete type");
            }

            if (result->getType() != expectedPointerType->getPointerElementType()) {
                // result->mutateType(expectedPointerType->getPointerElementType());
            }

            results[targetVariableName] = result;
        }

        for (std::pair<std::string, llvm::Value *> resultPair : results) {
            builder->CreateStore(resultPair.second, (*variableMap)[resultPair.first]);
        }

        llvm::BasicBlock *targetCutpoint;
        if (cutpointBlockMap->find(targetCutpointName) != cutpointBlockMap->end()) {
            targetCutpoint = (*cutpointBlockMap)[targetCutpointName];
        } else {
            targetCutpoint = (*cutpointBlockMap)["end"];
        }

        builder->CreateBr(targetCutpoint);
    }
}

llvm::Value *Runner::generateLLVMCodeForExpressionTree(nlohmann::json *expressionTree, llvm::BasicBlock *block,
                                                       llvm::IRBuilder<> *builder,
                                                       std::map<std::string, llvm::Value *> *variableMap) {
    std::string treeString = expressionTree->dump(4);

    if (expressionTree->is_object()) {
        nlohmann::json leftChild = (*expressionTree)["left-child"];
        nlohmann::json rightChild = (*expressionTree)["right-child"];
        std::string expressionOperator = (*expressionTree)["operator"];

        llvm::Value *leftResult = this->generateLLVMCodeForExpressionTree(&leftChild, block, builder, variableMap);
        llvm::Value *rightResult = this->generateLLVMCodeForExpressionTree(&rightChild, block, builder, variableMap);

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
    } else {
        std::string value = expressionTree->get<std::string>();

        if (variableMap->find(value) != variableMap->end()) {
            llvm::Value *variableValue = (*variableMap)[expressionTree->get<std::string>()];

            if (value[0] == 'a') {
                return variableValue;
            } else {
                return builder->CreateLoad(variableValue);
            }
        } else {
            long longValue = std::stol(value);
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(this->llvmContext), longValue, true);
        }
    }

    std::cout << "unknown json value:" << std::endl;
    std::cout << expressionTree->dump(4);

    exit(EXIT_FAILURE);
}


void Runner::optimizeFunction(llvm::Function *function) {
    for (auto &block : *function) {
        for (auto instructionIt = block.begin(), instructionEnd = block.end(); instructionIt != instructionEnd; instructionIt++) {
            llvm::Instruction &instruction = *instructionIt;
            if (instruction.getOpcode() != llvm::Instruction::Load) {
                continue;
            }

            for (auto secondIt = instructionIt; secondIt != instructionEnd; secondIt++) {
                llvm::Instruction &secondInstruction = *secondIt;
                if (secondInstruction.getOpcode() != llvm::Instruction::Load) {
                    continue;
                }

                if (secondInstruction.isIdenticalTo(&instruction)) {
                    secondInstruction.replaceAllUsesWith(&instruction);
                    secondInstruction.eraseFromParent();
                }
            }
        }
    }
}


bool Runner::verifyModule(llvm::Module &module) {
    std::error_code error;
    llvm::raw_fd_ostream errorStream(this->outputDirectory + "/verify.txt", error);
    bool failed = llvm::verifyModule(module, &errorStream);
    return !failed;
}