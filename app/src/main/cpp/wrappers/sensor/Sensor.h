#ifndef INC_1341_SENSOR_H
#define INC_1341_SENSOR_H

#include <android/sensor.h>

namespace wrappers {
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

        int getMinDelay()
        {
            return ASensor_getMinDelay(handle);
        }
    };
}

#endif //INC_1341_SENSOR_H
