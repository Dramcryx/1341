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
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <android/imagedecoder.h>

#include "Logger.h"
#include "StabilizationManager.h"

#include "libyuv/include/libyuv.h"
#include "wrappers/camera/CaptureRequest.h"
#include "wrappers/camera/CameraOutputTarget.h"
#include "wrappers/sensor/Sensor.h"
#include "wrappers/sensor/SensorManager.h"
#include "wrappers/Looper.h"

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
        ARect rect{0, 0, 4000, 3000};
        assert(0 == AHardwareBuffer_lock(this->handle, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, &rect, std::addressof(imageArray)));
    }

    inline void * acquireAndLock(ARect * rect)
    {
        AHardwareBuffer_acquire(this->handle);
        assert(0 == AHardwareBuffer_lock(this->handle, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, rect, std::addressof(imageArray)));
        return this->imageArray;
    }

    inline void unlock()
    {
        AHardwareBuffer_unlock(this->handle, nullptr);
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

class WorkersQueue
{
public:
    WorkersQueue(std::size_t workers = std::thread::hardware_concurrency());

    void setStabInit(std::function<bool(uint8_t *, uint32_t)> cb)
    {
        initStab = cb;
    }

    void setGetStab(std::function<std::pair<int, int>(uint8_t *, uint32_t)> cb)
    {
        getStab = cb;
    }

    ~WorkersQueue();

    struct TaskContext
    {
        uint64_t frameNumber = 0;
        ANativeWindow * surface = nullptr;

        Image image;

        float x;
        float y;
        float t;
    };

    void addToQueue(TaskContext &&buffer);


private:
    std::vector<std::thread> mWorkers;
    std::list<TaskContext> mTasks;

    std::list<std::function<void()>> mSlaveTasks;

    std::function<bool(uint8_t *, uint32_t)> initStab;
    std::function<std::pair<int, int>(uint8_t *, uint32_t)> getStab;

    std::mutex mQueueProtector;
    std::atomic_bool stop = false;
    std::atomic_uint64_t currentFrame = 0;

    std::atomic_bool surfaceUsed;
    void run();
    void run2();
    void run3();
    void run4();
    void run5();

    void run6();
};

struct ImageReader
{
    using ImageCallback = AImageReader_ImageCallback;
    using ImageListener = AImageReader_ImageListener;

    AImageReader * handle = nullptr;
    ImageListener imageListener;

    std::atomic_uint64_t frameCounter = 0;

    StabilizationManager stabilizationManager;
    WorkersQueue queue;

    inline ImageReader(): queue(2)
    {
        auto status = AImageReader_new(4000, 3000, AIMAGE_FORMAT_YUV_420_888, 10, std::addressof(this->handle));
        assert(status == AMEDIA_OK);

        queue.setStabInit([this](uint8_t * p, uint32_t stride){
            return true;//stabilizationManager.setReferenceFrame(p, stride);
        });
        queue.setGetStab([this](uint8_t * p , uint32_t stride) -> std::pair<int, int> {
            return stabilizationManager.trackFeatures(p, stride);
        });
    }

    inline ~ImageReader()
    {
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
        static double x = 0.0;
        static double y = 0.0;
        auto newtime = std::chrono::high_resolution_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(newtime - time).count();
        Logger::logError(50, "Callback time diff: %ld", diff);
        time = newtime;

        WorkersQueue::TaskContext ctx{++frameCounter, reinterpret_cast<ANativeWindow*>(context)};
        ctx.t = diff;
        Logger::logError(64, "FROM SENSOR MANAGER: x %f, y %f", ctx.x, ctx.y);

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
