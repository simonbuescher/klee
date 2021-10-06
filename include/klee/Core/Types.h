//
// Created by simon on 06.10.21.
//

#ifndef KLEE_TYPES_H
#define KLEE_TYPES_H

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
}

#endif //KLEE_TYPES_H
