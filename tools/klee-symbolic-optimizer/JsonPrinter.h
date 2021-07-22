//
// Created by simon on 22.07.21.
//

#ifndef KLEE_JSONPRINTER_H
#define KLEE_JSONPRINTER_H


#include <klee/Expr/Expr.h>
#include <klee/ADT/Ref.h>
#include "../../lib/Core/Path.h"

class JsonPrinter {
private:
    nlohmann::json *jsonObject;
public:
    explicit JsonPrinter(nlohmann::json *json) {
        jsonObject = json;
    }

    void print(klee::Path &path);

    void printExpression(klee::ref<klee::Expr> expression, std::string *resultString);

    void printBinaryExpression(std::string op, klee::ref<klee::Expr> expression, std::string *result);

    void escapeVariableName(std::string *variableName);
};


#endif //KLEE_JSONPRINTER_H
