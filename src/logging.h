#ifndef LOGGING_H
#define LOGGING_H

#include <iostream>

#ifndef _DEBUG
#include <fstream>
static std::ofstream _nullstream; // use an unopened fstream as a "null" stream
#define LOG_STREAM _nullstream
#else
#define LOG_STREAM std::clog      // log to std::clog if in debug mode
#endif

#define LOG_INFO LOG_STREAM << __FILE__ << ':' << __LINE__ << " [INFO] "
#define LOG_END std::endl

#define LOG_ERROR std::cerr << __FILE__ << ':' << __LINE__ << " [ERROR] "

#endif
