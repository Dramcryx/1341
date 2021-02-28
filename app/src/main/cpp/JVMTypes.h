#ifndef INC_1341_JVMTYPES_H
#define INC_1341_JVMTYPES_H

// JVM
#include <jni.h>

#define _HIDDEN_ __attribute__((visibility("hidden")))
// - Note
//      Group of java native type variables
// - Reference
//      https://programming.guide/java/list-of-java-exceptions.html
struct _HIDDEN_ java_type_set_t final {
    jclass runtime_exception{};
    jclass illegal_argument_exception{};
    jclass illegal_state_exception{};
    jclass unsupported_operation_exception{};
    jclass index_out_of_bounds_exception{};

    jclass device_t{};
    jfieldID device_id_f{};
};

#endif //INC_1341_JVMTYPES_H
