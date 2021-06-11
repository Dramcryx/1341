#include "AndroidWrappers.h"

#include <array>
#include "fastcv.h"

wrappers::WorkersQueue::WorkersQueue(std::size_t workers)
{
    //fcvMemInitPreAlloc(1024 * 1024 * 128);
    //fcvSetOperationMode(fcvOperationMode::FASTCV_OP_PERFORMANCE);
    mWorkers.reserve(workers);
    for (int i = 0; i < mWorkers.capacity(); ++i) {
        mWorkers.emplace_back(&WorkersQueue::run6, this);
    }
}

wrappers::WorkersQueue::~WorkersQueue()
{
    stop = true;
    for (auto & i: mWorkers)
    {
        if (i.joinable())
        {
            i.join();
        }
    }
    //fcvMemDeInit();
}

void wrappers::WorkersQueue::addToQueue(wrappers::WorkersQueue::TaskContext &&buffer)
{
    std::lock_guard<std::mutex> lockGuard(mQueueProtector);
    mTasks.push_back(buffer);
    Logger::logError(32, "QUEUE SIZE: %d", mTasks.size());
}

void wrappers::WorkersQueue::run()
{
    AHardwareBuffer_Desc desc{};
    desc.width = 3200;
    desc.height = 1800;
    desc.layers = 1;
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;

    AHardwareBuffer * hwb = nullptr;
    AHardwareBuffer_allocate(&desc, &hwb);
    std::unique_ptr<AHardwareBuffer, void(*)(AHardwareBuffer *)> localBuffer{hwb, AHardwareBuffer_release};

    while (!stop)
    {
        std::unique_lock<std::mutex> lockGuard(mQueueProtector);
        if (!mTasks.empty())
        {
            Logger::logError(32, "QUEUE SIZE: %d", mTasks.size());
            auto task = mTasks.front();
            mTasks.pop_front();
            lockGuard.unlock();

            auto startProcess = std::chrono::high_resolution_clock::now();
            // LOCK SOURCE IMAGE
            HardwareBuffer imageBuffer{};
            task.image.getHardwareBuffer(imageBuffer);
            imageBuffer.acquireAndLock();

            // GET YUV PLANES INFO
            uint8_t * y = nullptr;
            int32_t y_count = 0;
            uint8_t * u = nullptr;
            int32_t u_count = 0;
            uint8_t * v = nullptr;
            int32_t v_count = 0;

            task.image.getPlaneData(0, &y, &y_count);
            task.image.getPlaneData(2, &u, &u_count);
            task.image.getPlaneData(1, &v, &v_count);

            int32_t y_stride = 0;
            Logger::ms(AImage_getPlaneRowStride(task.image.handle.get(), 0, &y_stride));

            // GET HARDWARE BUFFER DESCRIPTION
            auto imageBufferDesc = imageBuffer.describe();

            // LOCK TEMPORARY BUFFER
            void *bufferRaw = nullptr;
            ARect rect2{0, 0, 3200, 1800};

            AHardwareBuffer_lock(localBuffer.get(), AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1,
                                 &rect2, &bufferRaw);

            AHardwareBuffer_Desc hwbdesc;
            AHardwareBuffer_describe(localBuffer.get(), &hwbdesc);
            // CONVERT IMAGE TO TEMPORARY BUFFER
            libyuv::NV12ToARGBMatrix(y, y_stride, u, y_stride, (uint8_t *) bufferRaw, hwbdesc.stride * 4,
                               &libyuv::kYuvV2020Constants, hwbdesc.width, hwbdesc.height);

            // IN-PLACE SCALE-DOWN
            libyuv::ARGBScale((uint8_t *) bufferRaw, hwbdesc.stride * 4, hwbdesc.width,
                              hwbdesc.height, (uint8_t *) bufferRaw, 1920 * 4, 1920, 1080,
                              libyuv::kFilterBox);



            // ROTATE TEMPORARY BUFFER BACK INTO IMAGE (REUSE IT'S MEMORY)
            libyuv::ARGBRotate((uint8_t *)bufferRaw, 1920 * 4, y, 4352, 1920, 1080, libyuv::kRotate90);
            AHardwareBuffer_unlock(localBuffer.get(), nullptr);

            if (currentFrame > task.frameNumber)
            {
                Logger::logInfo(32, "TRYING TO DRAW OLDER FRAME");
                continue;
            }

            bool wannause = false;
            if (surfaceUsed.compare_exchange_strong(wannause, true)){


            // LOCK PREVIEW SURFACE
            ANativeWindow_Buffer surfaceBuffer{};
            ARect rect{0, 0, 1080, 1920};
            auto isSurfaceLockFailed = ANativeWindow_lock(task.surface, &surfaceBuffer, &rect);

                auto redraw_start = std::chrono::high_resolution_clock::now();
                auto out = (uint8_t *) surfaceBuffer.bits;

                libyuv::ARGBCopy(y, surfaceBuffer.stride * 4, out, surfaceBuffer.stride * 4,
                                 surfaceBuffer.width, surfaceBuffer.height);

                //libyuv::ARGBRotate((uint8_t *)bufferRaw, 1920 * 4, out, surfaceBuffer.stride * 4, surfaceBuffer.height, surfaceBuffer.width, libyuv::kRotate90);

                // WORKS 1920x1080
//                libyuv::NV12ToARGB(y, y_stride, u, y_stride, (uint8_t *)bufferRaw, 1920 * 4, 1920, 1080);
//                libyuv::ARGBRotate((uint8_t *)bufferRaw, 1920 * 4, out, surfaceBuffer.stride * 4, surfaceBuffer.height, surfaceBuffer.width, libyuv::kRotate90);
                //^^^^^^^^^

//                AHardwareBuffer_unlock(localBuffer.get(), nullptr);

                // WORKS ROTATED
                //libyuv::NV12ToARGB(y, y_stride, u, y_stride, out, surfaceBuffer.stride * 4, surfaceBuffer.width, surfaceBuffer.height);
                // ^^^^^^^^^

                auto redraw_end = std::chrono::high_resolution_clock::now();

                isSurfaceLockFailed = ANativeWindow_unlockAndPost(task.surface);
                currentFrame = task.frameNumber;
                surfaceUsed = false;
                Logger::logError(100, "TIME TO REDRAW: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         redraw_end - redraw_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "FULL PROCEDURE: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         redraw_end - startProcess).count(),
                                 currentFrame.load(std::memory_order_relaxed));
            }
        }

    }
}

