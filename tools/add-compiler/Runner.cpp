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
#include "ADDCodeGenerator.h"


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

    ValueMap cutpointBlockMap;
    ValueMap variableMap;

    llvm::BasicBlock *entry = llvm::BasicBlock::Create(this->llvmContext, "main", function);

    for (nlohmann::json add : *addJson) {
        std::string cutpointName = add["start-cutpoint"];

        llvm::BasicBlock *block = llvm::BasicBlock::Create(this->llvmContext, cutpointName, function);
        cutpointBlockMap.store(cutpointName, block);
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

        ValueMap expressionCache;

        ADDCodeGeneratorOptions generatorOptions(
                &this->llvmContext,
                this->generatedModule,
                function,
                block,
                &addBlockBuilder,
                &cutpointBlockMap,
                &variableMap,
                &expressionCache
                );
        ADDCodeGenerator generator(&add, &generatorOptions);
        generator.generate();
    }

    if (!this->verifyModule(*this->generatedModule)) {
        std::cout << "verify module failed" << std::endl;
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

bool Runner::verifyModule(llvm::Module &module) {
    std::error_code error;
    llvm::raw_fd_ostream errorStream(this->outputDirectory + "/verify.txt", error);
    bool failed = llvm::verifyModule(module, &errorStream);
    return !failed;
}