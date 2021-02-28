#include "CameraModel.h"

#include "CameraGroup.h"

#include "JVMTypes.h"
#include "Logger.h"

#include <thread>
#include <string>
#include <cassert>

using namespace std;

__attribute__((constructor)) void jni_on_load(void) noexcept {
    Logger::logInfo(100, "Thread ID: %d", pthread_gettid_np(pthread_self()));
}

java_type_set_t java{};
camera_group_t context{};

_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_CameraModel_Init(JNIEnv* env, jclass type) noexcept {
    uint16_t num_camera = 0;
    camera_status_t status = ACAMERA_OK;

    // already initialized. do nothing
    if (context.manager != nullptr)
        return;

    // Find exception class information (type info)
    java.runtime_exception = env->FindClass("java/lang/RuntimeException");
    java.illegal_argument_exception =
            env->FindClass("java/lang/IllegalArgumentException");
    java.illegal_state_exception =
            env->FindClass("java/lang/IllegalStateException");
    java.unsupported_operation_exception =
            env->FindClass("java/lang/UnsupportedOperationException");
    java.index_out_of_bounds_exception =
            env->FindClass("java/lang/IndexOutOfBoundsException");

    // !!! Since we can't throw if this info is null, call assert !!!
    assert(java.runtime_exception != nullptr);
    assert(java.illegal_argument_exception != nullptr);
    assert(java.illegal_state_exception != nullptr);
    assert(java.unsupported_operation_exception != nullptr);
    assert(java.index_out_of_bounds_exception != nullptr);

    context.release();

    context.manager = ACameraManager_create();
    assert(context.manager != nullptr);

    status = ACameraManager_getCameraIdList(context.manager, &context.id_list);
    if (status != ACAMERA_OK)
        goto ThrowJavaException;

    // https://developer.android.com/reference/android/hardware/camera2/CameraMetadata
    // https://android.googlesource.com/platform/frameworks/av/+/2e19c3c/services/camera/libcameraservice/camera2/CameraMetadata.h
    num_camera = context.id_list->numCameras;
    // library must reserve enough space to support
    assert(num_camera <= camera_group_t::max_camera_count);

    for (uint16_t i = 0u; i < num_camera; ++i) {
        const char* cam_id = context.id_list->cameraIds[i];
        status = ACameraManager_getCameraCharacteristics(
                context.manager, cam_id, addressof(context.metadata_set[i]));

        if (status == ACAMERA_OK)
            continue;

        Logger::logError("ACameraManager_getCameraCharacteristics");
        goto ThrowJavaException;
    }
    return;
    ThrowJavaException:
    env->ThrowNew(java.illegal_argument_exception,
                  camera_error_message(status));
}

_C_INTERFACE_ jint JNICALL //
Java_com_dramcryx_cam1341_CameraModel_GetDeviceCount(JNIEnv* env, jclass type) noexcept {
    if (context.manager == nullptr) // not initialized
        return 0;

    return context.id_list->numCameras;
}

_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_CameraModel_SetDeviceData(JNIEnv* env, jclass type,
                                          jobjectArray devices) noexcept {
    if (context.manager == nullptr) // not initialized
        return;

    java.device_t = env->FindClass("com/dramcryx/cam1341/Device");
    assert(java.device_t != nullptr);

    const auto count = context.id_list->numCameras;
    assert(count == env->GetArrayLength(devices));

    // https://developer.android.com/ndk/reference/group/camera
    for (short index = 0; index < count; ++index) {
        ACameraMetadata_const_entry entry{};
        ACameraMetadata_getConstEntry(
                context.metadata_set[index],
                ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);
        for (auto i = 0u; i < entry.count; i += 4) {
            const int32_t direction = entry.data.i32[i + 3];
            if (direction ==
                ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_INPUT)
                ;
            if (direction ==
                ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT)
                ;

            const int32_t format = entry.data.i32[i + 0];
            const int32_t width = entry.data.i32[i + 1];
            const int32_t height = entry.data.i32[i + 2];

            if (format == AIMAGE_FORMAT_PRIVATE)
                Logger::logDebug(100, "Private: %d x %d ", width, height);
            if (format == AIMAGE_FORMAT_YUV_420_888)
                Logger::logDebug(100, "YUV_420_888: %d x %d ", width, height);
            if (format == AIMAGE_FORMAT_JPEG)
                Logger::logDebug(100, "JPEG: %d x %d ", width, height);
            if (format == AIMAGE_FORMAT_RAW16)
                Logger::logDebug(100, "Raw16: %d x %d ", width, height);
        }

        jobject device = env->GetObjectArrayElement(devices, index);
        assert(device != nullptr);

        java.device_id_f = env->GetFieldID(java.device_t, "id", "S"); // short
        assert(java.device_id_f != nullptr);

        env->SetShortField(device, java.device_id_f, index);
    }
}

