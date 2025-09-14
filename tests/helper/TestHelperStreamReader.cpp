#include "TestHelperStreamReader.h"

#include <string.h>

class TestHelperStringReader : public TestHelperStreamReader {
public:
    explicit TestHelperStringReader(std::string content)
        :m_content(std::move(content)),
        m_pos(0)
        {}
    ~TestHelperStringReader() override = default;
    carpal::Future<int> readAsync(void* pBuf, int bufSize) override {
        if(bufSize <= 0) {
            return carpal::completedFuture<int>(-1);
        }
        if(bufSize > m_content.size() - m_pos) {
            bufSize = int(m_content.size() - m_pos);
        }
        memcpy(pBuf, m_content.data()+m_pos, bufSize);
        m_pos += bufSize;
        return carpal::completedFuture<int>(bufSize);
    }
    int read(void* pBuf, int bufSize) override {
        if(bufSize <= 0) {
            return -1;
        }
        if(bufSize > m_content.size() - m_pos) {
            bufSize = int(m_content.size() - m_pos);
        }
        memcpy(pBuf, m_content.data()+m_pos, bufSize);
        m_pos += bufSize;
        return bufSize;
    }
    void rewind() override {
        m_pos = 0;
    }
private:
    std::string m_content;
    size_t m_pos;
};

std::shared_ptr<TestHelperStreamReader> testHelperStringReader(std::string content) {
    return std::make_shared<TestHelperStringReader>(std::move(content));
}
