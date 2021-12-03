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

    std::unique_ptr<llvm::Module> &inputModule = this->loadedModules[0];
    for (auto &function: inputModule->getFunctionList()) {
        this->allFunctions[function.getName()] = &function;
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

    // create our output module
    this->createLLVMModule();
}

void Runner::run() {
    // insert all function declarations into new module previous to code generation
    for (auto nameFunctionPair : this->allFunctions) {
        this->generatedModule->getOrInsertFunction(nameFunctionPair.first, nameFunctionPair.second->getFunctionType());
    }

    auto *executor = klee::ADDInterpreter::create(this->llvmContext);
    klee::Interpreter::ModuleOptions moduleOptions(
            KLEE_LIB_PATH,
            this->allFunctions.begin()->first,
            "64_Debug+Asserts",
            false,
            false,
            false
    );
    llvm::Module *finalModule = executor->setModule(this->loadedModules, moduleOptions);

    for (auto nameFunctionPair : this->allFunctions) {
        llvm::Function *function = finalModule->getFunction(nameFunctionPair.first);
        if (function->isDeclaration()) {
            continue;
        }

        std::cout << "evaluating function \"" + nameFunctionPair.first.str() + "\" \n" << std::endl;

        klee::FunctionEvaluation functionEvaluation(function);
        executor->runFunction(&functionEvaluation);

        this->outputPathResults(functionEvaluation, nameFunctionPair.first);

        this->callJavaLib(nameFunctionPair.first);

        nlohmann::json addJson;
        this->readADDs(&addJson, nameFunctionPair.first);

        this->generateLLVMCode(functionEvaluation, &addJson);
    }

    delete executor;

    std::error_code error;
    llvm::raw_fd_ostream moduleOutputFile(
            this->outputDirectory + "/generated-llvm.bc",
            error
    );

    llvm::WriteBitcodeToFile(*this->generatedModule, moduleOutputFile);

    moduleOutputFile.flush();
}

void Runner::parseArguments() {
    assert(argc == 2 && "invalid arguments");

    this->inputFile = this->argv[1];
}

void Runner::prepareFiles() {
    mkdir(this->outputDirectory.c_str(), 0777);
}

void Runner::outputPathResults(klee::FunctionEvaluation &functionEvaluation, llvm::StringRef functionName) {
    JsonPrinter printer;

    for (auto *path : functionEvaluation.getPathList()) {
        printer.print(path);
    }

    printer.writeToFile(this->outputDirectory + "/" + functionName.str() + ".symex.json");
}

