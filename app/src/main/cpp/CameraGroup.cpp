#include "CameraGroup.h"

// STL
#include <memory>
#include <random>

#include "Logger.h"
#include "AndroidWrappers.h"

#include <arm_neon.h>

wrappers::ImageReader imageReader{};
wrappers::NativeWindow imageReaderWindow;

void camera_group_t::release() noexcept {
    // close all devices
    for (uint16_t id = 0u; id < max_camera_count; ++id)
        close_device(id);

    // release all metadata
    for (auto& meta : metadata_set)
        if (meta) {
            ACameraMetadata_free(meta);
            meta = nullptr;
        }

    // remove id list
    if (id_list)
        ACameraManager_deleteCameraIdList(id_list);
    id_list = nullptr;

    // release manager (camera service)
    if (manager)
        ACameraManager_delete(manager);
    manager = nullptr;
}

camera_status_t
camera_group_t::open_device(uint16_t id,
                            ACameraDevice_StateCallbacks& callbacks) noexcept {
    auto& device = this->device_set[id];
    auto status = ACameraManager_openCamera(         //
            this->manager, this->id_list->cameraIds[id], //
            std::addressof(callbacks), std::addressof(device));
    return status;
}

// Notice that this routine doesn't free metadata
void camera_group_t::close_device(uint16_t id) noexcept {
    // close session
    auto& session = this->session_set[id];
    if (session) {
        Logger::logWarn(100, "session for device %d is alive. abort/closing...", id);

        // Abort all kind of requests
        ACameraCaptureSession_abortCaptures(session);
        ACameraCaptureSession_stopRepeating(session);
        // close
        ACameraCaptureSession_close(session);
        session = nullptr;
    }
    // close device
    auto& device = this->device_set[id];
    if (device) {
        // Producing meesage like following
        // W/ACameraCaptureSession: Device is closed but session 0 is not
        // notified
        //
        // Seems like ffmpeg also has same issue, but can't sure about its
        // comment...
        //
        Logger::logWarn(100, "closing device %d ...", id);

        ACameraDevice_close(device);
        device = nullptr;
    }
}

camera_status_t camera_group_t::start_repeat(
        uint16_t id, ANativeWindow* window,
        ACameraCaptureSession_stateCallbacks& on_session_changed,
        ACameraCaptureSession_captureCallbacks& on_capture_event) noexcept {
    camera_status_t status = ACAMERA_OK;

    if (imageReaderWindow.handle == nullptr)
    {
        imageReader.getWindow(imageReaderWindow);
        imageReader.setImageListener(window, [](void * context, AImageReader * ignore){
            imageReader(context, ignore);
        });
    }

    // ---- capture request (preview) ----

    wrappers::CameraDevice device{this->device_set[id]};
    // `ACaptureRequest` == how to capture
    auto request = device.createCaptureRequest(TEMPLATE_RECORD);
    Logger::cs(request->setTargetFpsRange(60, 60));
    Logger::cs(request->setNoiseReductionMode(ACAMERA_NOISE_REDUCTION_MODE_OFF));
    Logger::cs(request->setAFMode(ACAMERA_CONTROL_AF_MODE_CONTINUOUS_VIDEO));
    Logger::cs(request->setTonemapMode(ACAMERA_TONEMAP_MODE_FAST));
//    uint8_t aaa = ACAMERA_TONEMAP_PRESET_CURVE_SRGB;
//    ACaptureRequest_setEntry_u8(request->handle, ACAMERA_TONEMAP_MODE_PRESET_CURVE, 1, &aaa);
//    // designate target surface in request
    //auto target = wrappers::CameraOutputTarget{window};
    auto target2 = wrappers::CameraOutputTarget{imageReaderWindow.handle};

    //Logger::cs(request->addTarget(target.handle));
    Logger::cs(request->addTarget(target2.handle));

    // session output
    //auto output = wrappers::CaptureSessionOutput(window);
    auto output2 = wrappers::CaptureSessionOutput(imageReaderWindow.handle);

//    auto output = wrappers::CaptureSessionSharedOutput(window);

    // container for multiplexing of session output
    auto container = wrappers::CaptureSessionOutputContainer();

    //status = container.add(output.handle);
    //assert(status == ACAMERA_OK);

    status = container.add(output2.handle);
    assert(status == ACAMERA_OK);

    // ---- create a session ----
    status = device.createCaptureSessionWithSessionParameters(
            container.handle,
            request->handle,
            std::addressof(on_session_changed),
            std::addressof(this->session_set[id]));
    assert(status == ACAMERA_OK);

    // ---- set request ----
    std::array<ACaptureRequest*, 1> batch_request{};
    batch_request[0] = request->handle;

    status = ACameraCaptureSession_setRepeatingRequest(
            this->session_set[id],
            std::addressof(on_capture_event),
            batch_request.size(),
            batch_request.data(),
            std::addressof(this->seq_id_set[id]));
    assert(status == ACAMERA_OK);

    return status;
}

