#include "Logger.h"

// STL
#include <memory>

// C
#include <cstdarg>

// Android
#include <android/log.h>

#include "CameraGroup.h"

void Logger::logVerbose(const char *text) {
    __android_log_write(android_LogPriority::ANDROID_LOG_VERBOSE, ::logTag, text);
}

void Logger::logDebug(const char *text) {
    __android_log_write(android_LogPriority::ANDROID_LOG_DEBUG, ::logTag, text);
}

void Logger::logInfo(const char *text) {
    __android_log_write(android_LogPriority::ANDROID_LOG_INFO, ::logTag, text);
}

void Logger::logWarn(const char *text) {
    __android_log_write(android_LogPriority::ANDROID_LOG_WARN, ::logTag, text);
}

void Logger::logError(const char *text) {
    __android_log_write(android_LogPriority::ANDROID_LOG_ERROR, ::logTag, text);
}

void Logger::logFatal(const char *text) {
    __android_log_write(android_LogPriority::ANDROID_LOG_FATAL, ::logTag, text);
}

void Logger::logVerbose(std::size_t max_length, const char *fmt, ...) {
    auto message = std::make_unique<char[]>(max_length);
    va_list args;
    va_start(args, fmt);
    vsnprintf(message.get(), max_length, fmt, args);
    va_end(args);
    __android_log_write(android_LogPriority::ANDROID_LOG_VERBOSE, ::logTag, message.get());
}

void Logger::logDebug(std::size_t max_length, const char *fmt, ...) {
    auto message = std::make_unique<char[]>(max_length);
    va_list args;
    va_start(args, fmt);
    vsnprintf(message.get(), max_length, fmt, args);
    va_end(args);
    __android_log_write(android_LogPriority::ANDROID_LOG_DEBUG, ::logTag, message.get());
}

void Logger::logInfo(std::size_t max_length, const char *fmt, ...) {
    auto message = std::make_unique<char[]>(max_length);
    va_list args;
    va_start(args, fmt);
    vsnprintf(message.get(), max_length, fmt, args);
    va_end(args);
    __android_log_write(android_LogPriority::ANDROID_LOG_INFO, ::logTag, message.get());
}

void Logger::logWarn(std::size_t max_length, const char *fmt, ...) {
    auto message = std::make_unique<char[]>(max_length);
    va_list args;
    va_start(args, fmt);
    vsnprintf(message.get(), max_length, fmt, args);
    va_end(args);
    __android_log_write(android_LogPriority::ANDROID_LOG_WARN, ::logTag, message.get());
}

void Logger::logError(std::size_t max_length, const char *fmt, ...) {
    auto message = std::make_unique<char[]>(max_length);
    va_list args;
    va_start(args, fmt);
    vsnprintf(message.get(), max_length, fmt, args);
    va_end(args);
    __android_log_write(android_LogPriority::ANDROID_LOG_ERROR, ::logTag, message.get());
}

void Logger::logFatal(std::size_t max_length, const char *fmt, ...) {
    auto message = std::make_unique<char[]>(max_length);
    va_list args;
    va_start(args, fmt);
    vsnprintf(message.get(), max_length, fmt, args);
    va_end(args);
    __android_log_write(android_LogPriority::ANDROID_LOG_FATAL, ::logTag, message.get());
}

void Logger::operator()(camera_status_t status) {
    status == ACAMERA_OK ? logInfo("ACAMERA_OK") : logError(camera_error_message(status));
}

void Logger::operator()(media_status_t status) {

}

void Logger::cs(camera_status_t status) {
    status == ACAMERA_OK ? logInfo("ACAMERA_OK") : logError(camera_error_message(status));
}

void Logger::ms(media_status_t status) {

    static const char * chars[] = {"AMEDIA_OK",
                                   "AMEDIA_IMGREADER_ERROR_BASE",
                                   "AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE",
                                   "AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED",
                                   "AMEDIA_IMGREADER_CANNOT_LOCK_IMAGE",
                                   "AMEDIA_IMGREADER_CANNOT_UNLOCK_IMAGE",
                                   "AMEDIA_IMGREADER_IMAGE_NOT_LOCKED"};
    switch (status)
    {
        case AMEDIA_OK:
            logInfo(chars[0]);
            break;
        case AMEDIA_IMGREADER_ERROR_BASE:
            logError(chars[1]);
            break;
        case AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE:
            logError(chars[2]);
            break;
        case AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED:
            logError(chars[3]);
            break;
        case AMEDIA_IMGREADER_CANNOT_LOCK_IMAGE:
            logError(chars[4]);
            break;
        case AMEDIA_IMGREADER_CANNOT_UNLOCK_IMAGE:
            logError(chars[5]);
            break;
        case AMEDIA_IMGREADER_IMAGE_NOT_LOCKED:
            logError(chars[6]);
            break;
        default:
            logError(32, "MEDIA ERROR: %d", status);
            return;
    }
}
