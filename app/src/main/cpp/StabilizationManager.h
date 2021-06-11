#ifndef INC_1341_STABILIZATIONMANAGER_H
#define INC_1341_STABILIZATIONMANAGER_H

#include <memory>
#include <utility>
#include <atomic>
#include <thread>
#include <chrono>
#include <array>

#include "wrappers/sensor/SensorManager.h"

#include "fastcv.h"

class LowPassFilter
{
public:
    LowPassFilter() = default;

    std::array<float, 3> filter(float * vals)
    {
        std::array<float, 3> retval = {alpha * oldvals[0] + (1.0f - alpha) * vals[0],
                                       alpha * oldvals[1] + (1.0f - alpha) * vals[1],
                                       alpha * oldvals[2] + (1.0f - alpha) * vals[2]};
        oldvals = retval;
        return retval;
    }

private:
    static constexpr float alpha = 0.5f;
    std::array<float, 3> oldvals = {0.0f};
};

class StabilizationManager {
public:
    StabilizationManager() : stop(false) {
        prevPyr = prev.data();
        nextPyr = next.data();

        assert(fcvPyramidAllocate_v3 (prevPyr, 2000, 1500, 2000, 1, 128, 3, fcvPyramidScale::FASTCV_PYRAMID_SCALE_HALF, 1) == fcvStatus::FASTCV_SUCCESS);
        assert(fcvPyramidAllocate_v3 (nextPyr, 2000, 1500, 2000, 1, 128, 3, fcvPyramidScale::FASTCV_PYRAMID_SCALE_HALF, 1) == fcvStatus::FASTCV_SUCCESS);

        backgroundSensorScanner = std::thread([this]() {
            timer = std::chrono::high_resolution_clock::now();
            sensorManager = wrappers::SensorManager::getInstanceForPackage();
            sensorEventQueue = sensorManager.createEventQueue(
                    *(looper = std::make_unique<wrappers::Looper>()), 1, nullptr, nullptr);
            auto gyroscope = sensorManager.getDefaultSensor(ASENSOR_TYPE_GYROSCOPE);
            sensorEventQueue.enableSensor(gyroscope);

            sensorEventQueue.setEventRate(gyroscope, 15000);
            LowPassFilter lpfGyro;

            static float NS2S = 1.0f / 1000000000.0f;
            float deltaRotationVector[4] = {0.f};
            float timestamp = 0;
            while (!stop) {
                int ident = wrappers::Looper::pollAll(16, nullptr, nullptr, nullptr);
                if (ident == ALOOPER_POLL_TIMEOUT) {
                    Logger::logError("NO EVENTS");
                }

                ASensorEvent sensorEvent[1];
                ASensorEventQueue_getEvents(sensorEventQueue.handle, sensorEvent, 3);
                for (auto &i: sensorEvent) {
                    if (i.type == ASENSOR_TYPE_GYROSCOPE) {
                        Logger::logInfo(128, "Gyroscope data: x %f, y %f, z %f",
                                        i.data[0],
                                        i.data[1], i.data[2]);
                        if (timestamp != 0) {
                            float dT = (i.timestamp - timestamp) * NS2S;
                            // Axis of the rotation sample, not normalized yet.
                            float axisX = i.data[0];
                            float axisY = i.data[1];
                            float axisZ = i.data[2];

                            // Calculate the angular speed of the sample
                            float omegaMagnitude = sqrt(axisX*axisX + axisY*axisY + axisZ*axisZ);

                            // Normalize the rotation vector if it's big enough to get the axis
                            // (that is, EPSILON should represent your maximum allowable margin of error)
                            if (omegaMagnitude > 1.f) {
                                axisX /= omegaMagnitude;
                                axisY /= omegaMagnitude;
                                axisZ /= omegaMagnitude;
                            }

                            // Integrate around this axis with the angular speed by the timestep
                            // in order to get a delta rotation from this sample over the timestep
                            // We will convert this axis-angle representation of the delta rotation
                            // into a quaternion before turning it into the rotation matrix.
                            float thetaOverTwo = omegaMagnitude * dT / 2.0f;
                            float sinThetaOverTwo = sin(thetaOverTwo);
                            float cosThetaOverTwo = cos(thetaOverTwo);
                            gyroX = sinThetaOverTwo * axisX;
                            gyroY = sinThetaOverTwo * axisY;
                            deltaRotationVector[2] = sinThetaOverTwo * axisZ;
                            deltaRotationVector[3] = cosThetaOverTwo;
                        }
                        timestamp = i.timestamp;
                        auto filtered = lpfGyro.filter(i.data);
                        //gyroX = filtered[0] - prevX;
                        //gyroY = filtered[1] - prevY;
                        prevX = filtered[0];
                        prevY = filtered[1];
                    }
                }
            }
        });
    }

