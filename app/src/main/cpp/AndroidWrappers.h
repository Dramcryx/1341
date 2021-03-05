#ifndef INC_1341_ANDROIDWRAPPERS_H
#define INC_1341_ANDROIDWRAPPERS_H

// STL
#include <vector>
#include <unordered_set>
#include <chrono>
#include <thread>
#include <list>
#include <mutex>

// C
#include <cassert>
#include <arm_neon.h>

// JVM
#include <jni.h>

// Android
#include <android/native_window_jni.h>
#include <camera/NdkCameraMetadataTags.h>
#include <camera/NdkCaptureRequest.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <android/imagedecoder.h>
#include <android/looper.h>
#include <android/sensor.h>

#include "Logger.h"

#include "libyuv/include/libyuv.h"
#include "CL/opencl.h"

namespace wrappers
{

struct NativeWindow
{
    ANativeWindow * handle = nullptr;
    inline NativeWindow() = default;
    inline NativeWindow(JNIEnv* env, jobject surface)
    {
        handle = ANativeWindow_fromSurface(env, surface);
    }

    inline ~NativeWindow()
    {
        ANativeWindow_release(handle);
    }
};

struct CaptureRequest
{
    ACaptureRequest * handle = nullptr;

    std::vector<ACameraOutputTarget*> targets;

    inline camera_status_t setTargetFpsRange(int from, int to)
    {
        const int range[2] = {from, to};
        return ACaptureRequest_setEntry_i32(this->handle, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE, 2, range);
    }

    inline camera_status_t setNoiseReductionMode(acamera_metadata_enum_acamera_noise_reduction_mode mode)
    {
        const uint8_t downcast = mode;
        return ACaptureRequest_setEntry_u8(this->handle, ACAMERA_NOISE_REDUCTION_MODE, 1, &downcast);
    }

    inline camera_status_t setAFMode(acamera_metadata_enum_acamera_control_af_mode mode)
    {
        const uint8_t downcast = mode;
        return ACaptureRequest_setEntry_u8(this->handle, ACAMERA_CONTROL_AF_MODE, 1, &downcast);
    }

    inline camera_status_t setTonemapMode(acamera_metadata_enum_acamera_tonemap_mode mode)
    {
        const uint8_t downcast = mode;
        return ACaptureRequest_setEntry_u8(this->handle, ACAMERA_TONEMAP_MODE, 1, &downcast);
    }

    inline camera_status_t addTarget(ACameraOutputTarget * output)
    {
        auto result = ACaptureRequest_addTarget(this->handle, output);
        if (result == ACAMERA_OK)
        {
            targets.push_back(output);
        }
        return result;
    }

    inline ~CaptureRequest()
    {
        for (auto i: targets)
        {
            ACaptureRequest_removeTarget(this->handle, i);
        }
        ACaptureRequest_free(handle);
    }
};

struct CameraOutputTarget
{
    ACameraOutputTarget * handle = nullptr;
    inline CameraOutputTarget(ANativeWindow * window)
    {
        auto status = ACameraOutputTarget_create(window, std::addressof(this->handle));
        assert(status == ACAMERA_OK);
    }

    inline ~CameraOutputTarget()
    {
        ACameraOutputTarget_free(this->handle);
    }

};

struct CaptureSessionOutput
{
    ACaptureSessionOutput * handle = nullptr;
    inline CaptureSessionOutput(ANativeWindow * window)
    {
        auto status = ACaptureSessionOutput_create(window, std::addressof(this->handle));
        assert(status == ACAMERA_OK);
    }

    inline ~CaptureSessionOutput()
    {
        ACaptureSessionOutput_free(this->handle);
    }
};

struct CaptureSessionSharedOutput
{
    ACaptureSessionOutput * handle = nullptr;
    std::unordered_set<ANativeWindow *> outputs;
    inline CaptureSessionSharedOutput(ANativeWindow * window)
    {
        auto status = ACaptureSessionSharedOutput_create(window, std::addressof(this->handle));
    }

    inline ~CaptureSessionSharedOutput()
    {
        for (auto i: outputs)
        {
            ACaptureSessionSharedOutput_remove(this->handle, i);
        }
        ACaptureSessionOutput_free(this->handle);
    }

