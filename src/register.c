#include "../include/interpreter.h"

void register_libs(VM *vm){
        // Register all library modules
    register_package_lib(vm);
    register_coroutine_lib(vm);
    register_math_lib(vm);
    register_string_lib(vm);
    register_table_lib(vm);
    register_utf8_lib(vm);
    register_os_lib(vm);
    register_io_lib(vm);
    register_debug_lib(vm);
    register_random_lib(vm);
    register_date_lib(vm);
    register_exception_lib(vm);
    register_async_lib(vm);
    register_class_lib(vm);

}
