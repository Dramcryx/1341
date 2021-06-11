#ifndef INC_1341_CAMERAOUTPUTTARGET_H
#define INC_1341_CAMERAOUTPUTTARGET_H

#include <camera/NdkCaptureRequest.h>

namespace wrappers {

    struct CameraOutputTarget {
        ACameraOutputTarget *handle = nullptr;

        inline CameraOutputTarget(ANativeWindow *window) {
            auto status = ACameraOutputTarget_create(window, std::addressof(this->handle));
            assert(status == ACAMERA_OK);
        }

        inline ~CameraOutputTarget() {
            ACameraOutputTarget_free(this->handle);
        }
    };

}

#endif //INC_1341_CAMERAOUTPUTTARGET_H
