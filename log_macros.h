#ifndef LOG_MACROS_H
#define LOG_MACROS_H

#ifdef _DEBUG
// 调试版本 - 正常输出日志
#define LOG_PRINT(fmt, ...) wprintf(fmt, __VA_ARGS__)
#define LOG_ERROR(fmt, ...) fwprintf(stderr, fmt, __VA_ARGS__)
#else
// Release版本 - 空实现，不输出日志
#define LOG_PRINT(fmt, ...) ((void)0)
#define LOG_ERROR(fmt, ...) ((void)0)
#endif

#endif // LOG_MACROS_H