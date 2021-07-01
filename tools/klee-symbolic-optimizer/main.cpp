//
// Created by simon on 29.06.21.
//

#include <iostream>
#include <klee/Core/Interpreter.h>
#include <klee/Expr/Expr.h>
#include <klee/Support/ErrorHandling.h>
#include <klee/Support/FileHandling.h>
#include <klee/Support/ModuleUtil.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/TargetSelect.h>
#include <vector>

std::string OUT_DIR = "./test-out/";
std::string INPUT_DIR = "../klee-test-programs/";

std::string KLEE_LIB_PATH = "/home/simon/Libraries/klee/build/runtime/lib";

class MyKleeHandler : public klee::InterpreterHandler {
private:
    klee::Interpreter *interpreter;

    std::unique_ptr<llvm::raw_fd_ostream> infoStream;

    std::uint32_t pathsCompleted;
    std::uint32_t pathsExplored;

public:
    MyKleeHandler() {
        infoStream = openOutputFile("info");

        pathsCompleted = 0;
        pathsExplored = 0;
    }

    ~MyKleeHandler() override { infoStream->close(); }

    void setInterpreter(klee::Interpreter *i) { interpreter = i; }

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
        std::cout << "MyKleeHandler.processTestCase called !!!" << std::endl;

        std::cout << "terminated state: " << std::endl;
        std::cout << "constraints: " << std::endl;

        for (klee::ref<klee::Expr> constraint : state.constraints) {
            llvm::SmallString<1024> constraintString;
            llvm::raw_svector_ostream constraintStream(constraintString);
            constraint->print(constraintStream);

            std::cout << constraintString.c_str() << std::endl;
        }

        std::cout << "expressions: " << std::endl;

        std::vector<klee::ref<klee::Expr>> expressions;
        interpreter->getReturnValues(state, expressions);
        for (klee::ref<klee::Expr> expression : expressions) {
            llvm::SmallString<1024> expressionString;
            llvm::raw_svector_ostream expressionStream(expressionString);
            expression->print(expressionStream);

            std::cout << expressionString.c_str() << std::endl;
        }

        std::cout << std::endl << std::endl;
    }
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

int main() {
    std::string inputFile = INPUT_DIR + "loop_free.bc";
    std::string functionName = "simple_fib";

    cleanOutputDir();

    std::string errorMsg;

    llvm::LLVMContext ctx;
    klee::Interpreter *interpreter;
    std::vector<std::unique_ptr<llvm::Module>> loadedModules;


    llvm::InitializeNativeTarget();
    loadModules(inputFile, ctx, loadedModules, errorMsg);

    MyKleeHandler *handler = new MyKleeHandler();
    klee::Interpreter::InterpreterOptions interpreterOptions;

    interpreter = klee::Interpreter::create(ctx, interpreterOptions, handler);
    handler->setInterpreter(interpreter);

    klee::Interpreter::ModuleOptions moduleOptions(KLEE_LIB_PATH, functionName, "64_Debug+Asserts",
                                                   false, true, true);
    llvm::Module *finalModule = interpreter->setModule(loadedModules, moduleOptions);
    llvm::Function *function = finalModule->getFunction(functionName);

    interpreter->runFunctionAsSymbolic(function);

    delete handler;
    return 0;
}