void camera_group_t::stop_repeat(uint16_t id) noexcept {
    auto& session = this->session_set[id];
    if (session) {
        Logger::logWarn(100, "stop_repeat for session %d ", id);

        // follow `ACameraCaptureSession_setRepeatingRequest`
        ACameraCaptureSession_stopRepeating(session);

        ACameraCaptureSession_close(session);
        session = nullptr;
    }
    this->seq_id_set[id] = CAPTURE_SEQUENCE_ID_NONE;
}

camera_status_t camera_group_t::start_capture(
        uint16_t id, ANativeWindow* window,
        ACameraCaptureSession_stateCallbacks& on_session_changed,
        ACameraCaptureSession_captureCallbacks& on_capture_event) noexcept {
    camera_status_t status = ACAMERA_OK;

    // ---- target surface for camera ----
    auto target = camera_output_target_ptr{[=]() {
        ACameraOutputTarget* target{};
        ACameraOutputTarget_create(
                window, std::addressof(target));
        return target;
    }(),
                                           ACameraOutputTarget_free};
    assert(target.get() != nullptr);

    // ---- capture request (preview) ----
    auto request =
            capture_request_ptr{[](ACameraDevice* device) {
                ACaptureRequest* ptr{};
                // capture as a preview
                // TEMPLATE_RECORD, TEMPLATE_PREVIEW,
                // TEMPLATE_MANUAL,
                const auto status =
                        ACameraDevice_createCaptureRequest(
                                device, TEMPLATE_STILL_CAPTURE, &ptr);
                assert(status == ACAMERA_OK);
                return ptr;
            }(this->device_set[id]),
                                ACaptureRequest_free};
    assert(request.get() != nullptr);

    // `ACaptureRequest` == how to capture
    // detailed config comes here...
    // ACaptureRequest_setEntry_*
    // - ACAMERA_REQUEST_MAX_NUM_OUTPUT_STREAMS
    // -

    // designate target surface in request
    status = ACaptureRequest_addTarget(request.get(), target.get());
    assert(status == ACAMERA_OK);
    // defer    ACaptureRequest_removeTarget;

    // ---- session output ----

    // container for multiplexing of session output
    auto container = capture_session_output_container_ptr{
            []() {
                ACaptureSessionOutputContainer* container{};
                ACaptureSessionOutputContainer_create(&container);
                return container;
            }(),
            ACaptureSessionOutputContainer_free};
    assert(container.get() != nullptr);

    // session output
    auto output = capture_session_output_ptr{
            [=]() {
                ACaptureSessionOutput* output{};
                ACaptureSessionOutput_create(window, &output);
                return output;
            }(),
            ACaptureSessionOutput_free};
    assert(output.get() != nullptr);

    status = ACaptureSessionOutputContainer_add(container.get(), output.get());
    assert(status == ACAMERA_OK);
    // defer ACaptureSessionOutputContainer_remove

    // ---- create a session ----
    status = ACameraDevice_createCaptureSession(
            this->device_set[id], container.get(), std::addressof(on_session_changed),
            std::addressof(this->session_set[id]));
    assert(status == ACAMERA_OK);

    // ---- set request ----
    std::array<ACaptureRequest*, 1> batch_request{};
    batch_request[0] = request.get();

    status = ACameraCaptureSession_capture(
            this->session_set[id], std::addressof(on_capture_event),
            batch_request.size(), batch_request.data(),
            std::addressof(this->seq_id_set[id]));
    assert(status == ACAMERA_OK);

    status =
            ACaptureSessionOutputContainer_remove(container.get(), output.get());
    assert(status == ACAMERA_OK);

    status = ACaptureRequest_removeTarget(request.get(), target.get());
    assert(status == ACAMERA_OK);

    return status;
}

void camera_group_t::stop_capture(uint16_t id) noexcept {
    auto& session = this->session_set[id];
    if (session) {
        Logger::logWarn(100, "stop_capture for session %d ", id);

        // follow `ACameraCaptureSession_capture`
        ACameraCaptureSession_abortCaptures(session);

        ACameraCaptureSession_close(session);
        session = nullptr;
    }
    this->seq_id_set[id] = 0;
}

