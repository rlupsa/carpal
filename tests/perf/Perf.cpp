// Copyright Radu Lupsa 2023
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at  https://www.boost.org/LICENSE_1_0.txt )

#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <catch2/catch_test_case_info.hpp>

#include "carpal/Logger.h"
#include <fstream>

class CarpalPerfRunListener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(Catch::TestRunInfo const&) override {
        m_logFile.open("carpal-perf.log", std::ios_base::binary | std::ios_base::out | std::ios_base::app);
        carpal::g_logger->setMinLevel(carpal::Logger::Level::info);
        carpal::g_logger->setOutput(&m_logFile);
        CARPAL_LOG_INFO("START tests");
    }

    void testRunEnded(Catch::TestRunStats const&) override {
        CARPAL_LOG_INFO("END tests");
        carpal::g_logger->setOutput(nullptr);
        m_logFile.close();
    }

    void testCaseStarting(Catch::TestCaseInfo const& testInfo) override {
        CARPAL_LOG_INFO("Start test case ", testInfo.name);
    }

    void testCaseEnded(Catch::TestCaseStats const& testCaseStats) override {
        CARPAL_LOG_INFO("Ended test case ", testCaseStats.testInfo->name,
            ((testCaseStats.totals.assertions.allOk() && testCaseStats.totals.testCases.allOk())? " passed" : " failed"));
    }

private:
    std::fstream m_logFile;
};

CATCH_REGISTER_LISTENER(CarpalPerfRunListener)
