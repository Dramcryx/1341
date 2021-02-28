#ifndef INC_1341_CAMERAMODEL_H
#define INC_1341_CAMERAMODEL_H

#define _INTERFACE_ __attribute__((visibility("default")))
#define _C_INTERFACE_ extern "C" __attribute__((visibility("default")))
#define _HIDDEN_ __attribute__((visibility("hidden")))

// JVM
#include <jni.h>

_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_CameraModel_Init(JNIEnv* env, jclass type) noexcept;

_C_INTERFACE_ jint JNICALL //
Java_com_dramcryx_cam1341_CameraModel_GetDeviceCount(JNIEnv* env, jclass type) noexcept;

_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_CameraModel_SetDeviceData(JNIEnv* env, jclass type,
        jobjectArray devices) noexcept;

#endif //INC_1341_CAMERAMODEL_H
