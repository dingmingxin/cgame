#ifndef SYSTEM_H
#define SYSTEM_H

#include "saveload.h"
#include "script_export.h"

SCRIPT(system,

       EXPORT void system_load_all(Store *f);
       EXPORT void system_save_all(Store *f);

    )

void system_init();
void system_deinit();
void system_update_all();
void system_draw_all();

#endif

