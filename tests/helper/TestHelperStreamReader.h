#pragma once

#include <functional>
#include <memory>

#include "carpal/Future.h"

class TestHelperStreamReader {
public:
    virtual ~TestHelperStreamReader() {}
    virtual carpal::Future<int> readAsync(void* pBuf, int bufSize) = 0;
    virtual int read(void* pBuf, int bufSize) = 0;
    virtual void rewind() = 0;
};

class FunctionsStreamReader {
public:
    virtual ~FunctionsStreamReader() {}
    //virtual carpal::Future<int> readAsync(void* pBuf, int bufSize) = 0;
    virtual std::function<char()> readFunc() = 0;
};

std::shared_ptr<TestHelperStreamReader> testHelperStringReader(std::string content);
