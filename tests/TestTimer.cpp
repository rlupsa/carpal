// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#include "carpal/Future.h"
#include "carpal/Timer.h"
#include "carpal/ThreadPool.h"

#include <catch2/catch_test_macros.hpp>
#include <stdio.h>

#include "TestHelper.h"

using namespace carpal;

TEST_CASE("Simple_alarm", "[timer]") {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    Timer timer = alarmClock()->setTimer(now + std::chrono::milliseconds(100));
    
    CHECK(!timer.getFuture().isComplete());
    delay(150);
    CHECK(timer.getFuture().isComplete());
    CHECK(timer.getFuture().get());
}

TEST_CASE("Simple_alarm2", "[timer]") {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    Timer timer = alarmClock()->setTimer(now + std::chrono::milliseconds(50));
    
    CHECK(!timer.getFuture().isComplete());
    CHECK(timer.getFuture().get());
    CHECK(std::chrono::system_clock::now() >= now + std::chrono::milliseconds(50));
}

TEST_CASE("Alarm_past", "[timer]") {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    Timer timer = alarmClock()->setTimer(now);
    delay(10);
    CHECK(timer.getFuture().isComplete());
    CHECK(timer.getFuture().get());
}

TEST_CASE("Alarm_cancel", "[timer]") {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    Timer timer = alarmClock()->setTimer(now + std::chrono::milliseconds(50));
    
    CHECK(!timer.getFuture().isComplete());
    timer.cancel();
    delay(10);
    CHECK(timer.getFuture().isComplete());
    CHECK(!timer.getFuture().get());
    CHECK(std::chrono::system_clock::now() < now + std::chrono::milliseconds(50));
}

TEST_CASE("Repeat_alarm", "[timer]") {
    std::chrono::system_clock::time_point const start = std::chrono::system_clock::now();
    std::chrono::system_clock::duration const period = std::chrono::milliseconds(100);
    PeriodicTimer timer = alarmClock()->setPeriodicTimerStartAt(period, start+period);
    std::chrono::system_clock::time_point when = start + period;
    for(int i=0 ; i<5 ; ++i) {
        std::optional<std::chrono::system_clock::time_point> val = timer.getStream().getNextItem();
        std::chrono::system_clock::time_point const now = std::chrono::system_clock::now();
        CHECK(val.has_value());
        CHECK(val.value() == when);
        CHECK(now >= when);
        CHECK(now < when + std::chrono::milliseconds(50));
        when += period;
    }
    timer.cancel();
    std::optional<std::chrono::system_clock::time_point> val = timer.getStream().getNextItem();
    std::chrono::system_clock::time_point const now = std::chrono::system_clock::now();
    when -= period;
    CHECK(!val.has_value());
    CHECK(now >= when);
    CHECK(now < when + std::chrono::milliseconds(50));
}

TEST_CASE("Repeat_alarm_past", "[timer]") {
    std::chrono::system_clock::time_point const start = std::chrono::system_clock::now();
    std::chrono::system_clock::duration const period = std::chrono::milliseconds(100);
    PeriodicTimer timer = alarmClock()->setPeriodicTimerStartAt(period, start);
    std::optional<std::chrono::system_clock::time_point> val = timer.getStream().getNextItem();
    std::chrono::system_clock::time_point const now = std::chrono::system_clock::now();
    CHECK(val.has_value());
    CHECK(val.value() == start);
    CHECK(now < start + std::chrono::milliseconds(10));
    timer.cancel();
    val = timer.getStream().getNextItem();
    CHECK(!val.has_value());
}