void wrappers::WorkersQueue::run2()
{
    AHardwareBuffer_Desc desc{};
    desc.width = 1920;
    desc.height = 1080;
    desc.layers = 1;
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;

    AHardwareBuffer * hwb = nullptr;
    AHardwareBuffer_allocate(&desc, &hwb);
    std::unique_ptr<AHardwareBuffer, void(*)(AHardwareBuffer *)> localBuffer{hwb, AHardwareBuffer_release};

    while (!stop)
    {
        std::unique_lock<std::mutex> lockGuard(mQueueProtector);
        if (!mTasks.empty())
        {
            auto task = mTasks.front();
            mTasks.pop_front();
            lockGuard.unlock();

            auto startProcess = std::chrono::high_resolution_clock::now();

            // LOCK SOURCE IMAGE
            HardwareBuffer imageBuffer{};
            task.image.getHardwareBuffer(imageBuffer);
            imageBuffer.acquireAndLock();

            // GET YUV PLANES INFO
            uint8_t * y = nullptr;
            int32_t y_count = 0;
            uint8_t * u = nullptr;
            int32_t u_count = 0;
            uint8_t * v = nullptr;
            int32_t v_count = 0;

            task.image.getPlaneData(0, &y, &y_count);
            task.image.getPlaneData(2, &u, &u_count);
            task.image.getPlaneData(1, &v, &v_count);

            int32_t y_stride = 0;
            Logger::ms(AImage_getPlaneRowStride(task.image.handle.get(), 0, &y_stride));

            // GET HARDWARE BUFFER DESCRIPTION
            auto imageBufferDesc = imageBuffer.describe();

            // LOCK TEMPORARY BUFFER
            void *bufferRaw = nullptr;
            ARect rect2{0, 0, 1920, 1080};

            AHardwareBuffer_lock(localBuffer.get(), AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1,
                                 &rect2, &bufferRaw);

            AHardwareBuffer_Desc hwbdesc;
            AHardwareBuffer_describe(localBuffer.get(), &hwbdesc);
            // IN-PLACE SCALE-DOWN
            libyuv::NV12Scale(y, y_stride, u, y_stride, 3200, 1800, y, 1920, u, 1920, 1920, 1080, libyuv::kFilterBox);
            libyuv::NV12ToARGBMatrix(y, 1920, u, 1920, (uint8_t *) bufferRaw, hwbdesc.stride * 4,
                                     &libyuv::kYuvV2020Constants, hwbdesc.width, hwbdesc.height);


            // ROTATE TEMPORARY BUFFER BACK INTO IMAGE (REUSE IT'S MEMORY)
            libyuv::ARGBRotate((uint8_t *)bufferRaw, 1920 * 4, y, 4352, 1920, 1080, libyuv::kRotate90);
            AHardwareBuffer_unlock(localBuffer.get(), nullptr);

            if (currentFrame > task.frameNumber)
            {
                Logger::logInfo(32, "TRYING TO DRAW OLDER FRAME");
                continue;
            }

            bool wannause = false;
            if (surfaceUsed.compare_exchange_strong(wannause, true)) {
                // LOCK PREVIEW SURFACE
                ANativeWindow_Buffer surfaceBuffer{};
                ARect rect{0, 0, 1080, 1920};
                auto isSurfaceLockFailed = ANativeWindow_lock(task.surface, &surfaceBuffer, &rect);

                auto redraw_start = std::chrono::high_resolution_clock::now();
                auto out = (uint8_t *) surfaceBuffer.bits;

                libyuv::ARGBCopy(y, surfaceBuffer.stride * 4, out, surfaceBuffer.stride * 4,
                                 surfaceBuffer.width, surfaceBuffer.height);

                auto redraw_end = std::chrono::high_resolution_clock::now();

                isSurfaceLockFailed = ANativeWindow_unlockAndPost(task.surface);
                currentFrame = task.frameNumber;
                surfaceUsed = false;
                Logger::logError(100, "TIME TO REDRAW: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         redraw_end - redraw_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "FULL PROCEDURE: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         redraw_end - startProcess).count(),
                                 currentFrame.load(std::memory_order_relaxed));
            }
        }

    }
}

