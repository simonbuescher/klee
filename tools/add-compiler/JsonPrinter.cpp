//
// Created by simon on 22.07.21.
//

#include <fstream>

#include <nlohmann/json.hpp>

#include "JsonPrinter.h"

void JsonPrinter::print(klee::Path *path) {
    klee::ConstraintSet constraints = path->getConstraints();

    std::string conditionString;

    if (constraints.empty()) {
        conditionString = "true";
    } else {
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

    std::string startCutpointName = path->front()->getName();
    if (startCutpointName.empty()) {
        startCutpointName = std::to_string((long)path->front());
    }

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

            uint64_t value = constExpr->getLimitedValue(UINT64_MAX);

            if (expression->getWidth() == 1) {
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
        case klee::Expr::Kind::SDiv:
        case klee::Expr::Kind::UDiv: {
            printBinaryExpression("/", expression, resultString);
            break;
        }
        case klee::Expr::Kind::SRem:
        case klee::Expr::Kind::URem: {
            printBinaryExpression("%", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Eq:{
            auto *binaryExpression = llvm::dyn_cast<klee::BinaryExpr>(expression);
            if (binaryExpression->left->isZero() && binaryExpression->left->getWidth() == 1) {
                std::string rightResult;
                printExpression(binaryExpression->right, &rightResult);

                *resultString = "!" + rightResult;
            }
            else {
                printBinaryExpression("==", expression, resultString);
            }
            break;
        }
        case klee::Expr::Kind::Slt:
        case klee::Expr::Kind::Ult: {
            printBinaryExpression("<", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Sle:
        case klee::Expr::Kind::Ule: {
            printBinaryExpression("<=", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Sgt:
        case klee::Expr::Kind::Ugt: {
            printBinaryExpression(">", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Sge:
        case klee::Expr::Kind::Uge: {
            printBinaryExpression(">=", expression, resultString);
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
        case klee::Expr::Kind::LShr:
        case klee::Expr::Kind::AShr: {
            printBinaryExpression(">>", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Shl: {
            printBinaryExpression("<<", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Concat: {
            printExpression(expression->getKid(1), resultString);
            break;
        }
        case klee::Expr::Kind::Read: {
            auto *readExpression = llvm::dyn_cast<klee::ReadExpr>(expression);
            std::string variableName = readExpression->updates.root->getName();
            escapeVariableName(&variableName);

            *resultString = variableName;

            break;
        }
        case klee::Expr::Kind::CastKindFirst:
        case klee::Expr::Kind::CastKindLast: {
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
            assert(false && "not implemented print case");
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