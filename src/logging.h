#ifndef _LOGGING_H_
#define _LOGGING_H_

#include <iostream>
#include <fstream>
#include <exception>
#include <string>

// TODO: Make this into a class to use one log object per thread,
// or make this thread-safe some other way.

namespace Netrounds
{
enum loglevel
{
    LOG_OFF,
    LOG_CRIT,
    LOG_ERR,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG
};

extern std::ofstream _log_stream;
extern loglevel _level;

//#define _lstream _log_stream
#define _lstream std::cout

#define logcrit               \
    if (_level < LOG_CRIT) {} \
    else _lstream

#define logerr               \
    if (_level < LOG_ERR) {} \
    else _lstream

#define logwarning               \
    if (_level < LOG_WARNING) {} \
    else _lstream

#define loginfo               \
    if (_level < LOG_INFO) {} \
    else _lstream

#define logdebug               \
    if (_level < LOG_DEBUG) {} \
    else _lstream

inline void INIT_LOGGING(std::string fname, loglevel level = LOG_ERR)
{
    std::cout << "Setting log level to " << level << '\n';
    _level = level;
    _log_stream.open(fname, std::ios::app);
    if (!_log_stream.is_open())
    {
        throw std::runtime_error("Could not open logfile " + fname);
    }
}
}
#endif
