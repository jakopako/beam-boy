/********************************************************************
** Beam Boy — Berry module/class registration table.
**
** Vendored from upstream default/be_modtab.c unchanged apart from this
** header. This is the table the interpreter core links against to know
** which built-in modules exist (be_module.c) and which native classes are
** registered up front (be_var.c) -- every ESP32/Tasmota-style Berry build
** needs its own copy of this file, since it is where user-defined native
** classes would be registered (Beam Boy has none yet; the Beam API is bound
** as functions on a module, not a class -- see vm_bench_scene.cpp).
********************************************************************/
#include "berry.h"

/* this file contains the declaration of the module table. */

/* default modules declare */
be_extern_native_module(string);
be_extern_native_module(math);
be_extern_native_module(global);
be_extern_native_module(gc);
be_extern_native_module(strict);
be_extern_native_module(undefined);

/* user-defined modules declare start */

/* user-defined modules declare end */

/* module list declaration */
BERRY_LOCAL const bntvmodule_t* const be_module_table[] = {
/* default modules register */
#if BE_USE_STRING_MODULE
    &be_native_module(string),
#endif
#if BE_USE_MATH_MODULE
    &be_native_module(math),
#endif
#if BE_USE_GLOBAL_MODULE
    &be_native_module(global),
#endif
#if BE_USE_GC_MODULE
    &be_native_module(gc),
#endif
#if BE_USE_STRICT_MODULE
    &be_native_module(strict),
#endif
    &be_native_module(undefined),
    /* user-defined modules register start */

    /* user-defined modules register end */
    NULL /* do not remove */
};

/* user-defined classes declare start */
/* be_extern_native_class(my_class); */
/* user-defined classes declare end */

BERRY_LOCAL bclass_array be_class_table = {
    /* first list are direct classes */
    /* &be_native_class(my_class), */
    NULL, /* do not remove */
};
