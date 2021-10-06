//
// Created by simon on 22.07.21.
//

#ifndef KLEE_JSONPRINTER_H
#define KLEE_JSONPRINTER_H


#include <klee/ADT/Ref.h>
#include "klee/Core/Path.h"
#include <klee/Expr/Expr.h>

#include <nlohmann/json.hpp>


class JsonPrinter {
private:
    nlohmann::json jsonObject;

public:
    void print(klee::Path &path);

    void printExpression(klee::ref<klee::Expr> expression, std::string *resultString);

    void printBinaryExpression(std::string op, klee::ref<klee::Expr> expression, std::string *result);

    void escapeVariableName(std::string *variableName);

    void writeToFile(std::string outputFile);
};


#endif //KLEE_JSONPRINTER_H
