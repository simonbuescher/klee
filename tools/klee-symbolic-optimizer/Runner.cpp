//
// Created by simon on 29.08.21.
//

#include <iomanip>
#include <iostream>
#include <fstream>
#include <sys/stat.h>

#include "JsonPrinter.h"
#include "Runner.h"

#include <nlohmann/json.hpp>

#include <klee/Support/ErrorHandling.h>
#include <klee/Support/FileHandling.h>
#include <klee/Support/ModuleUtil.h>
#include <llvm/Support/TargetSelect.h>


class MyKleeHandler : public klee::InterpreterHandler {
private:
    JsonPrinter *jsonPrinter;

    std::string outputDirectory;

    std::unique_ptr<llvm::raw_fd_ostream> infoStream;

    std::uint32_t pathsCompleted;
    std::uint32_t pathsExplored;

public:
    MyKleeHandler(JsonPrinter *printer, std::string outputDirectory) {
        jsonPrinter = printer;

        this->outputDirectory = outputDirectory;

        infoStream = this->openOutputFile("info");

        pathsCompleted = 0;
        pathsExplored = 0;
    }

    ~MyKleeHandler() override { infoStream->close(); }

    llvm::raw_ostream &getInfoStream() const override { return *infoStream; }

    std::string getOutputFilename(const std::string &filename) override {
        std::string path = this->outputDirectory;
        return path + "/klee/" + filename;
    }

    std::unique_ptr<llvm::raw_fd_ostream> openOutputFile(const std::string &filename) override {
        std::string error;
        std::string path = this->getOutputFilename(filename);

        auto file = klee::klee_open_output_file(path, error);
        if (!file) {
            klee::klee_warning(
                    "error opening file \"%s\".  KLEE may have run out of file "
                    "descriptors: try to increase the maximum number of open file "
                    "descriptors by using ulimit (%s).",
                    path.c_str(), error.c_str()
            );
            return nullptr;
        }

        return file;
    }

    void incPathsCompleted() override { pathsCompleted++; }

    void incPathsExplored(std::uint32_t num = 1) override {
        pathsExplored += num;
    }

    void processTestCase(const klee::ExecutionState &state, const char *err,
                         const char *suffix) override {
        std::cout << "MyKleeHandler.processTestCase called, this should not happen in symbolic execution" << std::endl;
        exit(EXIT_FAILURE);
    }

    void processPathExecution(klee::Path &path) override {
        std::cout << "PATH FINISHED: ["
                  << path.getPathRepr()
                  << "] ["
                  << (path.shouldExecuteFinishBlock() ? "execute last" : "dont execute last")
                  << "]"
                  << std::endl;

        jsonPrinter->print(path);
    }

};


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
    nlohmann::json outputJson;

    auto *printer = new JsonPrinter(&outputJson);
    auto *handler = new MyKleeHandler(printer, this->outputDirectory);

    klee::Interpreter::InterpreterOptions interpreterOptions;
    this->kleeInterpreter = klee::Interpreter::create(this->llvmContext, interpreterOptions, handler);

    klee::Interpreter::ModuleOptions moduleOptions(
            KLEE_LIB_PATH,
            this->mainFunction,
            "64_Debug+Asserts",
            false,
            true,
            true
    );
    llvm::Module *finalModule = this->kleeInterpreter->setModule(this->loadedModules, moduleOptions);
    llvm::Function *function = finalModule->getFunction(this->mainFunction);

    // run paths
    this->kleeInterpreter->runFunctionAsSymbolic(function);

    // output results to out dir
    this->outputPathResults(&outputJson);

    // call java lib to generate adds
    this->callJavaLib();

    // read adds
    nlohmann::json addJson;
    this->readADDs(&addJson);

    // generate llvm code
    this->generateLLVMCode(&addJson);
}

void Runner::parseArguments() {
    assert(argc == 3 && "invalid arguments");

    this->inputFile = this->argv[1];
    this->mainFunction = this->argv[2];
}