    inline camera_status_t add(ANativeWindow * target)
    {
        auto status = ACaptureSessionSharedOutput_add(this->handle, target);
        if (status == ACAMERA_OK)
        {
            outputs.insert(target);
        }
        return status;
    }

    inline camera_status_t remove(ANativeWindow * target)
    {
        auto status = ACaptureSessionSharedOutput_remove(this->handle, target);
        if (status == ACAMERA_OK)
        {
            outputs.erase(target);
        }
        return status;
    }
};

struct CaptureSessionOutputContainer
{
    ACaptureSessionOutputContainer *handle = nullptr;

    std::unordered_set<ACaptureSessionOutput*> outputs;
    inline CaptureSessionOutputContainer()
    {
        ACaptureSessionOutputContainer_create(std::addressof(this->handle));
        outputs.reserve(3);
    }

    inline ~CaptureSessionOutputContainer()
    {
        for (auto i: outputs)
        {
            ACaptureSessionOutputContainer_remove(this->handle, i);
        }
        ACaptureSessionOutputContainer_free(this->handle);
    }

    inline camera_status_t add(ACaptureSessionOutput * target)
    {
        auto status = ACaptureSessionOutputContainer_add(this->handle, target);
        if (status == ACAMERA_OK)
        {
            outputs.insert(target);
        }
        return status;
    }

    inline camera_status_t remove(ACaptureSessionOutput * target)
    {
        auto status = ACaptureSessionOutputContainer_add(this->handle, target);
        if (status == ACAMERA_OK)
        {
            outputs.erase(target);
        }
        return status;
    }
};

// TODO
struct CameraDevice
{
    ACameraDevice * handle = nullptr;
    inline std::unique_ptr<CaptureRequest> createCaptureRequest(ACameraDevice_request_template templ)
    {
        auto retval = std::make_unique<CaptureRequest>();
        ACameraDevice_createCaptureRequest(handle, templ, std::addressof(retval->handle));
        return retval;
    }

    inline camera_status_t createCaptureSessionWithSessionParameters(
            const ACaptureSessionOutputContainer* outputs,
            const ACaptureRequest* sessionParameters,
            const ACameraCaptureSession_stateCallbacks* callbacks,
            /*out*/ACameraCaptureSession** session)
    {
        return ACameraDevice_createCaptureSessionWithSessionParameters(this->handle, outputs, sessionParameters, callbacks, session);
    }
};

// TODO
struct HardwareBuffer
{
    using Desc = AHardwareBuffer_Desc;
    using Plane = AHardwareBuffer_Plane;
    using Planes = AHardwareBuffer_Planes;

    AHardwareBuffer * handle = nullptr;
    void * imageArray = nullptr;

    inline HardwareBuffer() = default;

    inline HardwareBuffer(const Desc & desc)
    {
        int result = AHardwareBuffer_allocate(&desc, std::addressof(this->handle));
        if (result != 0)
        {
            Logger::logError(128, "%s can't allocate buffer: %d", __FUNCTION__ , result);
        }
    }

    inline void acquireAndLock()
    {
        AHardwareBuffer_acquire(this->handle);
//        ARect rect{36000008, 1};
        ARect rect{0, 0, 4000, 3000};
//        ARect rect{0, 0, 5008*3000, 0};
        assert(0 == AHardwareBuffer_lock(this->handle, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, &rect, std::addressof(imageArray)));
    }

    inline ~HardwareBuffer()
    {
        if (this->handle) {
            AHardwareBuffer_unlock(this->handle, nullptr);
            AHardwareBuffer_release(this->handle);
        }
    }

    inline Desc describe()
    {
        Desc retval;
        AHardwareBuffer_describe(this->handle, std::addressof(retval));
        return retval;
    }

    inline Planes lockPlanes(ARect * rect)
    {
        Planes retval{};
        int result = AHardwareBuffer_lockPlanes(this->handle, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, rect, &retval);
        if (result != 0)
        {
            Logger::logError(128, "%s can't lock planes: %d", __FUNCTION__ , result);
        }
        return retval;
    }
};

// TODO
struct Image
{
    //AImage * handle = nullptr;

    std::shared_ptr<AImage> handle;

