#ifndef INC_1341_CAPTUREREQUEST_H
#define INC_1341_CAPTUREREQUEST_H

#include <camera/NdkCaptureRequest.h>

namespace wrappers {

    struct CaptureRequest {
        ACaptureRequest *handle = nullptr;

        std::vector<ACameraOutputTarget *> targets;

        inline camera_status_t setTargetFpsRange(int from, int to) {
            const int range[2] = {from, to};
            return ACaptureRequest_setEntry_i32(this->handle, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE,
                                                2, range);
        }

        inline camera_status_t
        setNoiseReductionMode(acamera_metadata_enum_acamera_noise_reduction_mode mode) {
            const uint8_t downcast = mode;
            return ACaptureRequest_setEntry_u8(this->handle, ACAMERA_NOISE_REDUCTION_MODE, 1,
                                               &downcast);
        }

        inline camera_status_t setAFMode(acamera_metadata_enum_acamera_control_af_mode mode) {
            const uint8_t downcast = mode;
            return ACaptureRequest_setEntry_u8(this->handle, ACAMERA_CONTROL_AF_MODE, 1, &downcast);
        }

        inline camera_status_t setTonemapMode(acamera_metadata_enum_acamera_tonemap_mode mode) {
            const uint8_t downcast = mode;
            return ACaptureRequest_setEntry_u8(this->handle, ACAMERA_TONEMAP_MODE, 1, &downcast);
        }

        inline camera_status_t addTarget(ACameraOutputTarget *output) {
            auto result = ACaptureRequest_addTarget(this->handle, output);
            if (result == ACAMERA_OK) {
                targets.push_back(output);
            }
            return result;
        }

        inline ~CaptureRequest() {
            for (auto i: targets) {
                ACaptureRequest_removeTarget(this->handle, i);
            }
            ACaptureRequest_free(handle);
        }
    };

}

#endif //INC_1341_CAPTUREREQUEST_H
