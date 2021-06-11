#ifndef INC_1341_SENSORMANAGER_H
#define INC_1341_SENSORMANAGER_H

#include "wrappers/Looper.h"
#include "wrappers/sensor/Sensor.h"

namespace wrappers {
    struct SensorManager {

        // SUBCLASS DECLARATIONS
        struct SensorList {
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

            int setEventRate(ASensor const *sensor, int32_t usec) {
                return ASensorEventQueue_setEventRate(handle, sensor, usec);
            }
        };
        // SUBCLASS DECLARATIONS END

        ASensorManager *handle = nullptr;

        static SensorManager getInstanceForPackage() {
            return {ASensorManager_getInstanceForPackage("com.dramcryx.cam1341")};
        }

        SensorList getSensorList() {
            SensorList ret{};
            ret.count = ASensorManager_getSensorList(handle, &ret.list);
            return ret;
        }

        SensorEventQueue
        createEventQueue(Looper &looper, int looperId, ALooper_callbackFunc callback, void *data) {
            return {ASensorManager_createEventQueue(handle, looper.handle, looperId, callback,
                                                    data)};
        }

        Sensor getDefaultSensor(int type) {
            return {ASensorManager_getDefaultSensor(handle, type)};
        }

        int destroyEventQueue(SensorEventQueue &queue) {
            return ASensorManager_destroyEventQueue(handle, std::exchange(queue.handle, nullptr));
        }
    };
}

#endif //INC_1341_SENSORMANAGER_H