    Image() = default;
    inline Image(AImage * pointer)
    {
        handle = std::shared_ptr<AImage>(pointer, AImage_delete);
    }

    inline media_status_t getHardwareBuffer(HardwareBuffer & hardwareBuffer)
    {
        return AImage_getHardwareBuffer(this->handle.get(), std::addressof(hardwareBuffer.handle));
    }

    inline media_status_t getPlanePixelStride(int planeIdx, int32_t * pixelStride)
    {
        return AImage_getPlanePixelStride(this->handle.get(), planeIdx, pixelStride);
    }

    inline media_status_t getNumberOfPlanes(int32_t * numberOfPlanes)
    {
        return AImage_getNumberOfPlanes(this->handle.get(), numberOfPlanes);
    }

    inline media_status_t getPlaneData(int planeIdx, uint8_t ** data, int32_t * dataLength)
    {
        return AImage_getPlaneData(this->handle.get(), planeIdx, data, dataLength);
    }

    inline media_status_t getPlaneRowStride(int planeIdx, int32_t * rowStride)
    {
        return AImage_getPlaneRowStride(this->handle.get(), planeIdx, rowStride);
    }
};

struct Looper
{
    ALooper * handle = nullptr;

    inline Looper()
    {
        handle = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    }

    inline ~Looper()
    {
        ALooper_release(handle);
    }

    static int pollAll(int timeoutMillis, int *outFd, int *outEvents, void **outData)
    {
        return ALooper_pollAll(timeoutMillis, outFd, outEvents, outData);
    }

    static int pollOnce(int timeoutMillis, int *outFd, int *outEvents, void **outData)
    {
        return ALooper_pollOnce(timeoutMillis, outFd, outEvents, outData);
    }
};

struct Sensor
{
    const ASensor * handle = nullptr;
    operator const ASensor *()
    {
        return handle;
    }

    operator ASensor *()
    {
        return const_cast<ASensor*>(handle);
    }

    const char * getName() const
    {
        return ASensor_getName(handle);
    }

    float getResolution()
    {
        return ASensor_getResolution(handle);
    }

};

struct SensorManager {

    // SUBCLASS DECLARATIONS
    struct SensorList
    {
        ASensorList list;
        int count;
    };

    struct SensorEventQueue {
        ASensorEventQueue *handle = nullptr;

        std::vector<ASensor *> enabledSensors;

        inline ~SensorEventQueue() {
            for (auto i: enabledSensors) {
                ASensorEventQueue_disableSensor(handle, i);
            }
        }

        int enableSensor(ASensor *sensor) {
            int ret = ASensorEventQueue_enableSensor(handle, sensor);
            if (!ret) {
                enabledSensors.push_back(sensor);
            }
            return ret;
        }
    };
    // SUBCLASS DECLARATIONS END

    ASensorManager * handle = nullptr;

    static SensorManager getInstanceForPackage()
    {
        return {ASensorManager_getInstanceForPackage("com.dramcryx.cam1341")};
    }

    SensorList getSensorList()
    {
        SensorList ret{};
        ret.count = ASensorManager_getSensorList(handle, &ret.list);
        return ret;
    }

    SensorEventQueue createEventQueue(Looper & looper, int looperId, ALooper_callbackFunc callback, void * data)
    {
        return {ASensorManager_createEventQueue(handle, looper.handle, looperId, callback, data)};
    }

    Sensor getDefaultSensor(int type)
    {
        return {ASensorManager_getDefaultSensor(handle, type)};
    }

    int destroyEventQueue(SensorEventQueue & queue)
    {
        return ASensorManager_destroyEventQueue(handle, std::exchange(queue.handle, nullptr));
    }
};

class WorkersQueue
{
public:
    WorkersQueue(std::size_t workers = std::thread::hardware_concurrency());

    ~WorkersQueue();

    struct TaskContext
    {
        uint64_t frameNumber = 0;
        ANativeWindow * surface = nullptr;

        Image image;

    };

    void addToQueue(TaskContext &&buffer);


private:
    std::vector<std::thread> mWorkers;
    std::list<TaskContext> mTasks;

    std::list<std::function<void()>> mSlaveTasks;