    ~StabilizationManager() {
        stop = true;
        if (backgroundSensorScanner.joinable()) {
            backgroundSensorScanner.join();
        }
        sensorManager.destroyEventQueue(sensorEventQueue);
        fcvPyramidDelete_v2(prevPyr, 3, 0);
        fcvPyramidDelete_v2(nextPyr, 3, 0);
    }

    std::pair<double, double> getCollector() {
        static auto time = std::chrono::high_resolution_clock::now();
        auto newtime = std::chrono::high_resolution_clock::now();

        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(newtime - time).count();
        Logger::logInfo(32, "DeltaT: %d", dt);
        time = newtime;

        auto count = accelCount.exchange(0);
        double retX = accelX.exchange(0.0f) / count * dt / 1e3 / 1.6e-6;
        double retY = accelY.exchange(0.0f) / count * dt / 1e3 / 1.6e-6;
        Logger::logInfo(32, "AC: x %f, y %f", retX, retY);
        return {retX, retY};
    }

    static const char * statusToString(int status)
    {
        switch (status)
        {
            case  1: return "TRACKED";
            case -1: return "NOT_FOUND";
            case -2: return "SMALL_DET";
            case -3: return "MAX_ITERATIONS";
            case -4: return "OUT_OF_BOUNDS";
            case -5: return "LARGE_RESIDUE";
            case -6: return "SMALL_EIGVAL";
            case -99: return "INVALID";
            default: return "UNKNOWN";
        }
    }

    static const char * fcvStatusToString(int status)
    {
        switch (status)
        {
            case FASTCV_SUCCESS: return "FASTCV_SUCCESS";
            case FASTCV_EFAIL: return "FASTCV_EFAIL";
            case FASTCV_EUNALIGNPARAM: return "FASTCV_EUNALIGNPARAM";
            case FASTCV_EBADPARAM: return "FASTCV_EBADPARAM";
            case FASTCV_EINVALSTATE: return "FASTCV_EINVALSTATE";
            case FASTCV_ENORES: return "FASTCV_ENORES";
            case FASTCV_EUNSUPPORTED: return "FASTCV_EUNSUPPORTED";
            case FASTCV_EHWQDSP: return "FASTCV_EHWQDSP";
            case FASTCV_EHWGPU: return "FASTCV_EHWGPU";
            default: return "UNKNOWN";
        }
    }

