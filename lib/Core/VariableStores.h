//
// Created by simon on 08.09.21.
//

#ifndef KLEE_VARIABLESTORES_H
#define KLEE_VARIABLESTORES_H


#include <llvm/IR/Type.h>
#include <klee/Expr/Expr.h>


namespace klee {
    typedef std::map<std::string, llvm::Type *>::iterator VariableTypeMapIterator;

    class VariableTypeMap {
    private:
        std::map<std::string, llvm::Type *> typeMap;

    public:
        VariableTypeMap() = default;
        VariableTypeMap(VariableTypeMap const &variableTypeMap) {
            this->typeMap = std::map<std::string, llvm::Type *>(variableTypeMap.typeMap);
        }

        llvm::Type *getVariableType(const std::string& name) {
            return this->typeMap[name];
        }

        void setVariableType(const std::string& name, llvm::Type *type) {
            this->typeMap[name] = type;
        }

        VariableTypeMapIterator begin() {
            return this->typeMap.begin();
        }

        VariableTypeMapIterator end() {
            return this->typeMap.end();
        }
    };


    typedef std::map<std::string, ref<Expr>> VariableExpressionMap;

//    typedef std::map<std::string, ref<Expr>>::iterator VariableExpressionMapIterator;
//
//    class VariableExpressionMap {
//    private:
//        std::map<std::string, ref<Expr>> *expressionMap;
//
//    public:
//        VariableExpressionMap(){
//            this->expressionMap = new std::map<std::string, ref<Expr>>();
//        }
//
//        VariableExpressionMap(VariableExpressionMap *variableExpressionMap) {
//            this->expressionMap = new std::map<std::string, ref<Expr>>();
//            *this->expressionMap = *variableExpressionMap->expressionMap;
//        }
//
//        ref<Expr> getVariableExpression(const std::string& name) {
//            return (*this->expressionMap)[name];
//        }
//
//        void setVariableExpression(const std::string& name, ref<Expr> expression) {
//            (*this->expressionMap)[name] = expression;
//        }
//
//        VariableExpressionMapIterator begin() {
//            return this->expressionMap->begin();
//        }
//
//        VariableExpressionMapIterator end() {
//            return this->expressionMap->end();
//        }
//    };
}


#endif //KLEE_VARIABLESTORES_H
