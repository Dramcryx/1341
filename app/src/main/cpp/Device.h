#ifndef INC_1341_DEVICE_H
#define INC_1341_DEVICE_H

#define _INTERFACE_ __attribute__((visibility("default")))
#define _C_INTERFACE_ extern "C" __attribute__((visibility("default")))
#define _HIDDEN_ __attribute__((visibility("hidden")))

// JVM
#include <jni.h>

_C_INTERFACE_ jbyte JNICALL //
Java_com_dramcryx_cam1341_Device_facing(JNIEnv* env, jobject instance) noexcept;

_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_Device_open(JNIEnv* env, jobject instance) noexcept;

_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_Device_close(JNIEnv* env, jobject instance) noexcept;

_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_Device_startRepeat(JNIEnv* env, jobject instance,
        jobject surface) noexcept;
_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_Device_stopRepeat(JNIEnv* env, jobject instance) noexcept;

_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_Device_startCapture(JNIEnv* env, jobject instance,
        jobject surface) noexcept;
_C_INTERFACE_ void JNICALL //
Java_com_dramcryx_cam1341_Device_stopCapture(JNIEnv* env, jobject instance) noexcept;

#endif //INC_1341_DEVICE_H
