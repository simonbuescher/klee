//
// Created by simon on 29.08.21.
//

#ifndef KLEE_RUNNER_H
#define KLEE_RUNNER_H


#include <memory>
#include <vector>
#include <klee/Core/Interpreter.h>
#include <nlohmann/json.hpp>


#define KLEE_LIB_PATH "/home/simon/Libraries/klee/build/runtime/lib"


class Runner {
private:
    int argc;
    char **argv;

    std::string outputDirectory;

    std::string inputFile;
    std::string mainFunction;

    llvm::LLVMContext llvmContext;
    klee::Interpreter *kleeInterpreter;

    std::vector<std::unique_ptr<llvm::Module>> loadedModules;

public:
    Runner(int argc, char **argv, std::string outputDirectory);
    ~Runner();

    void init();
    void run();

    void parseArguments();
    void prepareFiles();

    void outputPathResults(nlohmann::json *outputJson);
    void callJavaLib();
    void readADDs(nlohmann::json *addJson);
    void generateLLVMCode(nlohmann::json *addJson);
};

#endif //KLEE_RUNNER_H
