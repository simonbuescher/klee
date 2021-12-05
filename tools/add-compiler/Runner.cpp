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

Runner::~Runner() {
    delete this->codeGenerator;
};

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
    this->functions = &inputModule->getFunctionList();

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

    auto *options = new CodeGeneratorOptions(&this->llvmContext, this->outputDirectory);
    this->codeGenerator = new CodeGenerator(options);
}

void Runner::run() {
    // insert all function declarations into new module previous to code generation
    for (llvm::Function &function : *this->functions) {
        this->codeGenerator->addFunction(&function);
    }

    auto *executor = klee::ADDInterpreter::create(this->llvmContext);
    klee::Interpreter::ModuleOptions moduleOptions(
            KLEE_LIB_PATH,
            this->functions->front().getName(),
            "64_Debug+Asserts",
            false,
            false,
            false
    );
    llvm::Module *finalModule = executor->setModule(this->loadedModules, moduleOptions);

    for (llvm::Function &function : *this->functions) {
        if (function.isDeclaration()) {
            continue;
        }

        llvm::StringRef functionName = function.getName();

        std::cout << "evaluating function \"" + functionName.str() + "\" \n" << std::endl;

        klee::FunctionEvaluation functionEvaluation(&function);
        executor->runFunction(&functionEvaluation);

        this->outputPathResults(&functionEvaluation, functionName);

        this->callJavaLib(functionName);

        nlohmann::json addJson;
        this->readADDs(&addJson, functionName);

        this->generateCode(&functionEvaluation, &addJson);
    }

    delete executor;

    this->codeGenerator->writeModule();
}

void Runner::parseArguments() {
    assert(argc == 2 && "invalid arguments");

    this->inputFile = this->argv[1];
}

void Runner::prepareFiles() {
    mkdir(this->outputDirectory.c_str(), 0777);
}

void Runner::outputPathResults(klee::FunctionEvaluation *functionEvaluation, llvm::StringRef functionName) {
    JsonPrinter printer;

    for (auto *path : functionEvaluation->getPathList()) {
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

void Runner::generateCode(klee::FunctionEvaluation *functionEvaluation, nlohmann::json *addJson) {
    this->codeGenerator->generateFunction(functionEvaluation, addJson);
}