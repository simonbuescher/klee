//
// Created by simon on 03.12.21.
//

#ifndef KLEE_VALUEMAP_H
#define KLEE_VALUEMAP_H


#include <string>
#include <map>
#include <llvm/IR/Value.h>

class ValueMap {
private:
    std::map<std::string, llvm::Value *> cacheMap;

public:
    bool contains(const std::string &key);

    void store(const std::string &key, llvm::Value *value);

    llvm::Value *get(const std::string &key);

    std::map<std::string, llvm::Value *>::iterator begin() { return this->cacheMap.begin(); }

    std::map<std::string, llvm::Value *>::iterator end() { return this->cacheMap.end(); }
};


#endif //KLEE_VALUEMAP_H
