#include "Device.h"

#include "CameraGroup.h"
#include "JVMTypes.h"
#include "AndroidWrappers.h"

extern camera_group_t context;
extern java_type_set_t java;

_C_INTERFACE_ jbyte JNICALL //
Java_com_dramcryx_cam1341_Device_facing(JNIEnv *env,
jobject instance) noexcept {
    if (context.manager == nullptr) // not initialized
        return JNI_FALSE;

    auto device_id = env->GetShortField(instance, java.device_id_f);
    assert(device_id != -1);

    const auto facing = context.get_facing(static_cast<uint16_t>(device_id));
    return static_cast<jbyte>(facing);
}

_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_Device_open(JNIEnv *env, jobject instance) noexcept {
    camera_status_t status = ACAMERA_OK;
    if (context.manager == nullptr) // not initialized
        return;

    const auto id = env->GetShortField(instance, java.device_id_f);
    assert(id != -1);

    ACameraDevice_StateCallbacks callbacks{};
    callbacks.context = std::addressof(context);
    callbacks.onDisconnected = reinterpret_cast<ACameraDevice_StateCallback>(
            context_on_device_disconnected);
    callbacks.onError = reinterpret_cast<ACameraDevice_ErrorStateCallback>(
            context_on_device_error);

    context.close_device(id);
    status = context.open_device(id, callbacks);

    if (status == ACAMERA_OK)
        return;

// throw exception
// TODO : fmt::format("ACameraManager_openCamera: {}", status).c_str()
    env->ThrowNew(java.runtime_exception, "ACameraManager_openCamera");
}

_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_Device_close(JNIEnv *env, jobject instance) noexcept {
    if (context.manager == nullptr) // not initialized
        return;

    const auto id = env->GetShortField(instance, java.device_id_f);
    assert(id != -1);

    context.close_device(id);
}

// TODO: Resource cleanup
_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_Device_startRepeat(JNIEnv *env, jobject instance,
        jobject surface) noexcept {
    camera_status_t status = ACAMERA_OK;
    if (context.manager == nullptr) // not initialized
        return;
    const auto id = env->GetShortField(instance, java.device_id_f);
    assert(id != -1);

// `ANativeWindow_fromSurface` acquires a reference
// `ANativeWindow_release` releases it
// ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    auto window = native_window_ptr{ANativeWindow_fromSurface(env, surface),
                                    ANativeWindow_release};
    assert(window.get() != nullptr);

    ACameraCaptureSession_stateCallbacks on_state_changed{};
    on_state_changed.context = std::addressof(context);
    on_state_changed.onReady =
            reinterpret_cast<ACameraCaptureSession_stateCallback>(
                    context_on_session_ready);
    on_state_changed.onClosed =
            reinterpret_cast<ACameraCaptureSession_stateCallback>(
                    context_on_session_closed);
    on_state_changed.onActive =
            reinterpret_cast<ACameraCaptureSession_stateCallback>(
                    context_on_session_active);

    ACameraCaptureSession_captureCallbacks on_capture_event{};
    on_capture_event.context = std::addressof(context);
    on_capture_event.onCaptureStarted =
            reinterpret_cast<ACameraCaptureSession_captureCallback_start>(
                    context_on_capture_started);
    on_capture_event.onCaptureBufferLost =
            reinterpret_cast<ACameraCaptureSession_captureCallback_bufferLost>(
                    context_on_capture_buffer_lost);
    on_capture_event.onCaptureProgressed =
            reinterpret_cast<ACameraCaptureSession_captureCallback_result>(
                    context_on_capture_progressed);
    on_capture_event.onCaptureCompleted =
            reinterpret_cast<ACameraCaptureSession_captureCallback_result>(
                    context_on_capture_completed);
    on_capture_event.onCaptureFailed =
            reinterpret_cast<ACameraCaptureSession_captureCallback_failed>(
                    context_on_capture_failed);
    on_capture_event.onCaptureSequenceAborted =
            reinterpret_cast<ACameraCaptureSession_captureCallback_sequenceAbort>(
                    context_on_capture_sequence_abort);
    on_capture_event.onCaptureSequenceCompleted =
            reinterpret_cast<ACameraCaptureSession_captureCallback_sequenceEnd>(
                    context_on_capture_sequence_complete);

    status = context.start_repeat(id, window.get(), on_state_changed,
                                  on_capture_event);
    assert(status == ACAMERA_OK);
}

_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_Device_stopRepeat(JNIEnv *env,
jobject instance) noexcept {
    if (context.manager == nullptr) // not initialized
        return;

    const auto id = env->GetShortField(instance, java.device_id_f);
    assert(id != -1);

    context.stop_repeat(id);
}

_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_Device_startCapture(JNIEnv *env, jobject instance,
        jobject surface) noexcept {
    camera_status_t status = ACAMERA_OK;
    if (context.manager == nullptr) // not initialized
        return;

    auto id = env->GetShortField(instance, java.device_id_f);
    assert(id != -1);

// `ANativeWindow_fromSurface` acquires a reference
// `ANativeWindow_release` releases it
// ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    auto window = native_window_ptr{ANativeWindow_fromSurface(env, surface),
                                    ANativeWindow_release};
    assert(window.get() != nullptr);

    ACameraCaptureSession_stateCallbacks on_state_changed{};
    on_state_changed.context = std::addressof(context);
    on_state_changed.onReady =
            reinterpret_cast<ACameraCaptureSession_stateCallback>(
                    context_on_session_ready);
    on_state_changed.onClosed =
            reinterpret_cast<ACameraCaptureSession_stateCallback>(
                    context_on_session_closed);
    on_state_changed.onActive =
            reinterpret_cast<ACameraCaptureSession_stateCallback>(
                    context_on_session_active);

    ACameraCaptureSession_captureCallbacks on_capture_event{};
    on_capture_event.context = std::addressof(context);
    on_capture_event.onCaptureStarted =
            reinterpret_cast<ACameraCaptureSession_captureCallback_start>(
                    context_on_capture_started);
    on_capture_event.onCaptureBufferLost =
            reinterpret_cast<ACameraCaptureSession_captureCallback_bufferLost>(
                    context_on_capture_buffer_lost);
    on_capture_event.onCaptureProgressed =
            reinterpret_cast<ACameraCaptureSession_captureCallback_result>(
                    context_on_capture_progressed);
    on_capture_event.onCaptureCompleted =
            reinterpret_cast<ACameraCaptureSession_captureCallback_result>(
                    context_on_capture_completed);
    on_capture_event.onCaptureFailed =
            reinterpret_cast<ACameraCaptureSession_captureCallback_failed>(
                    context_on_capture_failed);
    on_capture_event.onCaptureSequenceAborted =
            reinterpret_cast<ACameraCaptureSession_captureCallback_sequenceAbort>(
                    context_on_capture_sequence_abort);
    on_capture_event.onCaptureSequenceCompleted =
            reinterpret_cast<ACameraCaptureSession_captureCallback_sequenceEnd>(
                    context_on_capture_sequence_complete);

    status = context.start_capture(id, window.get(), on_state_changed,
                                   on_capture_event);
    assert(status == ACAMERA_OK);
}

_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_Device_stopCapture(JNIEnv *env,
jobject instance) noexcept {
    if (context.manager == nullptr) // not initialized
        return;

    const auto id = env->GetShortField(instance, java.device_id_f);
    assert(id != -1);

    context.stop_capture(id);
}
