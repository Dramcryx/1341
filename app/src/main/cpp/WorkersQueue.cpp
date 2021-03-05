#include "AndroidWrappers.h"

#include <array>

#include "CL/opencl.h"
#include "CL/cl.hpp"

const char * kTestKernel =
        "__kernel void test(__global int* message)"
        "{"
        " int gid = get_global_id(0);"
        " message[gid] += gid;"
        "}";

wrappers::WorkersQueue::WorkersQueue(std::size_t workers)
{
    mWorkers.reserve(workers);
    for (int i = 0; i < mWorkers.capacity(); ++i) {
        mWorkers.emplace_back(&WorkersQueue::run5, this);
    }


    /* получить доступные платформы */
    ret = clGetPlatformIDs(1, &platform_id, &ret_num_platforms);



    /* получить доступные устройства */
    ret = clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_GPU, 1, &device_id, &ret_num_devices);



    /* создать контекст */
    context = clCreateContext(nullptr, 1, &device_id, nullptr, nullptr, &ret);


    /* создаем команду */
    command_queue = clCreateCommandQueue(context, device_id, 0, &ret);




    const size_t plength = strlen(kTestKernel);
    /* создать бинарник из кода программы */
    program = clCreateProgramWithSource(context, 1, (const char **)&kTestKernel, &plength, &ret);

    /* скомпилировать программу */
    ret = clBuildProgram(program, 1, &device_id, nullptr, nullptr, nullptr);

    /* создать кернел */
    kernel = clCreateKernel(program, "test", &ret);

    cl_mem memobj = NULL;
    int memLenth = 10;

    std::unique_ptr<cl_int[]> mem = std::make_unique<cl_int[]>(memLenth);


    /* создать буфер */
    memobj = clCreateBuffer(context, CL_MEM_READ_WRITE, memLenth * sizeof(cl_int), NULL, &ret);

    /* записать данные в буфер */
    ret = clEnqueueWriteBuffer(command_queue, memobj, CL_TRUE, 0, memLenth * sizeof(cl_int), mem.get(), 0, NULL, NULL);

    /* устанавливаем параметр */
    ret = clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *)&memobj);

    size_t global_work_size[1] = { 10 };

    /* выполнить кернел */
    ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, global_work_size, NULL, 0, NULL, NULL);

    /* считать данные из буфера */
    ret = clEnqueueReadBuffer(command_queue, memobj, CL_TRUE, 0, memLenth * sizeof(float), mem.get(), 0, NULL, NULL);

    auto x = 0;
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

            cl_image_format clImageFormat{
                .image_channel_order = CL_RGBA,
                .image_channel_data_type =  CL_UNORM_INT8
            };

            cl_image_desc clImageDesc {
                .image_type = CL_MEM_OBJECT_IMAGE2D,
                .image_width = 1080,
                .image_height = 1920,
                .image_row_pitch = 1080 * 4
            };

//            cl_int ret = 0;
//            auto imageMem = clCreateImage2D(context, CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR , &clImageFormat, 1920, 1080, 1920 * 4, y, &ret);

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
    AHardwareBuffer_Desc desc{1920, 1080, 1, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM};
    HardwareBuffer localBuffer{desc};

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
            task.image.getPlaneRowStride(0, &y_stride);

            int32_t u_stride = 0;
            task.image.getPlaneRowStride(2, &u_stride);

            int32_t v_stride = 0;
            task.image.getPlaneRowStride(1, &v_stride);

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