void wrappers::WorkersQueue::run3()
{
    // STABLE BUT SLOW
    // RUN2 BUT PARALLEL CONVERT AND ROTATION
    AHardwareBuffer_Desc desc{};
    desc.width = 1920;
    desc.height = 1080;
    desc.layers = 1;
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;

    AHardwareBuffer * hwb = nullptr;
    AHardwareBuffer_allocate(&desc, &hwb);
    std::unique_ptr<AHardwareBuffer, void(*)(AHardwareBuffer *)> localBuffer{hwb, AHardwareBuffer_release};

    constexpr auto slavecount = 3;
    std::thread slaves[slavecount];

    while (!stop)
    {
        std::unique_lock<std::mutex> lockGuard(mQueueProtector);
        if (!mTasks.empty())
        {
            auto task = mTasks.front();
            mTasks.pop_front();
            lockGuard.unlock();

            auto startProcess = std::chrono::high_resolution_clock::now();

            // LOCK SOURCE IMAGE
            HardwareBuffer imageBuffer{};
            task.image.getHardwareBuffer(imageBuffer);
            imageBuffer.acquireAndLock();

            // GET YUV PLANES INFO
            uint8_t * y = nullptr;
            int32_t y_count = 0;
            uint8_t * u = nullptr;
            int32_t u_count = 0;
            uint8_t * v = nullptr;
            int32_t v_count = 0;

            task.image.getPlaneData(0, &y, &y_count);
            task.image.getPlaneData(2, &u, &u_count);
            task.image.getPlaneData(1, &v, &v_count);

            int32_t y_stride = 0;
            Logger::ms(AImage_getPlaneRowStride(task.image.handle.get(), 0, &y_stride));

            // GET HARDWARE BUFFER DESCRIPTION
            auto imageBufferDesc = imageBuffer.describe();

            // LOCK TEMPORARY BUFFER
            void *bufferRaw = nullptr;
            ARect rect2{0, 0, 1920, 1080};

            AHardwareBuffer_lock(localBuffer.get(), AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1,
                                 &rect2, &bufferRaw);

            AHardwareBuffer_Desc hwbdesc;
            AHardwareBuffer_describe(localBuffer.get(), &hwbdesc);
            // IN-PLACE SCALE-DOWN
            //libyuv::NV12Scale(y, y_stride, u, y_stride, 3200, 1800, y, 1920, u, 1920, 1920,1080, libyuv::kFilterBox);

            auto scale_start = std::chrono::high_resolution_clock::now();

            slaves[0] = std::thread([=](){
                libyuv::ScalePlane(y, y_stride, 3840, 2160, y, 1920, 1920,1080, libyuv::kFilterBox);
            });

            slaves[1] = std::thread([=](){
                libyuv::UVScale(u, y_stride, 3840 / 2, 2160 / 2, u, 1920, 1920 / 2, 1080 / 2, libyuv::kFilterBox);
            });
            for (auto &i: slaves) {
                if (i.joinable()) {
                    i.join();
                }
            }
            auto scale_end = std::chrono::high_resolution_clock::now();
            constexpr auto yhstep = 1080 / slavecount;
            constexpr auto uvhstep = yhstep / 2;

            auto argb_start = std::chrono::high_resolution_clock::now();
            for (int i = 0 ; i < slavecount; ++i) {

                slaves[i] = std::thread([=](){
                    libyuv::NV12ToARGBMatrix(y + 1920 * yhstep * i, 1920, u + 1920 * uvhstep * i, 1920, (uint8_t *) bufferRaw + hwbdesc.stride * 4 * yhstep * i, hwbdesc.stride * 4,
                                             &libyuv::kYuvV2020Constants, hwbdesc.width, hwbdesc.height / slavecount);
                });
            }
            for (auto &i: slaves) {
                if (i.joinable()) {
                    i.join();
                }
            }
            auto argb_end = std::chrono::high_resolution_clock::now();

            auto rotate_start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < slavecount; ++i) {
                constexpr auto src_stride = 1920 * 4;
                constexpr auto dst_stride = 4352;
                constexpr auto width = 1920;
                constexpr auto height = 1080;
                auto heightstep = height / slavecount;
                slaves[i] = std::thread([=]() {
                    // ROTATE TEMPORARY BUFFER BACK INTO IMAGE (REUSE IT'S MEMORY)
                    libyuv::ARGBRotate((uint8_t *) bufferRaw + (src_stride * heightstep * i), src_stride, y + 4320 / slavecount * (slavecount - 1 - i),
                                       dst_stride,
                                       width,
                                       height / slavecount,
                                       libyuv::kRotate90);
                });
            }
            for (auto &i: slaves) {
                if (i.joinable()) {
                    i.join();
                }
            }
            auto rotate_end = std::chrono::high_resolution_clock::now();
            AHardwareBuffer_unlock(localBuffer.get(), nullptr);

            if (currentFrame > task.frameNumber)
            {
                Logger::logInfo(32, "TRYING TO DRAW OLDER FRAME");
                continue;
            }

            bool wannause = false;
            if (surfaceUsed.compare_exchange_strong(wannause, true)) {
                // LOCK PREVIEW SURFACE
                ANativeWindow_Buffer surfaceBuffer{};
                ARect rect{0, 0, 1080, 1920};
                auto isSurfaceLockFailed = ANativeWindow_lock(task.surface, &surfaceBuffer, &rect);

                auto redraw_start = std::chrono::high_resolution_clock::now();
                auto out = (uint8_t *) surfaceBuffer.bits;

                libyuv::ARGBCopy(y, surfaceBuffer.stride * 4, out, surfaceBuffer.stride * 4,
                                 surfaceBuffer.width, surfaceBuffer.height);

                auto redraw_end = std::chrono::high_resolution_clock::now();

                isSurfaceLockFailed = ANativeWindow_unlockAndPost(task.surface);
                currentFrame = task.frameNumber;
                surfaceUsed = false;
                Logger::logError(100, "TIME TO SCALE-DOWN: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         scale_end - scale_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "TIME TO MAKE ARGB: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         argb_end - argb_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "TIME TO ROTATE: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         rotate_end - rotate_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "TIME TO REDRAW: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         redraw_end - redraw_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "FULL PROCEDURE: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         redraw_end - startProcess).count(),
                                 currentFrame.load(std::memory_order_relaxed));
            }
        }

    }
}

