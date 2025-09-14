// Copyright Radu Lupsa 2025
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/StreamSource.h"
#include "carpal/Future.h"
#include "Generator.h"
#include "TestHelperStreamReader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Parsers.h"

Generator<std::function<char()>,EofMark> functionsGenerator(std::string const* pString) {
    for(char c : *pString) {
        co_yield [c](){return c;};
    }
    co_return EofMark();
}

TEST_CASE("Perf_parse_parts", "[perfParse]") {
    std::string const testString("A test string");
    std::shared_ptr<TestHelperStreamReader> pReader = testHelperStringReader(testString);
    char buf[20];
    CHECK(pReader->readAsync(buf, 6).get() == 6);
    CHECK(buf[0] == 'A');
    CHECK(buf[1] == ' ');
    CHECK(buf[5] == 't');
    CHECK(pReader->readAsync(buf, 9).get() == 7);
    CHECK(buf[0] == ' ');
    CHECK(buf[1] == 's');
    CHECK(buf[6] == 'g');
    CHECK(pReader->readAsync(buf, 9).get() == 0);

    pReader->rewind();
    carpal::StreamSource<char> buffered = bufferedTextRead(pReader);
    int pos = 0;
    while(true) {
        carpal::StreamValue<char> v = buffered.dequeue();
        if(v.isItem()) {
            CHECK(pos < testString.size());
            if(pos < testString.size()) {
                CHECK(v.item() == testString[pos]);
            } else {
                break;
            }
            ++pos;
        } else {
            CHECK(pos == testString.size());
            break;
        }
    }
}

TEST_CASE("Perf_parse_parser", "[perfParse]") {
    std::string const testString("10 12 25 4 ");
    CHECK(sumIntegersDirect(testString) == 51);

    std::shared_ptr<TestHelperStreamReader> pReader = testHelperStringReader(testString);
    pReader->rewind();
    CHECK(sumIntegersThroughReader(pReader) == 51);

    CHECK(sumIntegersThroughGenerator(textCharGenerator(&testString)) == 51);
    CHECK(sumIntegersThroughIntGenerator(integersParserThroughGenerator(textCharGenerator(&testString))) == 51);
    CHECK(sumIntegersThroughFunctionGenerator(functionsGenerator(&testString)) == 51);

    pReader->rewind();
    carpal::Future<uint64_t> fSum = sumIntegersTextReader(bufferedTextRead(pReader));
    CHECK(fSum.get() == 51);

    size_t nrNr = 10000;
    std::string s;
    for(size_t i=0 ; i<nrNr ; ++i) {
        s += std::string("263 ");
    }

#if 1
    std::shared_ptr<TestHelperStreamReader> pReader2 = testHelperStringReader(s);
    BENCHMARK("direct") {return sumIntegersDirect(s);};
    BENCHMARK("virtual") {
        pReader2->rewind();
        return sumIntegersThroughReader(pReader2);
    };
    BENCHMARK("generator") {return sumIntegersThroughGenerator(textCharGenerator(&s));};
    BENCHMARK("generator2") {return sumIntegersThroughIntGenerator(integersParserThroughGenerator(textCharGenerator(&s)));};
    BENCHMARK("generator_func") {return sumIntegersThroughFunctionGenerator(functionsGenerator(&s));};
    BENCHMARK("coroutine") {
        pReader2->rewind();
        carpal::Future<uint64_t> fSum = sumIntegersTextReader(bufferedTextRead(pReader2));
        return fSum.get();
    };
#endif
}