auto camera_group_t::get_facing(uint16_t id) noexcept -> uint16_t {
    // const ACameraMetadata*
    const auto* metadata = metadata_set[id];

    ACameraMetadata_const_entry entry{};
    ACameraMetadata_getConstEntry(metadata, ACAMERA_LENS_FACING, &entry);

    // lens facing
    const auto facing = *(entry.data.u8);
    assert(
            facing == ACAMERA_LENS_FACING_FRONT || // ACAMERA_LENS_FACING_FRONT
            facing == ACAMERA_LENS_FACING_BACK ||  // ACAMERA_LENS_FACING_BACK
            facing == ACAMERA_LENS_FACING_EXTERNAL // ACAMERA_LENS_FACING_EXTERNAL
    );
    return facing;
}


// ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----
// device callbacks

void context_on_device_disconnected(camera_group_t& context,
                                    ACameraDevice* device) noexcept {
    Logger::logError(100, "on_device_disconnected: %s", ACameraDevice_getId(device));
}

void context_on_device_error(camera_group_t& context, ACameraDevice* device,
                             int error) noexcept {
    Logger::logError(100, "on_device_error: %s", ACameraDevice_getId(device));
}

// session state callbacks

void context_on_session_active(camera_group_t& context,
                               ACameraCaptureSession* session) noexcept {
    Logger::logInfo("on_session_active");
}

void context_on_session_closed(camera_group_t& context,
                               ACameraCaptureSession* session) noexcept {
    Logger::logWarn("on_session_closed");
}

void context_on_session_ready(camera_group_t& context,
                              ACameraCaptureSession* session) noexcept {
    Logger::logInfo("on_session_ready");
}

// capture callbacks

void context_on_capture_started(camera_group_t& context,
                                ACameraCaptureSession* session,
                                const ACaptureRequest* request,
                                uint64_t time_point) noexcept {
    Logger::logDebug(100, "context_on_capture_started: %lu", time_point);
}

void context_on_capture_progressed(camera_group_t& context,
                                   ACameraCaptureSession* session,
                                   ACaptureRequest* request,
                                   const ACameraMetadata* result) noexcept {
    camera_status_t status = ACAMERA_OK;
    ACameraMetadata_const_entry entry{};
    uint64_t time_point = 0;
    // ACAMERA_SENSOR_TIMESTAMP
    // ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE
    // ACAMERA_SENSOR_FRAME_DURATION
    status =
            ACameraMetadata_getConstEntry(result, ACAMERA_SENSOR_TIMESTAMP, &entry);
    if (status == ACAMERA_OK)
        time_point = static_cast<uint64_t>(*(entry.data.i64));

    Logger::logDebug(100, "context_on_capture_progressed: %lu", time_point);
}

void context_on_capture_completed(camera_group_t& context,
                                  ACameraCaptureSession* session,
                                  ACaptureRequest* request,
                                  const ACameraMetadata* result) noexcept {
    camera_status_t status = ACAMERA_OK;
    ACameraMetadata_const_entry entry{};
    uint64_t time_point = 0;
    // ACAMERA_SENSOR_TIMESTAMP
    // ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE
    // ACAMERA_SENSOR_FRAME_DURATION
    status =
            ACameraMetadata_getConstEntry(result, ACAMERA_SENSOR_TIMESTAMP, &entry);
    if (status == ACAMERA_OK)
        time_point = static_cast<uint64_t>(*(entry.data.i64));

    Logger::logDebug(100, "context_on_capture_completed: %lu", time_point);
}

void context_on_capture_failed(camera_group_t& context,
                               ACameraCaptureSession* session,
                               ACaptureRequest* request,
                               ACameraCaptureFailure* failure) noexcept {
    Logger::logError(256, "context_on_capture_failed %ld %d %d %d",
                     failure->frameNumber,
                     failure->reason, failure->sequenceId,
                     failure->wasImageCaptured);
}

void context_on_capture_buffer_lost(camera_group_t& context,
                                    ACameraCaptureSession* session,
                                    ACaptureRequest* request,
                                    ANativeWindow* window,
                                    int64_t frameNumber) noexcept {
    Logger::logError("context_on_capture_buffer_lost");
}

void context_on_capture_sequence_abort(camera_group_t& context,
                                       ACameraCaptureSession* session,
                                       int sequenceId) noexcept {
    Logger::logError("context_on_capture_sequence_abort");
}

void context_on_capture_sequence_complete(camera_group_t& context,
                                          ACameraCaptureSession* session,
                                          int sequenceId,
                                          int64_t frameNumber) noexcept {
    Logger::logDebug("context_on_capture_sequence_complete");
}

// ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----

__attribute__((constructor)) void on_native_lib_attach() noexcept(false) {
    Logger::logError("ATTACHED");
}

__attribute__((destructor)) void on_native_lib_detach() noexcept {
    Logger::logError("DETACHED");
}