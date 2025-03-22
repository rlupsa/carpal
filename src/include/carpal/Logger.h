#pragma once

#include "carpal/config.h"

#ifdef CARPAL_ENABLE_LOGGING

#include <functional>
#include <sstream>
#include <stdint.h>
#include <string>
#include <chrono>
#include <mutex>

namespace carpal {

class Logger {
public:
    enum class Level {
        trace=0, debug=1, info=2, warn=3, error=4
    };

    bool enabled(Level lvl) const {
        return uint8_t(lvl) >= m_minLevel;
    }
    bool enabledTrace() const {
        return enabled(Level::trace);
    }
    bool enabledDebug() const {
        return enabled(Level::debug);
    }
    bool enabledInfo() const {
        return enabled(Level::info);
    }
    bool enabledWarn() const {
        return enabled(Level::warn);
    }
    bool enabledError() const {
        return enabled(Level::error);
    }
    
    template<typename... Args>
    bool log(Level lvl, Args const&... args) {
        if(!enabled(lvl)) return false;
        std::ostringstream ss;
        initMessage(ss, lvl);
        addContent(ss, args...);
        writeMessage(ss.str());
        return true;
    }
    template<typename... Args>
    bool trace(Args&&... args) {
        return log(Level::trace, args...);
    }
    template<typename... Args>
    bool debug(Args&&... args) {
        return log(Level::debug, args...);
    }
    template<typename... Args>
    bool info(Args&&... args) {
        return log(Level::info, args...);
    }
    template<typename... Args>
    bool warn(Args&&... args) {
        return log(Level::warn, args...);
    }
    template<typename... Args>
    bool error(Args&&... args) {
        return log(Level::error, args...);
    }
    
    Logger();

    void setOutput(std::ostream* pOut);
    void setFunc(std::function<void(std::string const& msg)> func);
    void setMinLevel(Level minLevel);

    template<typename T>
    static std::string toHex(T v) {
        unsigned const nrDigits = 2u*sizeof(v);
        char buf[nrDigits+1];
        for(unsigned i=0 ; i<nrDigits ; ++i) {
            buf[i] = ms_hexDigits[ (v >> (4u*(nrDigits-1-i))) & 0x0Fu ];
        }
        buf[nrDigits] = 0;
        return buf;
    }
    void writeMessage(std::string const& msg);

private:
    static void writeLocalTimeToMilliseconds(std::ostream& ss, std::chrono::system_clock::time_point const& tp);

    void initMessage(std::ostream& ss, Level lvl);

    void addContent(std::ostream& ss) {
        ss << std::endl;
    }
    template<typename T, typename... Args>
    void addContent(std::ostream& ss, T const& v, Args const&... rest) {
        ss << v;
        addContent(ss, rest...);
    }
    template<typename... Args>
    void addContent(std::ostream& ss, std::chrono::system_clock::time_point const& v, Args const&... rest) {
        writeLocalTimeToMilliseconds(ss, v);
        addContent(ss, rest...);
    }

    static char const ms_hexDigits[17];

    std::mutex m_mtx;
    uint8_t m_minLevel;
    std::ostream* m_pOut;
    std::function<void(std::string const& msg)> m_logFunc;
};

extern Logger* g_logger;

} // namespace carpal

#define CARPAL_LOG_TRACE(...) carpal::g_logger->trace(__VA_ARGS__)
#define CARPAL_LOG_DEBUG(...) carpal::g_logger->debug(__VA_ARGS__)
#define CARPAL_LOG_INFO(...) carpal::g_logger->info(__VA_ARGS__)
#define CARPAL_LOG_WARN(...) carpal::g_logger->warn(__VA_ARGS__)
#define CARPAL_LOG_ERROR(...) carpal::g_logger->error(__VA_ARGS__)

#else // defined CARPAL_ENABLE_LOGGING

#define CARPAL_LOG_TRACE(...) ((void)0)
#define CARPAL_LOG_DEBUG(...) ((void)0)
#define CARPAL_LOG_INFO(...) ((void)0)
#define CARPAL_LOG_WARN(...) ((void)0)
#define CARPAL_LOG_ERROR(...) ((void)0)

#endif // not defined CARPAL_ENABLE_LOGGING
