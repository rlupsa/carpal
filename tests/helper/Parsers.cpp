#include "Parsers.h"

#include <map>

carpal::StreamSource<char> bufferedTextRead(std::shared_ptr<TestHelperStreamReader> pReader) {
    co_await carpal::defaultSameThreadStart();
    static int const bufSize = 1000;
    char buf[bufSize + 1];
    while(true) {
        int ret = co_await pReader->readAsync(buf, bufSize);
        if(ret <= 0) {
            co_return;
        }
        for(int i=0 ; i<ret ; ++i) {
            co_yield(buf[i]);
        }
    }
}

uint64_t sumIntegersDirect(std::string const& content) {
    uint64_t sum = 0;
    uint64_t current = 0;
    for(char c : content) {
        if(c == ' ') {
            sum += current;
            current = 0;
        } else {
            current = current*10 + (c-'0');
        }
    }
    sum += current;
    return sum;
}

uint64_t sumIntegersThroughReader(std::shared_ptr<TestHelperStreamReader> pReader) {
    uint64_t sum = 0;
    uint64_t current = 0;
    while(true) {
        char c;
        if(pReader->read(&c, 1) < 1) {
            break;
        }
        if(c == ' ') {
            sum += current;
            current = 0;
        } else {
            current = current*10 + (c-'0');
        }
    }
    sum += current;
    return sum;
}

uint64_t sumIntegersThroughQueuePull(std::shared_ptr<TestHelperStreamReader> pReader) {
    carpal::SingleProducerSingleConsumerQueue<char> queue;

    uint64_t sum = 0;
    uint64_t current = 0;
    while(true) {
        queue.setOnSlotAvailableOnceCallback([pReader,&queue](){
            char c;
            if(pReader->read(&c, 1) < 1) {
                queue.enqueue(carpal::StreamValue<char,void>::makeEof());
            } else {
                queue.enqueue(carpal::StreamValue<char,void>::makeItem(c));
            }
        });
        carpal::StreamValue<char,void> val = queue.dequeue();
        if(!val.isItem()) {
            break;
        }
        char c = val.item();
        if(c == ' ') {
            sum += current;
            current = 0;
        } else {
            current = current*10 + (c-'0');
        }
    }
    sum += current;
    return sum;
}

Generator<char,EofMark> textCharGenerator(std::string const* pString) {
    for(char c : *pString) {
        co_yield c;
    }
    co_return EofMark();
}

uint64_t sumIntegersThroughGenerator(Generator<char,EofMark>&& generator) {
    uint64_t sum = 0;
    uint64_t current = 0;
    while(true) {
        carpal::StreamValue<char,EofMark> val = generator.dequeue();
        if(!val.isItem()) {
            break;
        }
        char c = val.item();
        if(c == ' ') {
            sum += current;
            current = 0;
        } else {
            current = current*10 + (c-'0');
        }
    }
    sum += current;
    return sum;
}

std::mutex mtx;
std::map<char, int> histogram;

Generator<uint64_t,EofMark> integersParserThroughGenerator(Generator<char,EofMark>&& generator) {
    uint64_t current = 0;
    while(true) {
        carpal::StreamValue<char,EofMark> val = co_await generator;
        if(!val.isItem()) {
            break;
        }
        std::unique_lock<std::mutex> lck(mtx);
        char c = val.item();
        if(c == ' ') {
            lck.unlock();
            co_yield current;
            current = 0;
        } else {
            auto it = histogram.find(c);
            if(it == histogram.end()) {
                histogram.emplace(c, 1);
            } else {
                ++it->second;
            }
            current = current*10 + (c-'0');
            lck.unlock();
        }
    }
    co_yield current;
    co_return EofMark();
}

uint64_t sumIntegersThroughIntGenerator(Generator<uint64_t,EofMark>&& generator) {
    uint64_t sum = 0;
    while(true) {
        carpal::StreamValue<uint64_t,EofMark> val = generator.dequeue();
        if(!val.isItem()) {
            return sum;
        }
        sum += val.item();
    }
}

carpal::Future<uint64_t> sumIntegersTextReader(carpal::StreamSource<char> src) {
    uint64_t sum = 0;
    uint64_t current = 0;
    while(true) {
        carpal::StreamValue<char> v = co_await src;
        if(v.isItem()) {
            char c = v.item();
            if(c == ' ') {
                sum += current;
                current = 0;
            } else {
                current = current*10 + (c-'0');
            }
        } else {
            sum += current;
            co_return sum;
        }
    }
}

uint64_t sumIntegersThroughFunctionGenerator(Generator<std::function<char()>,EofMark>&& generator) {
    uint64_t sum = 0;
    uint64_t current = 0;
    while(true) {
        carpal::StreamValue<std::function<char()>,EofMark> val = generator.dequeue();
        if(!val.isItem()) {
            break;
        }
        char c = val.item()();
        if(c == ' ') {
            sum += current;
            current = 0;
        } else {
            current = current*10 + (c-'0');
        }
    }
    sum += current;
    return sum;
}
