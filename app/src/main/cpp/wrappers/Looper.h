#ifndef INC_1341_LOOPER_H
#define INC_1341_LOOPER_H

#include <android/looper.h>

namespace wrappers {
    struct Looper {
        ALooper *handle = nullptr;

        inline Looper() {
            handle = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
        }

        inline ~Looper() {
            ALooper_release(handle);
        }

        static int pollAll(int timeoutMillis, int *outFd, int *outEvents, void **outData) {
            return ALooper_pollAll(timeoutMillis, outFd, outEvents, outData);
        }

        static int pollOnce(int timeoutMillis, int *outFd, int *outEvents, void **outData) {
            return ALooper_pollOnce(timeoutMillis, outFd, outEvents, outData);
        }
    };
}

#endif //INC_1341_LOOPER_H
