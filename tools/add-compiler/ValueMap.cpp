//
// Created by simon on 03.12.21.
//

#include "ValueMap.h"

bool ValueMap::contains(const std::string &key) {
    return this->cacheMap.find(key) != this->cacheMap.end();
}

void ValueMap::store(const std::string &key, llvm::Value *value) {
    this->cacheMap[key] = value;
}

llvm::Value *ValueMap::get(const std::string &key) {
    return this->cacheMap[key];

}