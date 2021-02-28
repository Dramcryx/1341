#ifndef INC_1341_LOGGER_H
#define INC_1341_LOGGER_H

// C
#include <cstddef>


// Android
#include <camera/NdkCameraError.h>
#include <media/NdkMediaError.h>

namespace {
    static const char * logTag = "cam1341";
}

class Logger {
public:
    static void logVerbose(const char * text);
    static void logVerbose(std::size_t max_length, const char * fmt, ...);

    static void logDebug(const char * text);
    static void logDebug(std::size_t max_length, const char * fmt, ...);

    static void logInfo(const char * text);
    static void logInfo(std::size_t max_length, const char * fmt, ...);

    static void logWarn(const char * text);
    static void logWarn(std::size_t max_length, const char * fmt, ...);

    static void logError(const char * text);
    static void logError(std::size_t max_length, const char * fmt, ...);

    static void logFatal(const char * text);
    static void logFatal(std::size_t max_length, const char * fmt, ...);

    static void cs(camera_status_t);
    static void ms(media_status_t);

    void operator()(camera_status_t status);
    void operator()(media_status_t status);

};


#endif //INC_1341_LOGGER_H