    std::pair<int, int> trackFeatures(uint8_t * frame, uint32_t stride)
    {
#define REFRESH 15
        std::lock_guard<std::mutex> lk(synclock);
        auto GX = -gyroX.load() * 3.2e-3 / 1.6e-6;
        auto GY = -gyroY.load() * 2.4e-3 / 1.6e-6;


        Logger::logInfo(64, "GYRO RATES %f, %f", GX, GY);
        if (std::abs(GX) > 100.f || std::abs(GY) > 100.f)
        {
            Logger::logFatal(64, "GYRO TOO MUCH");
            counter = -8;
            return {stabX, stabY};
        }

        if (counter++ == 0 || actual < 12)
        {
            Logger::logFatal(64, "REEVAL");
            stabX = std::clamp(stabX, -40, 40);
            stabY = std::clamp(stabY, -210, 210);
            uint32_t features[32] = {0};
            Logger::logInfo(32, fcvStatusToString(fcvGoodFeatureToTracku8(frame, 2000, 1500, stride, 100.f, 0, 15, features, 16,
                                    &actual)));
            for (int i = 0; i < 2 * actual; ++i) {
                featuresFloat[i] = features[i];
            }
            fcvPyramidCreateu8_v3(frame, 2000, 1500, stride, 3,
                                  fcvPyramidScale::FASTCV_PYRAMID_SCALE_HALF, prevPyr);
            //++counter;
        }
        else if (counter > 1) {
            float32_t newFeaturesFloat[128] = {0.f};
            std::memcpy(newFeaturesFloat, featuresFloat, sizeof(float32_t) * 128);

            int32_t   statuses[128]         = {0};
            fcvPyramidCreateu8_v3(frame, 2000, 1500, stride, 3, fcvPyramidScale::FASTCV_PYRAMID_SCALE_HALF, nextPyr);

            fcvTrackLKOpticalFlowu8_v2((uint8_t*) prevPyr[0].ptr,
                                       frame,
                                       2000,
                                       1500,
                                       stride,
                                       prevPyr,
                                       nextPyr,
                                       featuresFloat,
                                       newFeaturesFloat,
                                       statuses,
                                       actual,
                                       21,
                                       21,
                                       5,
                                       3);

            float dx = 0;
            float dy = 0;
            int count = 0;

            for (int i = 0; i < actual; ++i)
            {
                if (statuses[i] == 1)
                {
                    //Logger::logInfo(128, "FEATURE TRACKED: %f, %f", newFeaturesFloat[2 * i], newFeaturesFloat[2 * i + 1]);
                    ++count;
                    dx += (newFeaturesFloat[2 * i] - featuresFloat[2 * i]);
                    dy += (newFeaturesFloat[2 * i + 1] - featuresFloat[2 * i + 1]);
                }
//                else
//                {
//                    Logger::logInfo(128, "FEATURE LOST (%s): %f, %f", statusToString(statuses[i]), newFeaturesFloat[2 * i], newFeaturesFloat[2 * i + 1]);
//                }
            }
            dx /= count;
            dy /= count;

            stabX += + dx;
            stabY += + dy;

            count = 0;
            for (int i = 0; i < actual; ++i) {
                if (statuses[i] == 1) {
                    featuresFloat[2 * (i - count)] = newFeaturesFloat[2 * i];
                    featuresFloat[2 * (i - count) + 1] = newFeaturesFloat[2 * i + 1];

                } else {
                    ++count;
                }
            }

            actual -= count;
//            Logger::logInfo(32, "LEFT %d", actual);
//            Logger::logInfo(64, "STAB FOR? %f %f; %d %d", dx, dy, stabX, stabY);

            std::swap(nextPyr, prevPyr);
//            Logger::logInfo(64, "FS %d, %d", stabX, stabY);
        }
        else {
            stabX *= 0.9;
            stabY *= 0.9;
        }
        return {stabX, stabY};
    }


private:
    std::unique_ptr<wrappers::Looper> looper;
    wrappers::SensorManager sensorManager;
    wrappers::SensorManager::SensorEventQueue sensorEventQueue;

    std::thread backgroundSensorScanner;

    std::atomic_bool stop;

    std::atomic_int32_t accelCount = 0;
    std::atomic<float>  accelX     = 0;
    std::atomic<float>  accelY     = 0;
    std::atomic<float>  prevX      = 0;
    std::atomic<float>  prevY      = 0;

    std::atomic_int32_t gyroCount = 0;
    std::atomic<float>  gyroX     = 0;
    std::atomic<float>  gyroY     = 0;

    std::atomic_int32_t rotCount = 0;
    std::atomic<float>  rotX     = 0;
    std::atomic<float>  rotY     = 0;

    std::chrono::time_point<std::chrono::high_resolution_clock> timer;

    std::array<fcvPyramidLevel_v2, 3> prev = {};
    std::array<fcvPyramidLevel_v2, 3> next = {};

    fcvPyramidLevel_v2 * prevPyr;
    fcvPyramidLevel_v2 * nextPyr;


    float featuresFloat[32] = {0.0f};
    uint32_t actual = 0;

    std::mutex synclock;

    int stabX = 0;
    int stabY = 0;

    std::atomic_uint_fast64_t counter = 0;

    LowPassFilter lpfLK;
};


#endif //INC_1341_STABILIZATIONMANAGER_H
