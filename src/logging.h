#ifndef LOGGING_H
#define LOGGING_H

#include <iostream>
#include <mutex>
static std::mutex _mutex;
#ifdef _DEBUG
#define LOG_INFO _mutex.lock(); std::clog << __TIME__ << " [INFO] "
#define LOG_END std::endl; _mutex.unlock();
#define LOG_ERROR _mutex.lock(); std::cerr << __TIME__ << " [ERROR] "
#else
#include <fstream>
static std::ofstream _nullstream; // use an unopened fstream as a "null" stream
#define LOG_INFO _nullstream
#define LOG_END std::endl; _mutex.unlock();
#define LOG_ERROR _mutex.lock(); std::cerr << __TIME__ << " [ERROR] "
#endif


#endif
