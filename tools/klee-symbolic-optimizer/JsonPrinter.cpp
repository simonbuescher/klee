//
// Created by simon on 22.07.21.
//

#include <nlohmann/json.hpp>
#include "JsonPrinter.h"

void JsonPrinter::print(klee::Path &path) {
    klee::ConstraintSet constraints = path.getConstraints();
    auto condition = *constraints.begin();
    for (auto constraintIt = constraints.begin() + 1; constraintIt != constraints.end(); constraintIt++) {
        condition = klee::AndExpr::create(condition, *constraintIt);
    }

    std::string conditionString;
    printExpression(condition, &conditionString);

    nlohmann::json parallelAssignmentsJson;
    for (std::pair<std::string, klee::ref<klee::Expr>> symbolicValue : path.getSymbolicValues()) {
        std::string expressionString;
        printExpression(symbolicValue.second, &expressionString);

        std::string variableName = symbolicValue.first;
        escapeVariableName(&variableName);

        parallelAssignmentsJson += {
                {"variable", variableName},
                {"expression", expressionString}
        };
    }

    /*
    *jsonObject += {
            {"start-cutpoint", path.front()->getName()},
            {"target-cutpoint", path.back()->getName()},
            {"condition", conditionString},
            {"parallel-assignments", parallelAssignmentsJson}
    };
    */
    *jsonObject += {
            {"start-cutpoint", std::to_string((long)path.front())},
            {"target-cutpoint", std::to_string((long)path.back())},
            {"condition", conditionString},
            {"parallel-assignments", parallelAssignmentsJson}
    };
}

void JsonPrinter::printExpression(klee::ref<klee::Expr> expression, std::string *resultString) {
    switch (expression->getKind()) {
        case klee::Expr::Kind::Constant: {
            auto *constExpr = llvm::dyn_cast<klee::ConstantExpr>(expression);

            llvm::SmallString<32> constString;
            llvm::raw_svector_ostream constStream(constString);
            constExpr->getAPValue().print(constStream, true);

            *resultString = constString.c_str();
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
        case klee::Expr::Kind::SRem: {
            printBinaryExpression("%", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Eq: {
            auto *binaryExpression = llvm::dyn_cast<klee::BinaryExpr>(expression);
            if (binaryExpression->left->isZero()) {
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
        case klee::Expr::Kind::Sle: {
            printBinaryExpression("<=", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Sgt: {
            printBinaryExpression(">", expression, resultString);
            break;
        }
        case klee::Expr::Kind::Sge: {
            printBinaryExpression(">", expression, resultString);
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
        case klee::Expr::Kind::Concat: {
            printExpression(expression->getKid(0), resultString);
            break;
        }
        case klee::Expr::Kind::Read: {
            auto *readExpression = llvm::dyn_cast<klee::ReadExpr>(expression);
            std::string variableName = readExpression->updates.root->getName();
            escapeVariableName(&variableName);

            *resultString = variableName;

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

