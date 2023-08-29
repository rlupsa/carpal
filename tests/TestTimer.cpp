#include "carpal/Future.h"
#include "carpal/Timer.h"
#include "carpal/ThreadPool.h"

#include <catch2/catch.hpp>
#include <stdio.h>

#include "TestHelper.h"

using namespace carpal;

TEST_CASE("Simple_alarm", "[timer]") {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    Timer timer = alarmClock()->setTimer(now + std::chrono::milliseconds(50));
    
    CHECK(!timer.getFuture().isComplete());
    delay(60);
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