void wrappers::WorkersQueue::run4()
{
    // OUTPUT WITH ARTIFACTS BUT FAST
    // RUN2 BUT PARALLEL CONVERT AND ROTATION
    AHardwareBuffer_Desc desc{};
    desc.width = 1920;
    desc.height = 1080;
    desc.layers = 1;
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;

    AHardwareBuffer * hwb = nullptr;
    AHardwareBuffer_allocate(&desc, &hwb);
    std::unique_ptr<AHardwareBuffer, void(*)(AHardwareBuffer *)> localBuffer{hwb, AHardwareBuffer_release};

    constexpr auto slavecount = 3;
    std::thread slaves[slavecount];

    while (!stop)
    {
        std::unique_lock<std::mutex> lockGuard(mQueueProtector);
        if (!mTasks.empty())
        {
            auto task = mTasks.front();
            mTasks.pop_front();
            lockGuard.unlock();

            auto startProcess = std::chrono::high_resolution_clock::now();

            // LOCK SOURCE IMAGE
            HardwareBuffer imageBuffer{};
            task.image.getHardwareBuffer(imageBuffer);
            imageBuffer.acquireAndLock();

            // GET YUV PLANES INFO
            uint8_t * y = nullptr;
            int32_t y_count = 0;
            uint8_t * u = nullptr;
            int32_t u_count = 0;
            uint8_t * v = nullptr;
            int32_t v_count = 0;

            task.image.getPlaneData(0, &y, &y_count);
            task.image.getPlaneData(2, &u, &u_count);
            task.image.getPlaneData(1, &v, &v_count);

            int32_t y_stride = 0;
            Logger::ms(AImage_getPlaneRowStride(task.image.handle.get(), 0, &y_stride));

            int32_t u_stride = 0;
            Logger::ms(AImage_getPlaneRowStride(task.image.handle.get(), 2, &u_stride));

            int32_t v_stride = 0;
            Logger::ms(AImage_getPlaneRowStride(task.image.handle.get(), 1, &v_stride));

            // GET HARDWARE BUFFER DESCRIPTION
            auto imageBufferDesc = imageBuffer.describe();


            // IN-PLACE SCALE-DOWN
            //libyuv::NV12Scale(y, y_stride, u, y_stride, 3200, 1800, y, 1920, u, 1920, 1920,1080, libyuv::kFilterBox);

            auto scale_start = std::chrono::high_resolution_clock::now();

            libyuv::ScalePlane(y, y_stride, 3840, 2160, y, 1920, 1920, 1080, libyuv::kFilterBox);
            libyuv::UVScale(u, y_stride, 3840 / 2, 2160 / 2, u, 1920, 1920 / 2, 1080 / 2, libyuv::kFilterBox);

            auto scale_end = std::chrono::high_resolution_clock::now();

            // LOCK TEMPORARY BUFFER
            uint8_t * bufferRaw = nullptr;
            ARect rect2{0, 0, 1920, 1080};

            AHardwareBuffer_lock(localBuffer.get(), AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1,
                                 &rect2, (void**)&bufferRaw);

            AHardwareBuffer_Desc hwbdesc;
            AHardwareBuffer_describe(localBuffer.get(), &hwbdesc);
            auto rotate_start = std::chrono::high_resolution_clock::now();

            auto yout = bufferRaw;
            auto uout = bufferRaw + 1920 * 1080 * 4;
            auto vout = bufferRaw + 1920 * 1080 * 8;
            auto uvout = bufferRaw + 1920 * 1080 * 10;


            libyuv::RotatePlane90(y, 1920, yout, 1080, 1920, 1080);

            libyuv::RotateUV90(u,
                               1920,
                               uout,
                               1080 / 2,
                               vout,
                               1080 / 2,
                               1920 / 2,
                               1080 / 2);

            libyuv::MergeUVPlane(uout,
                                 1080 / 2,
                                 vout,
                                 1080 / 2,
                                 uvout,
                                 1080,
                                 1080 / 2,
                                 1920 / 2);
            auto rotate_end = std::chrono::high_resolution_clock::now();

            constexpr auto yhstep = 1920 / slavecount;
            constexpr auto uvhstep = yhstep / 2;

            auto argb_start = std::chrono::high_resolution_clock::now();
            libyuv::NV12ToARGBMatrix(yout,
                                             1080,
                                             uvout,
                                             1080,
                                             y,
                                             1080 * 4,
                                             &libyuv::kYuvV2020Constants,
                                             1080,
                                             1920);
            auto argb_end = std::chrono::high_resolution_clock::now();

            AHardwareBuffer_unlock(localBuffer.get(), nullptr);

            if (currentFrame > task.frameNumber)
            {
                Logger::logInfo(32, "TRYING TO DRAW OLDER FRAME");
                continue;
            }

            bool wannause = false;
            if (surfaceUsed.compare_exchange_strong(wannause, true)) {
                // LOCK PREVIEW SURFACE
                ANativeWindow_Buffer surfaceBuffer{};
                ARect rect{0, 0, 1080, 1920};
                auto isSurfaceLockFailed = ANativeWindow_lock(task.surface, &surfaceBuffer, &rect);

                auto redraw_start = std::chrono::high_resolution_clock::now();
                auto out = (uint8_t *) surfaceBuffer.bits;

                libyuv::ARGBCopy(y, 1080 * 4, out, surfaceBuffer.stride * 4,
                                 surfaceBuffer.width, surfaceBuffer.height);

                auto redraw_end = std::chrono::high_resolution_clock::now();

                isSurfaceLockFailed = ANativeWindow_unlockAndPost(task.surface);
                currentFrame = task.frameNumber;
                surfaceUsed = false;
                Logger::logError(100, "TIME TO SCALE-DOWN: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         scale_end - scale_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "TIME TO MAKE ARGB: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         argb_end - argb_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "TIME TO ROTATE: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         rotate_end - rotate_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "TIME TO REDRAW: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         redraw_end - redraw_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "FULL PROCEDURE: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         redraw_end - startProcess).count(),
                                 currentFrame.load(std::memory_order_relaxed));
            }
        }

    }
}

