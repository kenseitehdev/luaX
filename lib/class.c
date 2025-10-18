// lib/class.c - Object-Oriented Programming class system
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../include/interpreter.h"

/* ---- Helpers ---- */

/* Helper to create empty table */
static Value new_table(void) {
    return V_table();
}

/* Helper to set table field */
static void set_field(Value table, const char *key, Value val) {
    if (table.tag != VAL_TABLE) return;
    tbl_set_public(table.as.t, V_str_from_c(key), val);
}

/* Helper to get table field */
static int get_field(Value table, const char *key, Value *out) {
    if (table.tag != VAL_TABLE) return 0;
    return tbl_get_public(table.as.t, V_str_from_c(key), out);
}

/* Helper to set numeric index */
static void set_index(Value table, int idx, Value val) {
    if (table.tag != VAL_TABLE) return;
    tbl_set_public(table.as.t, V_int(idx), val);
}

/* Helper to get numeric index */
static int get_index(Value table, int idx, Value *out) {
    if (table.tag != VAL_TABLE) return 0;
    return tbl_get_public(table.as.t, V_int(idx), out);
}
static int lookup_method(Value class_table, const char *method_name, Value *out) {
    Value current = class_table;
    
    for (int depth = 0; depth < 100; depth++) {  /* Prevent infinite loops */
        if (current.tag != VAL_TABLE) break;
        
        /* Try to find method in current class */
        if (get_field(current, method_name, out)) {
            if (is_callable(*out)) {
                return 1;
            }
        }
        
        /* Look in parent class */
        Value parent;
        if (!get_field(current, "__parent", &parent) || parent.tag != VAL_TABLE) {
            break;
        }
        
        current = parent;
    }
    
    return 0;
}

/* getmethod(class, method_name) - helper to get method from class hierarchy */
static Value class_getmethod(struct VM *vm, int argc, Value *argv) {
    (void)vm;
    
    if (argc < 2) {
        fprintf(stderr, "getmethod: expected (class, method_name)\n");
        return V_nil();
    }
    
    Value class_table = argv[0];
    Value method_name = argv[1];
    
    if (class_table.tag != VAL_TABLE || method_name.tag != VAL_STR) {
        return V_nil();
    }
    
    char name_str[256];
    int len = method_name.as.s->len < 255 ? method_name.as.s->len : 255;
    memcpy(name_str, method_name.as.s->data, len);
    name_str[len] = '\0';
    
    Value method;
    if (lookup_method(class_table, name_str, &method)) {
        return method;
    }
    
    return V_nil();
}

/* ---- Metatable Setup ---- */

/* Set up metatable for class definition */
static void setup_class_metatable(struct VM *vm, Value class_table) {
    Value mt = new_table();
    
    /* Store reference to the class itself in metatable */
    set_field(mt, "__class", class_table);
    
    /* __call metamethod for instantiation: Class(...) */
    /* This will be handled by interpreter's __call support */
    /* For now, we'll provide a .new() method */
    
    /* Set metatable (if your VM supports setting metatables) */
    /* For now, we'll store it as a field */
    set_field(class_table, "__metatable", mt);
}

/* Set up metatable for instance */
static void setup_instance_metatable(struct VM *vm, Value instance, Value class_table) {
    Value mt = new_table();
    
    /* __index metamethod - look up methods in class */
    set_field(mt, "__index", class_table);
    
    /* Store class reference */
    set_field(instance, "__class", class_table);
    
    /* Set metatable */
    set_field(instance, "__metatable", mt);
    
    (void)vm;
}

/* ---- Class Creation ---- */