void Runner::callJavaLib(llvm::StringRef functionName) {
    char command[128];
    sprintf(command, "java -jar path-to-add.jar -i %s/%s.symex.json -o %s/%s.adds.json",
            this->outputDirectory.c_str(), functionName.str().c_str(), this->outputDirectory.c_str(), functionName.str().c_str());

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

void Runner::readADDs(nlohmann::json *addJson, llvm::StringRef functionName) {
    std::ifstream addsInputFile(this->outputDirectory + "/" + functionName.str() + ".adds.json");

    addsInputFile >> *addJson;
}


void Runner::createLLVMModule() {
    this->generatedModule = new llvm::Module("generated-llvm", this->llvmContext);
}


void Runner::generateLLVMCode(klee::FunctionEvaluation &functionEvaluation, nlohmann::json *addJson) {
    llvm::FunctionCallee functionCallee = this->generatedModule->getOrInsertFunction(functionEvaluation.getFunction()->getName(),
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

        std::map<std::string, llvm::Value *> expressionCache;

        this->generateLLVMCodeForDecisionDiagram(&decisionDiagram, block, &addBlockBuilder, function, &variableMap,
                                                 &cutpointBlockMap, &expressionCache);
    }

    if (!this->verifyModule(*this->generatedModule)) {
        std::cout << "first verify module failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    // this->optimizeFunction(function);

    if (!this->verifyModule(*this->generatedModule)) {
        std::cout << "second verify module failed" << std::endl;
        exit(EXIT_FAILURE);
    }
}

void Runner::generateLLVMCodePreparation(llvm::BasicBlock *block,
                                         llvm::IRBuilder<> *blockBuilder,
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

    llvm::BasicBlock &originalFrontBasicBlock = functionEvaluation->getFunction()->front();
    std::string startCutpointName = originalFrontBasicBlock.getName();
    if (startCutpointName.empty()) {
        startCutpointName = std::to_string((long)&originalFrontBasicBlock);
    }
    blockBuilder->CreateBr((*cutpointBlockMap)[startCutpointName]);
}

void Runner::generateLLVMCodeForDecisionDiagram(nlohmann::json *ddJson,
                                                llvm::BasicBlock *block,
                                                llvm::IRBuilder<> *builder, llvm::Function *function,
                                                std::map<std::string, llvm::Value *> *variableMap,
                                                std::map<std::string, llvm::BasicBlock *> *cutpointBlockMap,
                                                std::map<std::string, llvm::Value *> *expressionCache) {

}

llvm::Value *Runner::generateLLVMCodeForExpressionTree(nlohmann::json *expressionTree,
                                                       llvm::BasicBlock *block,
                                                       llvm::IRBuilder<> *builder,
                                                       std::map<std::string, llvm::Value *> *variableMap,
                                                       std::map<std::string, llvm::Value *> *expressionCache) {
    std::string treeString = expressionTree->dump();

    if (expressionCache->find(treeString) != expressionCache->end()) {
        std::cout << "CACHE HIT ON '" << treeString << "' IN BB '" << block->getName().str() << "'" << std::endl;
        return (*expressionCache)[treeString];
    }

    llvm::Value *result = nullptr;
    if (expressionTree->is_object()) {
        std::string expressionType = (*expressionTree)["type"];

        if (expressionType == "function-call") {
            std::string functionName = (*expressionTree)["function-name"];
            nlohmann::json argumentTrees = (*expressionTree)["function-arguments"];

            std::vector<llvm::Value *> arguments;
            for (auto argJson : argumentTrees) {
                llvm::Value *argumentResult = this->generateLLVMCodeForExpressionTree(&argJson, block, builder, variableMap, expressionCache);
                arguments.push_back(argumentResult);
            }

            llvm::Function *sourceFunction = this->allFunctions[functionName];
            llvm::FunctionCallee calledFunction = this->generatedModule->getOrInsertFunction(functionName, sourceFunction->getFunctionType());
            result = builder->CreateCall(calledFunction, arguments);

        } else {
            std::string expressionOperator = expressionType;
            nlohmann::json leftChild = (*expressionTree)["left-child"];
            nlohmann::json rightChild = (*expressionTree)["right-child"];

            llvm::Value *leftResult = this->generateLLVMCodeForExpressionTree(&leftChild, block, builder, variableMap, expressionCache);
            llvm::Value *rightResult = this->generateLLVMCodeForExpressionTree(&rightChild, block, builder, variableMap, expressionCache);

            if (leftResult->getType() != rightResult->getType()) {
                if (llvm::isa<llvm::Constant>(leftResult)) {
                    leftResult->mutateType(rightResult->getType());
                } else if (llvm::isa<llvm::Constant>(rightResult)) {
                    rightResult->mutateType(leftResult->getType());
                }
            }

            if (expressionOperator == "+") {
                result = builder->CreateAdd(leftResult, rightResult);
            } else if (expressionOperator == "-") {
                result = builder->CreateSub(leftResult, rightResult);
            } else if (expressionOperator == "*") {
                result = builder->CreateMul(leftResult, rightResult);
            } else if (expressionOperator == "/") {
                result = builder->CreateSDiv(leftResult, rightResult);
            } else if (expressionOperator == "%") {
                result = builder->CreateSRem(leftResult, rightResult);
            } else if (expressionOperator == "<<") {
                result = builder->CreateShl(leftResult, rightResult);
            } else if (expressionOperator == ">>") {
                result = builder->CreateAShr(leftResult, rightResult);
            } else if (expressionOperator == "&") {
                result = builder->CreateAnd(leftResult, rightResult);
            } else if (expressionOperator == "|") {
                result = builder->CreateOr(leftResult, rightResult);
            } else if (expressionOperator == "=") {
                result = builder->CreateICmpEQ(leftResult, rightResult);
            } else if (expressionOperator == "<") {
                result = builder->CreateICmpSLT(leftResult, rightResult);
            } else if (expressionOperator == "<=") {
                result = builder->CreateICmpSLE(leftResult, rightResult);
            } else if (expressionOperator == ">") {
                result = builder->CreateICmpSGT(leftResult, rightResult);
            } else if (expressionOperator == ">=") {
                result = builder->CreateICmpSGE(leftResult, rightResult);
            }

        }
    } else {
        std::string value = expressionTree->get<std::string>();

        if (variableMap->find(value) != variableMap->end()) {
            llvm::Value *variableValue = (*variableMap)[expressionTree->get<std::string>()];

            if (value[0] == 'a') {
                result = variableValue;
            } else {
                result = builder->CreateLoad(variableValue);
            }
        } else {
            uint64_t longValue = std::stoul(value);
            // immediately return here so we dont cache constants, as that does not make sense
            return llvm::ConstantInt::get(llvm::Type::getInt64Ty(this->llvmContext), longValue, true);
        }
    }

    if (result == nullptr) {
        std::cout << "unknown json value:" << std::endl;
        std::cout << treeString;
        exit(EXIT_FAILURE);
    }

    (*expressionCache)[treeString] = result;
    return result;
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