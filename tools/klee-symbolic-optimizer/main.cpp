//
// Created by simon on 29.06.21.
//

#include <iostream>
#include <vector>
#include <klee/Core/Interpreter.h>
#include <klee/Expr/Expr.h>
#include <klee/Support/ErrorHandling.h>
#include <klee/Support/FileHandling.h>
#include <klee/Support/ModuleUtil.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/LLVMContext.h>
#include <iomanip>
#include <fstream>

#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"

#include "nlohmann/json.hpp"
#include "JsonPrinter.h"


std::string OUT_DIR = "./test-out/";
std::string INPUT_DIR = "../klee-test-programs/";

std::string KLEE_LIB_PATH = "/home/simon/Libraries/klee/build/runtime/lib";

class MyKleeHandler : public klee::InterpreterHandler {
private:
    llvm::LLVMContext *context;
    klee::Interpreter *interpreter;
    llvm::Function *function;

    JsonPrinter *jsonPrinter;

    std::unique_ptr<llvm::raw_fd_ostream> infoStream;

    std::uint32_t pathsCompleted;
    std::uint32_t pathsExplored;

    int pathNum = 0;

public:
    MyKleeHandler(JsonPrinter *printer) {
        jsonPrinter = printer;

        infoStream = openOutputFile("info");

        pathsCompleted = 0;
        pathsExplored = 0;
    }

    ~MyKleeHandler() override { infoStream->close(); }

    void setContext(llvm::LLVMContext *c) { context = c; }

    void setInterpreter(klee::Interpreter *i) { interpreter = i; }

    void setFunction(llvm::Function *f) { function = f; }

    llvm::raw_ostream &getInfoStream() const override { return *infoStream; }

    std::string getOutputFilename(const std::string &filename) override {
        std::string path = OUT_DIR;
        return path + filename;
    }

    std::unique_ptr<llvm::raw_fd_ostream>
    openOutputFile(const std::string &filename) override {
        std::string Error;
        std::string path = getOutputFilename(filename);
        auto f = klee::klee_open_output_file(path, Error);
        if (!f) {
            klee::klee_warning(
                    "error opening file \"%s\".  KLEE may have run out of file "
                    "descriptors: try to increase the maximum number of open file "
                    "descriptors by using ulimit (%s).",
                    path.c_str(), Error.c_str());
            return nullptr;
        }
        return f;
    }

    void incPathsCompleted() override { pathsCompleted++; }

    void incPathsExplored(std::uint32_t num = 1) override {
        pathsExplored += num;
    }

    void processTestCase(const klee::ExecutionState &state, const char *err,
                         const char *suffix) override {
        std::cout << "MyKleeHandler.processTestCase called, this should not happen in symbolic execution" << std::endl;
    }

    void processPathExecution(klee::Path &path) override {
        std::cout << "PATH FINISHED: [" << path.getPathRepr() << "] ["
                  << (path.shouldExecuteFinishBlock() ? "execute last" : "dont execute last") << "]" << std::endl;

        jsonPrinter->print(path);

        /*
        llvm::Module *myModule = makeLLVMModule(path);

        std::error_code errorCode;
        llvm::raw_fd_ostream fileStream(std::to_string(pathNum++) + ".bc", errorCode, llvm::sys::fs::F_None);
        WriteBitcodeToFile(*myModule, fileStream);
        fileStream.flush();

        delete myModule;
        */
    }

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

};

void cleanOutputDir() {
    std::remove((OUT_DIR + "assembly.ll").c_str());
    std::remove((OUT_DIR + "info").c_str());
    std::remove((OUT_DIR + "run.istats").c_str());
    std::remove((OUT_DIR + "run.stats").c_str());
    std::remove((OUT_DIR + "run.stats-shm").c_str());
    std::remove((OUT_DIR + "run.stats-wal").c_str());
}


void loadModules(const std::string &inputFile, llvm::LLVMContext &ctx,
                 std::vector<std::unique_ptr<llvm::Module>> &loadedModules, std::string &errorMsg) {
    if (!klee::loadFile(inputFile, ctx, loadedModules, errorMsg)) {
        std::cout << "error loading program '" << inputFile.c_str()
                  << "': " << errorMsg.c_str() << std::endl;
        exit(EXIT_FAILURE);
    }

    std::unique_ptr<llvm::Module> module(klee::linkModules(loadedModules, "", errorMsg));
    if (!module) {
        std::cout << "error loading program '" << inputFile.c_str()
                  << "': " << errorMsg.c_str() << std::endl;
        exit(EXIT_FAILURE);
    }

    // Push the module as the first entry
    loadedModules.emplace_back(std::move(module));
}


int main(int argc, char **argv) {
    assert(argc == 3 && "wrong arguments");

    std::string inputFile = INPUT_DIR + argv[1];
    std::string functionName = argv[2];

    cleanOutputDir();

    std::string errorMsg;

    llvm::LLVMContext ctx;
    klee::Interpreter *interpreter;
    std::vector<std::unique_ptr<llvm::Module>> loadedModules;


    llvm::InitializeNativeTarget();
    loadModules(inputFile, ctx, loadedModules, errorMsg);

    nlohmann::json jsonOut;
    JsonPrinter *printer = new JsonPrinter(&jsonOut);
    MyKleeHandler *handler = new MyKleeHandler(printer);
    klee::Interpreter::InterpreterOptions interpreterOptions;

    interpreter = klee::Interpreter::create(ctx, interpreterOptions, handler);

    klee::Interpreter::ModuleOptions moduleOptions(KLEE_LIB_PATH, functionName, "64_Debug+Asserts",
                                                   false, true, true);
    llvm::Module *finalModule = interpreter->setModule(loadedModules, moduleOptions);
    llvm::Function *function = finalModule->getFunction(functionName);

    handler->setContext(&ctx);
    handler->setInterpreter(interpreter);
    handler->setFunction(function);


    interpreter->runFunctionAsSymbolic(function);

    std::ofstream outFile;
    outFile.open("test-out/out.json");
    outFile << std::setw(4) << jsonOut;
    outFile.close();

    delete handler;
    return 0;
}