/* class(definition) - creates a new class */
static Value class_create(struct VM *vm, int argc, Value *argv) {
    if (argc < 1 || argv[0].tag != VAL_TABLE) {
        fprintf(stderr, "class: expected table definition\n");
        return V_nil();
    }
    
    Value def = argv[0];
    Value class_table = new_table();
    
    /* Get special fields */
    Value name, init, extends;
    int has_name = get_field(def, "name", &name);
    int has_init = get_field(def, "init", &init);
    int has_extends = get_field(def, "extends", &extends);
    
    /* Store class name */
    if (has_name) {
        set_field(class_table, "__name", name);
    } else {
        set_field(class_table, "__name", V_str_from_c("Class"));
    }
    
    /* Store parent class for inheritance */
    if (has_extends && extends.tag == VAL_TABLE) {
        set_field(class_table, "__parent", extends);
        
        /* Set up __index to look up methods in parent if not found */
        Value mt = new_table();
        set_field(mt, "__index", extends);
        set_field(class_table, "__metatable", mt);
    }
    
    /* Copy ALL fields from definition to class */
    const char *common_names[] = {
        "init", "new", "greet", "tostring", "get", "set",
        "work", "update", "render", "destroy", "clone",
        "draw", "move", "stop", "start", "reset", "clear",
        "add", "remove", "find", "search", "filter", "map",
        "reduce", "foreach", "each", "toString", "valueOf",
        "call", "apply", "bind", "push", "pop", "shift",
        "unshift", "slice", "splice", "concat", "join",
        "reverse", "sort", "indexOf", "lastIndexOf", "includes",
        "build", "create", "make", "construct", "initialize",
        "setup", "configure", "load", "save", "delete",
        "insert", "update", "select", "query", "execute",
        "run", "process", "handle", "compute", "calculate",
        "validate", "check", "verify", "test", "assert",
        "print", "log", "debug", "warn", "error",
        "open", "close", "read", "write", "flush",
        "connect", "disconnect", "send", "receive", "listen",
        "parse", "format", "encode", "decode", "serialize",
        "deserialize", "transform", "convert", "cast",
        "name", "extends", "parent", "super", "base",
        NULL
    };
    
    for (int i = 0; common_names[i]; i++) {
        Value val;
        if (get_field(def, common_names[i], &val)) {
            set_field(class_table, common_names[i], val);
        }
    }
    
    /* Try numeric indices (in case methods are in array) */
    for (int i = 1; i <= 100; i++) {
        Value val;
        if (get_index(def, i, &val)) {
            set_index(class_table, i, val);
        }
    }
    
    /* Set up class metatable */
    setup_class_metatable(vm, class_table);
    
    /* Mark this as a class */
    set_field(class_table, "__is_class", V_bool(1));
    
    return class_table;
}

/* Create an instance of a class */
static Value create_instance(struct VM *vm, Value class_table, int argc, Value *argv) {
    if (class_table.tag != VAL_TABLE) {
        fprintf(stderr, "new: not a valid class\n");
        return V_nil();
    }
    
    /* Create new instance table */
    Value instance = new_table();
    
    /* Set up instance metatable for method lookup */
    setup_instance_metatable(vm, instance, class_table);
    
    /* Call init() constructor if it exists */
    Value init;
    if (get_field(class_table, "init", &init) && is_callable(init)) {
        /* Prepend instance as first argument (self) */
        Value *new_argv = (Value*)malloc(sizeof(Value) * (argc + 1));
        if (new_argv) {
            new_argv[0] = instance;
            for (int i = 0; i < argc; i++) {
                new_argv[i + 1] = argv[i];
            }
            call_any_public(vm, init, argc + 1, new_argv);
            free(new_argv);
        }
    }
    
    return instance;
}

/* class.new() method - creates an instance */
static Value class_new_method(struct VM *vm, int argc, Value *argv) {
    if (argc < 1 || argv[0].tag != VAL_TABLE) {
        fprintf(stderr, "Class.new: expected class as first argument\n");
        return V_nil();
    }
    
    Value class_table = argv[0];
    
    /* Create instance with remaining arguments */
    return create_instance(vm, class_table, argc - 1, argv + 1);
}

/* instanceof(obj, class) - checks if object is instance of class */
static Value class_instanceof(struct VM *vm, int argc, Value *argv) {
    (void)vm;
    
    if (argc < 2) {
        fprintf(stderr, "instanceof: expected (object, class)\n");
        return V_bool(0);
    }
    
    Value obj = argv[0];
    Value target_class = argv[1];
    
    if (obj.tag != VAL_TABLE || target_class.tag != VAL_TABLE) {
        return V_bool(0);
    }
    
    /* Get object's class */
    Value obj_class;
    if (!get_field(obj, "__class", &obj_class) || obj_class.tag != VAL_TABLE) {
        return V_bool(0);
    }
    
    /* Check direct match */
    if (obj_class.tag == target_class.tag && 
        obj_class.as.t == target_class.as.t) {
        return V_bool(1);
    }
    
    /* Check inheritance chain */
    Value current = obj_class;
    for (int depth = 0; depth < 100; depth++) {  /* Prevent infinite loops */
        Value parent;
        if (!get_field(current, "__parent", &parent) || parent.tag != VAL_TABLE) {
            break;
        }
        
        if (parent.as.t == target_class.as.t) {
            return V_bool(1);
        }
        
        current = parent;
    }
    
    return V_bool(0);
}

