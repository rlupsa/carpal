// Copyright Radu Lupsa 2025
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

//#include "carpal/Future.h"
//#include "carpal/AsyncMutex.h"
//#include "carpal/ThreadPool.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <stdio.h>
#include <stdint.h>
//#include "TestHelper.h"

uint64_t sum_arithmetic_progression(uint64_t n) {
    uint64_t out = 0;
    for(uint64_t i=1 ; i<=n ; ++i) {
        out += i;
    }
    return out;
}

TEST_CASE("Perf_sample", "[perfSample]") {
    CHECK(sum_arithmetic_progression(1) == 1);
    CHECK(sum_arithmetic_progression(10) == 10*11/2);
    CHECK(sum_arithmetic_progression(1000) == 1000*1001/2);

    BENCHMARK("sum_arithmetic_progression(1000)") {return sum_arithmetic_progression(1000);};
    BENCHMARK("sum_arithmetic_progression(10000)") {return sum_arithmetic_progression(10000);};
}