void wrappers::WorkersQueue::run5()
{
    // OUTPUT WITH ARTIFACTS BUT FAST
    // RUN2 BUT PARALLEL CONVERT AND ROTATION
    AHardwareBuffer_Desc desc{1920, 1080, 1, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN};
    HardwareBuffer localBuffer{desc};

    int stabX = 0;
    int stabY = 0;

    bool frameset = false;

    while (!stop)
    {
        std::unique_lock<std::mutex> lockGuard(mQueueProtector);
        if (!mTasks.empty())
        {
            auto task = mTasks.front();
            mTasks.pop_front();
            lockGuard.unlock();

            auto startProcess = std::chrono::high_resolution_clock::now();

            // LOCK SOURCE IMAGE
            HardwareBuffer imageBuffer{};
            task.image.getHardwareBuffer(imageBuffer);
            imageBuffer.acquireAndLock();

            // GET YUV PLANES INFO
            uint8_t * y = nullptr;
            int32_t y_count = 0;
            uint8_t * u = nullptr;
            int32_t u_count = 0;
            uint8_t * v = nullptr;
            int32_t v_count = 0;

            task.image.getPlaneData(0, &y, &y_count);
            task.image.getPlaneData(2, &u, &u_count);
            task.image.getPlaneData(1, &v, &v_count);

            int32_t y_stride = 0;
            task.image.getPlaneRowStride(0, &y_stride);

            int32_t u_stride = 0;
            task.image.getPlaneRowStride(2, &u_stride);

            int32_t v_stride = 0;
            task.image.getPlaneRowStride(1, &v_stride);

            auto scale_start = std::chrono::high_resolution_clock::now();

            auto clampX = std::clamp(stabX, -80, 80) & (~0 ^ 1);
            auto clampY = std::clamp(stabY, -420, 420) & (~0 ^ 1);

            libyuv::ScalePlane(y + y_stride * (420 + clampY) + 80 + clampX, y_stride, 3840, 2160, y, 1920, 1920, 1080, libyuv::kFilterBox);
            libyuv::UVScale(u + y_stride * (420 + clampY) / 2 + 80 + clampX, y_stride, 3840 / 2, 2160 / 2, u, 1920, 1920 / 2, 1080 / 2, libyuv::kFilterBox);

            auto scale_end = std::chrono::high_resolution_clock::now();
            // LOCK TEMPORARY BUFFER
            uint8_t * bufferRaw = nullptr;
            ARect rect2{0, 0, 1920, 1080};

            AHardwareBuffer_lock(localBuffer.handle, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1,
                                 &rect2, (void**)&bufferRaw);

            AHardwareBuffer_Desc hwbdesc;
            AHardwareBuffer_describe(localBuffer.handle, &hwbdesc);

            auto rotate_start = std::chrono::high_resolution_clock::now();

            auto yout = y + 1920 * 1080 * 3;
            auto uout = y + 1920 * 1080 * 4;
            auto vout = y + 1920 * 1080 * 5;
            auto uvout = u;


            libyuv::RotatePlane90(y, 1920, yout, 1080, 1920, 1080);

            libyuv::RotateUV90(u,
                               1920,
                               uout,
                               1080 / 2,
                               vout,
                               1080 / 2,
                               1920 / 2,
                               1080 / 2);

            libyuv::MergeUVPlane(uout,
                                 1080 / 2,
                                 vout,
                                 1080 / 2,
                                 uvout,
                                 1080,
                                 1080 / 2,
                                 1920 / 2);
            auto rotate_end = std::chrono::high_resolution_clock::now();

            auto argb_start = std::chrono::high_resolution_clock::now();
            libyuv::NV12ToARGBMatrix(yout,
                                     1080,
                                     uvout,
                                     1080,
                                     bufferRaw,
                                     1080 * 4,
                                     &libyuv::kYuvV2020Constants,
                                     1080,
                                     1920);
            auto argb_end = std::chrono::high_resolution_clock::now();

            AHardwareBuffer_unlock(localBuffer.handle, nullptr);

            if (currentFrame > task.frameNumber)
            {
                Logger::logInfo(32, "TRYING TO DRAW OLDER FRAME");
                continue;
            }

            bool wannause = false;
            if (surfaceUsed.compare_exchange_strong(wannause, true)) {
                // LOCK PREVIEW SURFACE
                ANativeWindow_Buffer surfaceBuffer{};
                ARect rect{0, 0, 1080, 1920};
                auto isSurfaceLockFailed = ANativeWindow_lock(task.surface, &surfaceBuffer, &rect);

                auto redraw_start = std::chrono::high_resolution_clock::now();
                auto out = (uint8_t *) surfaceBuffer.bits;

                libyuv::ARGBCopy(bufferRaw, 1080 * 4, out, surfaceBuffer.stride * 4,
                                 surfaceBuffer.width, surfaceBuffer.height);

                auto redraw_end = std::chrono::high_resolution_clock::now();

                isSurfaceLockFailed = ANativeWindow_unlockAndPost(task.surface);
                currentFrame = task.frameNumber;
                surfaceUsed = false;
                Logger::logError(100, "TIME TO SCALE-DOWN: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         scale_end - scale_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "TIME TO MAKE ARGB: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         argb_end - argb_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "TIME TO ROTATE: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         rotate_end - rotate_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "TIME TO REDRAW: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         redraw_end - redraw_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "FULL PROCEDURE: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         redraw_end - startProcess).count(),
                                 currentFrame.load(std::memory_order_relaxed));
            }
        }

    }
}

