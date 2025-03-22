#include "carpal/Logger.h"
#include <iomanip>
#include <iostream>
#include <thread>

namespace {
    char const* levelNames[] = {"Trace ", "Debug ", "Info  ", "Warn  ", "Error "};

    carpal::Logger loggerObj;
} // unnamed namespace

carpal::Logger::Logger()
    :m_minLevel(1),
    m_pOut(&std::cerr)
{
    // empty
}

void carpal::Logger::setOutput(std::ostream* pOut) {
    std::unique_lock<std::mutex> lck(m_mtx);
    if (pOut != nullptr) {
        m_pOut = pOut;
    } else {
        m_pOut = &std::cerr;
    }
}

void carpal::Logger::setFunc(std::function<void(std::string const& msg)> func) {
    m_logFunc = func;
}

void carpal::Logger::setMinLevel(Level minLevel) {
    std::unique_lock<std::mutex> lck(m_mtx);
    m_minLevel = uint8_t(minLevel);
}

void carpal::Logger::writeLocalTimeToMilliseconds(std::ostream& ss, std::chrono::system_clock::time_point const& tp) {
    std::time_t tpTimeT = std::chrono::system_clock::to_time_t(tp);
    std::tm tpTm;
#ifdef _WINDOWS
    localtime_s(&tpTm, &tpTimeT);
#else
    localtime_r(&tpTimeT, &tpTm);
#endif
    std::chrono::system_clock::time_point tpBack = std::chrono::system_clock::from_time_t(tpTimeT);
    int16_t remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(tp - tpBack).count();
    char buf[4];
    buf[0] = remainingMs / 100 + '0';
    buf[1] = (remainingMs % 100) / 10 + '0';
    buf[2] = remainingMs % 10 + '0';
    buf[3] = 0;
    ss << std::put_time(&tpTm, "%F %T.") << buf;
}

void carpal::Logger::initMessage(std::ostream& ss, Level lvl) {
    writeLocalTimeToMilliseconds(ss, std::chrono::system_clock::now());
    ss << " 0x" << std::hex << std::setw(8) << std::setfill('0') << std::this_thread::get_id() << std::dec << std::setw(0) << std::setfill(' ');
    ss << " " << levelNames[uint8_t(lvl)];
}

void carpal::Logger::writeMessage(std::string const& msg) {
    std::unique_lock<std::mutex> lck(m_mtx);
    if(m_logFunc != nullptr) {
        m_logFunc(msg);
    } else {
        (*m_pOut) << msg;
    }
}

char const carpal::Logger::ms_hexDigits[17] = "0123456789ABCDEF";

carpal::Logger* carpal::g_logger = &loggerObj;