    std::mutex mQueueProtector;
    std::atomic_bool stop = false;
    std::atomic_uint64_t currentFrame = 0;

    std::atomic_bool surfaceUsed;

    cl_int ret = 0;
    cl_platform_id platform_id;
    cl_uint ret_num_platforms = 0;
    cl_context context;
    cl_device_id device_id;
    cl_uint ret_num_devices = 0;
    cl_command_queue command_queue;
    cl_program program = nullptr;
    cl_kernel kernel = nullptr;
    void run();

    void run2();
    void run3();

    void run4();

    void run5();
};

struct ImageReader
{
    using ImageCallback = AImageReader_ImageCallback;
    using ImageListener = AImageReader_ImageListener;

    AImageReader * handle = nullptr;
    ImageListener imageListener;


    std::atomic_uint64_t frameCounter = 0;
    std::unique_ptr<Looper> looper;
    SensorManager sensorManager;
    SensorManager::SensorEventQueue sensorEventQueue;
    std::once_flag flg; // for looper

    WorkersQueue queue;

    inline ImageReader(): queue(2)
    {
        auto status = AImageReader_new(4000, 3000, AIMAGE_FORMAT_YUV_420_888, 3, std::addressof(this->handle));
        assert(status == AMEDIA_OK);
        sensorManager = SensorManager::getInstanceForPackage();

    }

    inline ~ImageReader()
    {
        sensorManager.destroyEventQueue(sensorEventQueue);
        AImageReader_delete(this->handle);
    }

    inline media_status_t setImageListener(void * context, ImageCallback callback)
    {
        this->imageListener = {context, callback};
        return AImageReader_setImageListener(this->handle, std::addressof(imageListener));
    }

    inline media_status_t acquireNextImage(Image & image)
    {
        AImage * pointer = nullptr;
        auto status = AImageReader_acquireNextImage(this->handle, std::addressof(pointer));
        image.handle = std::shared_ptr<AImage>(pointer, AImage_delete);
        return status;
    }

    inline media_status_t getWindow(NativeWindow & nativeWindow)
    {
        return AImageReader_getWindow(this->handle, std::addressof(nativeWindow.handle));
    }

    inline void operator()(void * context, AImageReader* reader)
    {
        static auto time = std::chrono::high_resolution_clock::now();
        auto newtime = std::chrono::high_resolution_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(newtime - time).count();
        Logger::logError(50, "Callback time diff: %ld", diff);
        time = newtime;


        std::call_once(flg, [this](){
            sensorEventQueue = sensorManager.createEventQueue(*(looper = std::make_unique<Looper>()) , 1, nullptr, nullptr);
            auto gyroscope = sensorManager.getDefaultSensor(ASENSOR_TYPE_GYROSCOPE);
            auto accelerometer = sensorManager.getDefaultSensor(ASENSOR_TYPE_ACCELEROMETER);
            sensorEventQueue.enableSensor(gyroscope);
            sensorEventQueue.enableSensor(accelerometer);
        });

//        int ident = Looper::pollAll(16, nullptr, nullptr, nullptr);
//
//        ASensorEvent sensorEvent[2];
//        ASensorEventQueue_getEvents(sensorEventQueue.handle, sensorEvent, 2);
//        for (auto &i: sensorEvent)
//        {
//            if (i.type == ASENSOR_TYPE_ACCELEROMETER)
//            {
//                Logger::logInfo(128, "Accelerometer data: x %f, y %f, z %f", i.acceleration.x, i.acceleration.y, i.acceleration.z);
//            }
//            if (i.type == ASENSOR_TYPE_GYROSCOPE)
//            {
//                Logger::logInfo(128, "Gyroscope data (rad/s): x %f, y %f, z %f", i.data[0], i.data[1], i.data[2]);
//            }
//        }

        WorkersQueue::TaskContext ctx{++frameCounter, reinterpret_cast<ANativeWindow*>(context)};
        Logger::ms(this->acquireNextImage(ctx.image));

        if (ctx.image.handle) {
            queue.addToQueue(std::move(ctx));
        }
        else {
            Logger::logError("OUT OF BUFFER!");
        }
    }
};


}

#endif //INC_1341_ANDROIDWRAPPERS_H
