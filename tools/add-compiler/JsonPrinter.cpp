//
// Created by simon on 22.07.21.
//

#include <fstream>

#include <nlohmann/json.hpp>
#include <iostream>

#include "JsonPrinter.h"

void JsonPrinter::print(klee::Path *path) {
    klee::ConstraintSet constraints = path->getConstraints();

    std::string conditionString;

    if (constraints.empty()) {
        conditionString = "true";
    } else {
        // conditions are split in the resulting constraint set, join them together using AND
        auto condition = *constraints.begin();
        for (auto constraintIt = constraints.begin() + 1; constraintIt != constraints.end(); constraintIt++) {
            condition = klee::AndExpr::create(condition, *constraintIt);
        }
        printExpression(condition, &conditionString);
    }


    nlohmann::json parallelAssignmentsJson;
    for (std::pair<std::string, klee::ref<klee::Expr>> symbolicValue : path->getSymbolicValues()) {
        std::string expressionString;
        printExpression(symbolicValue.second, &expressionString);

        std::string variableName = symbolicValue.first;
        escapeVariableName(&variableName);

        parallelAssignmentsJson += {
                {"variable", variableName},
                {"expression", expressionString}
        };
    }

    // get the name of the starting cutpoint.
    // if it does not have a name, use its address
    std::string startCutpointName = path->front()->getName();
    if (startCutpointName.empty()) {
        startCutpointName = std::to_string((long)path->front());
    }

    // get the name of the target cutpoint.
    // if a path is just 1 block long, we need to branch to the ending block at the end of code generation, so default to that.
    std::string targetCutpointName = "end";
    if (path->size() > 1) {
        targetCutpointName = path->back()->getName();
        if (targetCutpointName.empty()) {
            targetCutpointName = std::to_string((long)path->back());
        }
    }

    this->jsonObject += {
            {"start-cutpoint", startCutpointName},
            {"target-cutpoint", targetCutpointName},
            {"condition", conditionString},
            {"parallel-assignments", parallelAssignmentsJson}
    };
}

void JsonPrinter::printExpression(klee::ref<klee::Expr> expression, std::string *resultString) {
    switch (expression->getKind()) {
        case klee::Expr::Kind::Constant: {
            auto *constExpr = llvm::dyn_cast<klee::ConstantExpr>(expression);

            // all values in LLVM IR are unsigned 64 bit integers.
            // get the whole value without loosing information by passing the maximum possible limit.
            uint64_t value = constExpr->getLimitedValue(UINT64_MAX);


            if (expression->getWidth() == 1) {
                // 1 bit wide numbers are boolean values and need to be passed to ADD generation accordingly
                *resultString = value == 0 ? "false" : "true";
            } else {
                *resultString = std::to_string(value);
            }
            break;
        }
        case klee::Expr::Kind::Add: {
            printBinaryExpression("+", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Sub: {
            printBinaryExpression("-", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Mul: {
            printBinaryExpression("*", expression, resultString);
            break;
        }
        case klee::Expr::Kind::SDiv: {
            printBinaryExpression("/", expression, resultString);
            break;
        }
        case klee::Expr::Kind::UDiv: {
            printBinaryExpression("u/", expression, resultString);
            break;
        }
        case klee::Expr::Kind::SRem:{
            printBinaryExpression("%", expression, resultString);
            break;
        }
        case klee::Expr::Kind::URem: {
            printBinaryExpression("u%", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Eq:{
            auto *binaryExpression = llvm::dyn_cast<klee::BinaryExpr>(expression);

            if (binaryExpression->left->isZero() && binaryExpression->left->getWidth() == 1) {
                // !some_boolean_value is represented as 0 == some_boolean_value in LLVM IR.
                // write it as a normal negation for ADD generation.
                std::string rightResult;
                printExpression(binaryExpression->right, &rightResult);

                *resultString = "!" + rightResult;
            }
            else {
                printBinaryExpression("==", expression, resultString);
            }
            break;
        }
        case klee::Expr::Kind::Slt: {
            printBinaryExpression("<", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Ult: {
            printBinaryExpression("u<", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Sle:{
            printBinaryExpression("<=", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Ule: {
            printBinaryExpression("u<=", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Sgt: {
            printBinaryExpression(">", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Ugt: {
            printBinaryExpression("u>", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Sge: {
            printBinaryExpression(">=", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Uge: {
            printBinaryExpression("u>=", expression, resultString);
            break;
        }
        case klee::Expr::Kind::And: {
            printBinaryExpression("&", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Or: {
            printBinaryExpression("|", expression, resultString);
            break;
        }
        case klee::Expr::Kind::LShr: {
            printBinaryExpression(">>", expression, resultString);
            break;
        }
        case klee::Expr::Kind::AShr: {
            printBinaryExpression("u>>", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Shl: {
            printBinaryExpression("<<", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Concat: {
            // in the current state of the implementation, all concat expressions represent reads from memory of a single variable.
            // these represent symbolic variables.
            // we can just look at the right child, which should be a read expression, to get the variable name.
            printExpression(expression->getKid(1), resultString);
            break;
        }
        case klee::Expr::Kind::Read: {
            auto *readExpression = llvm::dyn_cast<klee::ReadExpr>(expression);

            // read the name of the variable out of the read expression
            std::string variableName = readExpression->updates.root->getName();
            escapeVariableName(&variableName);

            *resultString = variableName;

            break;
        }
        case klee::Expr::Kind::CastKindFirst:
        case klee::Expr::Kind::CastKindLast: {
            // casts can be ignored in our json representation as they just store type information that we dont need.
            // just print the contained expression.
            printExpression(expression->getKid(0), resultString);
            break;
        }
        case klee::Expr::Kind::Call: {
            auto *callExpression = llvm::dyn_cast<klee::CallExpr>(expression);

            std::string functionCall = "";
            functionCall += callExpression->functionName.str();
            functionCall += "(";

            for (unsigned int i = 0; i < callExpression->getNumKids(); i++) {
                if (i != 0) functionCall += ", ";

                std::string argumentString;
                printExpression(callExpression->getKid(i), &argumentString);
                functionCall += argumentString;
            }

            functionCall += ")";

            *resultString = functionCall;
            break;
        }
        default: {
            std::cout << "Trying to print an expression for which the print is not implemented." << std::endl;
            exit(EXIT_FAILURE);
        }
    }
}


void JsonPrinter::printBinaryExpression(std::string op, klee::ref<klee::Expr> expression, std::string *result) {
    std::string resultLeft, resultRight;

    klee::ref<klee::BinaryExpr> binaryExpression = llvm::dyn_cast<klee::BinaryExpr>(expression);

    printExpression(binaryExpression->left, &resultLeft);
    printExpression(binaryExpression->right, &resultRight);

    *result = "(" + resultLeft + " " + op + " " + resultRight + ")";
}

void JsonPrinter::escapeVariableName(std::string *variableName) {
    // variables are usually called %1, %2 and so forth.
    // escape this to be named var1, var2, ...
    size_t start_pos = variableName->find('%');
    if(start_pos != std::string::npos) {
        variableName->replace(start_pos, 1, "var");
    }
}

void JsonPrinter::writeToFile(std::string outputFile) {
    std::ofstream outputFileStream;
    outputFileStream.open(outputFile);

    outputFileStream << this->jsonObject.dump(4);

    outputFileStream.close();
}