void Runner::prepareFiles() {
    mkdir(this->outputDirectory.c_str(), 0777);
    mkdir((this->outputDirectory + "/klee").c_str(), 0777);

    std::remove((this->outputDirectory + "/assembly.ll").c_str());
    std::remove((this->outputDirectory + "/info").c_str());
    std::remove((this->outputDirectory + "/run.istats").c_str());
    std::remove((this->outputDirectory + "/run.stats").c_str());
    std::remove((this->outputDirectory + "/run.stats-shm").c_str());
    std::remove((this->outputDirectory + "/run.stats-wal").c_str());
}

void Runner::outputPathResults(nlohmann::json *outputJson) {
    std::ofstream outputFileStream;
    outputFileStream.open(this->outputDirectory + "/out.json");
    outputFileStream << std::setw(4) << *outputJson;
    outputFileStream.close();
}

void Runner::callJavaLib() {
    FILE *commandOutput;

    commandOutput = popen("java -jar path-to-add.jar -i run-out/out.json -o run-out/adds.json", "r");

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

}

void Runner::generateLLVMCode(nlohmann::json *addJson) {
    /*
    llvm::Module *myModule = makeLLVMModule(path);

    std::error_code errorCode;
    llvm::raw_fd_ostream fileStream(std::to_string(pathNum++) + ".bc", errorCode, llvm::sys::fs::F_None);
    WriteBitcodeToFile(*myModule, fileStream);
    fileStream.flush();

    delete myModule;
    */

/*
llvm::Module *makeLLVMModule(klee::Path &path) {
    llvm::Module *module = new llvm::Module("test", *context);

    llvm::FunctionCallee functionCallee = module->getOrInsertFunction("mult",
                                                                      llvm::Type::getInt32Ty(*context),
                                                                      llvm::Type::getInt32Ty(*context));

    llvm::Function *pathFunction = cast<llvm::Function>(functionCallee.getCallee());
    pathFunction->setCallingConv(llvm::CallingConv::C);

    llvm::BasicBlock *block = llvm::BasicBlock::Create(*context, "entry", pathFunction);
    llvm::IRBuilder<> builder(block);


    std::vector<llvm::Value *> values;
    for (auto symbolic : path.getSymbolicValues()) {
        llvm::StringRef valueName = symbolic.first;
        auto expression = symbolic.second;

        llvm::Value *alloc = builder.CreateAlloca(llvm::Type::getInt32Ty(*context));
        values.push_back(alloc);
    }

    std::vector<llvm::Value *> results;
    for (auto symbolic : path.getSymbolicValues()) {
        llvm::StringRef valueName = symbolic.first;
        auto expression = symbolic.second;

        llvm::Value *result = evaluateExpression(expression, &builder, &values);

        results.push_back(result);
    }

    builder.CreateRet(results[1]);

    return module;
}

llvm::Value *evaluateExpression(klee::ref<klee::Expr> expression, llvm::IRBuilder<> *builder,
                                std::vector<llvm::Value *> *values) {
    switch (expression->getKind()) {
        case klee::Expr::Kind::Add:
            return createBinOp(expression, llvm::Instruction::Add, builder, values);
        case klee::Expr::Kind::Mul:
            return createBinOp(expression, llvm::Instruction::Add, builder, values);
        case klee::Expr::Kind::Read:
            // todo figure out how to get the right value
            return values->at(1);
        case klee::Expr::Kind::Concat:
            // todo figure out together with read, we may just need on of the reads to identify the right value
            return evaluateExpression(expression->getKid(0), builder, values);
        case klee::Expr::Kind::Constant:
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), cast<klee::ConstantExpr>(expression)->getAPValue());
        default:
            assert(false && "unhandled kind in evaluateExpression");
            // return nullptr to satisfy ClangTidy
            return nullptr;
    }
}

llvm::Value *
createBinOp(klee::ref<klee::Expr> expression, llvm::Instruction::BinaryOps instruction, llvm::IRBuilder<> *builder,
            std::vector<llvm::Value *> *values) {
    llvm::Value *left = evaluateExpression(expression->getKid(0), builder, values);
    llvm::Value *right = evaluateExpression(expression->getKid(1), builder, values);

    llvm::Value *result = builder->CreateBinOp(instruction, left, right);
    return result;
}
*/
}