void wrappers::WorkersQueue::run6()
{
    AHardwareBuffer_Desc scaleDownBufferYDesc{2000, 1500, 1, AHARDWAREBUFFER_FORMAT_S8_UINT, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN};
    HardwareBuffer scaleDownBufferY{scaleDownBufferYDesc};
    ARect r{0, 0, 2000, 1500};
    auto scaleDownYPtr = (uint8_t*) scaleDownBufferY.acquireAndLock(&r);
    auto scaleDownYDesc = scaleDownBufferY.describe();

    HardwareBuffer scaleDownBufferU{scaleDownBufferYDesc};
    auto scaleDownUPtr = (uint8_t*) scaleDownBufferU.acquireAndLock(&r);
    auto scaleDownUDesc = scaleDownBufferU.describe();

    HardwareBuffer scaleDownBufferV{scaleDownBufferYDesc};
    auto scaleDownVPtr = (uint8_t*) scaleDownBufferV.acquireAndLock(&r);
    auto scaleDownVDesc = scaleDownBufferV.describe();

    HardwareBuffer scaleDownBufferUV{scaleDownBufferYDesc};
    auto scaleDownUVPtr = (uint8_t*) scaleDownBufferUV.acquireAndLock(&r);
    auto scaleDownUVDesc = scaleDownBufferUV.describe();


    AHardwareBuffer_Desc displayBufferDesc{2000, 1500, 1, AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN};
    HardwareBuffer displayBuffer{displayBufferDesc};
    //ARect r0{0, 0, 1920, 1080};
    auto displayBufferPtr = (uint8_t*) displayBuffer.acquireAndLock(&r);
    auto displayBufferDescr = displayBuffer.describe();

    int stabX = 0;
    int stabY = 0;

    while (!stop)
    {
        std::unique_lock<std::mutex> lockGuard(mQueueProtector);
        if (!mTasks.empty())
        {
            auto task = mTasks.front();
            mTasks.pop_front();
            lockGuard.unlock();

            auto startProcess = std::chrono::high_resolution_clock::now();

            // LOCK SOURCE IMAGE
            HardwareBuffer imageBuffer{};
            task.image.getHardwareBuffer(imageBuffer);
            imageBuffer.acquireAndLock();

            // GET YUV PLANES INFO
            uint8_t * y = nullptr;
            int32_t y_count = 0;
            uint8_t * u = nullptr;
            int32_t u_count = 0;
            uint8_t * v = nullptr;
            int32_t v_count = 0;

            task.image.getPlaneData(0, &y, &y_count);
            task.image.getPlaneData(2, &u, &u_count);
            task.image.getPlaneData(1, &v, &v_count);

            int32_t y_stride = 0;
            task.image.getPlaneRowStride(0, &y_stride);

            int32_t u_stride = 0;
            task.image.getPlaneRowStride(2, &u_stride);

            int32_t v_stride = 0;
            task.image.getPlaneRowStride(1, &v_stride);


            int src_width = 4000;
            int src_height = 3000;
            int dst_width = src_width / 2;
            int dst_height = src_height / 2;


            auto scale_start = std::chrono::high_resolution_clock::now();

            libyuv::ScalePlane(y, y_stride, src_width, src_height, scaleDownYPtr, scaleDownYDesc.stride, dst_width, dst_height, libyuv::kFilterBox);
            libyuv::UVScale(u, y_stride, src_width / 2, src_height / 2, scaleDownUVPtr, scaleDownUVDesc.stride, dst_width / 2, dst_height / 2, libyuv::kFilterBox);

            auto scale_end = std::chrono::high_resolution_clock::now();

            auto stab_start = std::chrono::high_resolution_clock::now();
            auto stab = getStab(scaleDownYPtr, scaleDownYDesc.stride);
            auto stab_end = std::chrono::high_resolution_clock::now();

            auto rotate_start = std::chrono::high_resolution_clock::now();

            auto yout = y + dst_width * dst_height * 3;
            auto uout = y + dst_width * dst_height * 4;
            auto vout = y + dst_width * dst_height * 5;
            auto uvout = u;


            auto clampX = std::clamp(stab.first, -40, 40) & (~0 ^ 1);
            auto clampY = std::clamp(stab.second, -210, 210) & (~0 ^ 1);

            libyuv::RotatePlane90(scaleDownYPtr + scaleDownYDesc.stride * (210 + clampY) + 40 + clampX, scaleDownYDesc.stride, yout, 1080, 1920, 1080);

            libyuv::RotateUV90(scaleDownUVPtr + scaleDownYDesc.stride * (210 + clampY) / 2 + 40 + clampX,
                               scaleDownUVDesc.stride,
                               uout,
                               1080 / 2,
                               vout,
                               1080 / 2,
                               1920 / 2,
                               1080 / 2);

            libyuv::MergeUVPlane(uout,
                                 1080 / 2,
                                 vout,
                                 1080 / 2,
                                 scaleDownUVPtr,
                                 1080,
                                 1080 / 2,
                                 1920 / 2);
            auto rotate_end = std::chrono::high_resolution_clock::now();

            auto argb_start = std::chrono::high_resolution_clock::now();
            libyuv::NV12ToARGBMatrix(yout,
                                     1080,
                                     scaleDownUVPtr,
                                     1080,
                                     displayBufferPtr,
                                     1080 * 4,
                                     &libyuv::kYuvV2020Constants,
                                     1080,
                                     1920);
            auto argb_end = std::chrono::high_resolution_clock::now();
            if (currentFrame > task.frameNumber)
            {
                Logger::logInfo(32 + stab.second, "TRYING TO DRAW OLDER FRAME");
                continue;
            }

            bool wannause = false;
            if (surfaceUsed.compare_exchange_strong(wannause, true)) {
                // LOCK PREVIEW SURFACE
                ANativeWindow_Buffer surfaceBuffer{};
                ARect rect{0, 0, 1080, 1920};
                auto isSurfaceLockFailed = ANativeWindow_lock(task.surface, &surfaceBuffer, &rect);

                auto redraw_start = std::chrono::high_resolution_clock::now();
                auto out = (uint8_t *) surfaceBuffer.bits;

                libyuv::ARGBCopy(displayBufferPtr, 1080 * 4, out, surfaceBuffer.stride * 4,
                                 surfaceBuffer.width, surfaceBuffer.height);

                auto redraw_end = std::chrono::high_resolution_clock::now();

                isSurfaceLockFailed = ANativeWindow_unlockAndPost(task.surface);
                currentFrame = task.frameNumber;
                surfaceUsed = false;
                Logger::logError(100, "TIME TO SCALE-DOWN: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         scale_end - scale_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "TIME TO MAKE ARGB: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         argb_end - argb_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "TIME TO STAB: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         stab_end - stab_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "TIME TO ROTATE: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         rotate_end - rotate_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logError(100, "TIME TO REDRAW: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         redraw_end - redraw_start).count(),
                                 currentFrame.load(std::memory_order_relaxed));
                Logger::logInfo(100, "FULL PROCEDURE: %d, FRAME %d",
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                         redraw_end - startProcess).count(),
                                 currentFrame.load(std::memory_order_relaxed));
            }
        }

    }
}