/* super(obj, method_name) - calls parent class method */
static Value class_super(struct VM *vm, int argc, Value *argv) {
    if (argc < 2) {
        fprintf(stderr, "super: expected (object, method_name, ...)\n");
        return V_nil();
    }
    
    Value obj = argv[0];
    Value method_name = argv[1];
    
    if (obj.tag != VAL_TABLE) {
        fprintf(stderr, "super: first argument must be an object\n");
        return V_nil();
    }
    
    /* Get object's class */
    Value obj_class;
    if (!get_field(obj, "__class", &obj_class) || obj_class.tag != VAL_TABLE) {
        fprintf(stderr, "super: object has no class\n");
        return V_nil();
    }
    
    /* Get parent class */
    Value parent_class;
    if (!get_field(obj_class, "__parent", &parent_class) || parent_class.tag != VAL_TABLE) {
        fprintf(stderr, "super: class has no parent\n");
        return V_nil();
    }
    
    /* Look up method in parent class */
    Value method;
    if (method_name.tag == VAL_STR) {
        char method_str[256];
        int len = method_name.as.s->len < 255 ? method_name.as.s->len : 255;
        memcpy(method_str, method_name.as.s->data, len);
        method_str[len] = '\0';
        
        if (!get_field(parent_class, method_str, &method) || !is_callable(method)) {
            fprintf(stderr, "super: method '%s' not found in parent\n", method_str);
            return V_nil();
        }
    } else {
        fprintf(stderr, "super: method name must be a string\n");
        return V_nil();
    }
    
    /* Call parent method with object as first argument */
    Value *new_argv = (Value*)malloc(sizeof(Value) * argc);
    if (!new_argv) return V_nil();
    
    new_argv[0] = obj;
    for (int i = 2; i < argc; i++) {
        new_argv[i - 1] = argv[i];
    }
    
    Value result = call_any_public(vm, method, argc - 1, new_argv);
    free(new_argv);
    
    return result;
}

/* getclass(obj) - returns the class of an object */
static Value class_getclass(struct VM *vm, int argc, Value *argv) {
    (void)vm;
    
    if (argc < 1) {
        fprintf(stderr, "getclass: expected object\n");
        return V_nil();
    }
    
    Value obj = argv[0];
    if (obj.tag != VAL_TABLE) {
        return V_nil();
    }
    
    Value class_ref;
    if (get_field(obj, "__class", &class_ref)) {
        return class_ref;
    }
    
    return V_nil();
}

/* classname(obj) - returns the name of an object's class */
static Value class_classname(struct VM *vm, int argc, Value *argv) {
    (void)vm;
    
    if (argc < 1) {
        return V_str_from_c("nil");
    }
    
    Value obj = argv[0];
    if (obj.tag != VAL_TABLE) {
        return V_str_from_c("not an object");
    }
    
    Value class_ref;
    if (!get_field(obj, "__class", &class_ref) || class_ref.tag != VAL_TABLE) {
        return V_str_from_c("table");
    }
    
    Value name;
    if (get_field(class_ref, "__name", &name) && name.tag == VAL_STR) {
        return name;
    }
    
    return V_str_from_c("Class");
}

/* ---- Helper for method binding ---- */

/* This would be used if you want to support obj.method() syntax automatically */
/* For now, users will need to use obj:method() or call methods explicitly */

/* ---- Registration ---- */
void register_class_lib(struct VM *vm) {
    /* Register global 'class' function */
    env_add_public(vm->env, "class", (Value){.tag=VAL_CFUNC, .as.cfunc=class_create}, false);
    
    /* Register utility functions */
    env_add_public(vm->env, "instanceof", (Value){.tag=VAL_CFUNC, .as.cfunc=class_instanceof}, false);
    env_add_public(vm->env, "super", (Value){.tag=VAL_CFUNC, .as.cfunc=class_super}, false);
    env_add_public(vm->env, "getclass", (Value){.tag=VAL_CFUNC, .as.cfunc=class_getclass}, false);
    env_add_public(vm->env, "classname", (Value){.tag=VAL_CFUNC, .as.cfunc=class_classname}, false);
    env_add_public(vm->env, "getmethod", (Value){.tag=VAL_CFUNC, .as.cfunc=class_getmethod}, false);
    
    /* Register Class helper table for additional utilities */
    Value Class = new_table();
    set_field(Class, "new", (Value){.tag=VAL_CFUNC, .as.cfunc=class_new_method});
    env_add_public(vm->env, "Class", Class, false);
}