// - References
//      NdkCameraError.h
auto camera_error_message(camera_status_t status) noexcept -> const char* {
    switch (status) {
        case ACAMERA_ERROR_UNKNOWN:
            return "Camera operation has failed due to an unspecified cause.";
        case ACAMERA_ERROR_INVALID_PARAMETER:
            return "Camera operation has failed due to an invalid parameter being "
                   "passed to the method.";
        case ACAMERA_ERROR_CAMERA_DISCONNECTED:
            return "Camera operation has failed because the camera device has been "
                   "closed, possibly because a higher-priority client has taken "
                   "ownership of the camera device.";
        case ACAMERA_ERROR_NOT_ENOUGH_MEMORY:
            return "Camera operation has failed due to insufficient memory.";
        case ACAMERA_ERROR_METADATA_NOT_FOUND:
            return "Camera operation has failed due to the requested metadata tag "
                   "cannot be found in input. ACameraMetadata or ACaptureRequest";
        case ACAMERA_ERROR_CAMERA_DEVICE:
            return "Camera operation has failed and the camera device has "
                   "encountered a fatal error and needs to be re-opened before it "
                   "can be used again.";
        case ACAMERA_ERROR_CAMERA_SERVICE:
            /**
             * Camera operation has failed and the camera service has encountered a
             * fatal error.
             *
             * <p>The Android device may need to be shut down and restarted to
             * restore camera function, or there may be a persistent hardware
             * problem.</p>
             *
             * <p>An attempt at recovery may be possible by closing the
             * ACameraDevice and the ACameraManager, and trying to acquire all
             * resources again from scratch.</p>
             */
            return "Camera operation has failed and the camera service has "
                   "encountered a fatal error.";
        case ACAMERA_ERROR_SESSION_CLOSED:
            return "The ACameraCaptureSession has been closed and cannot perform "
                   "any operation other than ACameraCaptureSession_close.";
        case ACAMERA_ERROR_INVALID_OPERATION:
            return "Camera operation has failed due to an invalid internal "
                   "operation. Usually this is due to a low-level problem that may "
                   "resolve itself on retry";
        case ACAMERA_ERROR_STREAM_CONFIGURE_FAIL:
            return "Camera device does not support the stream configuration "
                   "provided by application in ACameraDevice_createCaptureSession.";
        case ACAMERA_ERROR_CAMERA_IN_USE:
            return "Camera device is being used by another higher priority camera "
                   "API client.";
        case ACAMERA_ERROR_MAX_CAMERA_IN_USE:
            return "The system-wide limit for number of open cameras or camera "
                   "resources has been reached, and more camera devices cannot be "
                   "opened until previous instances are closed.";
        case ACAMERA_ERROR_CAMERA_DISABLED:
            return "The camera is disabled due to a device policy, and cannot be "
                   "opened.";
        case ACAMERA_ERROR_PERMISSION_DENIED:
            return "The application does not have permission to open camera.";
        default:
            return "ACAMERA_OK";
    }
}