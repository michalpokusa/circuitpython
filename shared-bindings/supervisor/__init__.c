/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016-2017 Scott Shawcroft for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"
#include "py/reload.h"
#include "py/objstr.h"

#include "lib/utils/interrupt_char.h"
#include "supervisor/shared/autoreload.h"
#include "supervisor/shared/status_leds.h"
#include "supervisor/shared/stack.h"
#include "supervisor/shared/translate.h"
#include "supervisor/shared/workflow.h"

#include "shared-bindings/microcontroller/__init__.h"
#include "shared-bindings/supervisor/__init__.h"
#include "shared-bindings/supervisor/Runtime.h"
#include "shared-bindings/time/__init__.h"

//| """Supervisor settings"""
//|

//| runtime: Runtime
//| """Runtime information, such as ``runtime.serial_connected``
//| (USB serial connection status).
//| This object is the sole instance of `supervisor.Runtime`."""
//|

//| def enable_autoreload() -> None:
//|     """Enable autoreload based on USB file write activity."""
//|     ...
//|
STATIC mp_obj_t supervisor_enable_autoreload(void) {
    autoreload_enable();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(supervisor_enable_autoreload_obj, supervisor_enable_autoreload);

//| def disable_autoreload() -> None:
//|     """Disable autoreload based on USB file write activity until
//|     `enable_autoreload` is called."""
//|     ...
//|
STATIC mp_obj_t supervisor_disable_autoreload(void) {
    autoreload_disable();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(supervisor_disable_autoreload_obj, supervisor_disable_autoreload);

//| def set_rgb_status_brightness(brightness: int) -> None:
//|     """Set brightness of status neopixel from 0-255
//|     `set_rgb_status_brightness` is called."""
//|     ...
//|
STATIC mp_obj_t supervisor_set_rgb_status_brightness(mp_obj_t lvl) {
    // This must be int. If cast to uint8_t first, will never raise a ValueError.
    int brightness_int = mp_obj_get_int(lvl);
    if (brightness_int < 0 || brightness_int > 255) {
        mp_raise_ValueError(translate("Brightness must be between 0 and 255"));
    }
    set_status_brightness((uint8_t)brightness_int);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(supervisor_set_rgb_status_brightness_obj, supervisor_set_rgb_status_brightness);

//| def reload() -> None:
//|     """Reload the main Python code and run it (equivalent to hitting Ctrl-D at the REPL)."""
//|     ...
//|
STATIC mp_obj_t supervisor_reload(void) {
    reload_requested = true;
    supervisor_set_run_reason(RUN_REASON_SUPERVISOR_RELOAD);
    mp_raise_reload_exception();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(supervisor_reload_obj, supervisor_reload);

//| def set_next_stack_limit(size: int) -> None:
//|     """Set the size of the stack for the next vm run. If its too large, the default will be used."""
//|     ...
//|
STATIC mp_obj_t supervisor_set_next_stack_limit(mp_obj_t size_obj) {
    mp_int_t size = mp_obj_get_int(size_obj);

    if (size < 256) {
        mp_raise_ValueError(translate("Stack size must be at least 256"));
    }
    set_next_stack_size(size);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(supervisor_set_next_stack_limit_obj, supervisor_set_next_stack_limit);

//| def set_next_code_file(filename: Optional[str], *, reload_on_success : bool = False, reload_on_error: bool = False, sticky_on_success: bool = False, sticky_on_error: bool = False, sticky_on_reload: bool = False) -> None:
//|     """Set what file to run on the next vm run.
//|
//|     When not ``None``, the given ``filename`` is inserted at the front of the usual ['code.py',
//|     'main.py'] search sequence.
//|
//|     The optional keyword arguments specify what happens after the specified file has run:
//|
//|     ``sticky_on_…`` determine whether the newly set filename and options stay in effect: If
//|     True, further runs will continue to run that file (unless it says otherwise by calling
//|     ``set_next_code_filename()`` itself). If False, the settings will only affect one run and
//|     revert to the standard code.py/main.py afterwards.
//|
//|     ``reload_on_…`` determine how to continue: If False, wait in the usual "Code done running.
//|     Waiting for reload. / Press any key to enter the REPL. Use CTRL-D to reload." state. If
//|     True, reload immediately as if CTRL-D was pressed.
//|
//|     ``…_on_success`` take effect when the program runs to completion or calls ``sys.exit()``.
//|
//|     ``…_on_error`` take effect when the program exits with an exception, including the
//|     KeyboardInterrupt caused by CTRL-C.
//|
//|     ``…_on_reload`` take effect when the program is interrupted by files being written to the USB
//|     drive (auto-reload) or when it calls ``supervisor.reload()``.
//|
//|     These settings are stored in RAM, not in persistent memory, and will therefore only affect
//|     soft reloads. Powering off or resetting the device will always revert to standard settings.
//|
//|     When called multiple times in the same run, only the last call takes effect, replacing any
//|     settings made by previous ones. This is the main use of passing ``None`` as a filename: to
//|     reset to the standard search sequence."""
//|     ...
//|
STATIC mp_obj_t supervisor_set_next_code_file(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_filename, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_rom_obj = mp_const_none} },
        { MP_QSTR_reload_on_success, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_reload_on_error, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_sticky_on_success, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_sticky_on_error, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_sticky_on_reload, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };
    struct {
        mp_arg_val_t filename;
        mp_arg_val_t reload_on_success;
        mp_arg_val_t reload_on_error;
        mp_arg_val_t sticky_on_success;
        mp_arg_val_t sticky_on_error;
        mp_arg_val_t sticky_on_reload;
    } args;
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, (mp_arg_val_t *)&args);
    if (!mp_obj_is_str_or_bytes(args.filename.u_obj) && args.filename.u_obj != mp_const_none) {
        mp_raise_TypeError(translate("argument has wrong type"));
    }
    if (args.filename.u_obj == mp_const_none) {
        args.filename.u_obj = mp_const_empty_bytes;
    }
    uint8_t options = 0;
    if (args.reload_on_success.u_bool) {
        options |= SUPERVISOR_NEXT_CODE_OPT_RELOAD_ON_SUCCESS;
    }
    if (args.reload_on_error.u_bool) {
        options |= SUPERVISOR_NEXT_CODE_OPT_RELOAD_ON_ERROR;
    }
    if (args.sticky_on_success.u_bool) {
        options |= SUPERVISOR_NEXT_CODE_OPT_STICKY_ON_SUCCESS;
    }
    if (args.sticky_on_error.u_bool) {
        options |= SUPERVISOR_NEXT_CODE_OPT_STICKY_ON_ERROR;
    }
    if (args.sticky_on_reload.u_bool) {
        options |= SUPERVISOR_NEXT_CODE_OPT_STICKY_ON_RELOAD;
    }
    size_t len;
    const char *filename = mp_obj_str_get_data(args.filename.u_obj, &len);
    free_memory(next_code_allocation);
    if (options != 0 || len != 0) {
        next_code_allocation = allocate_memory(align32_size(sizeof(next_code_info_t) + len + 1), false, true);
        if (next_code_allocation == NULL) {
            m_malloc_fail(sizeof(next_code_info_t) + len + 1);
        }
        next_code_info_t *next_code = (next_code_info_t *)next_code_allocation->ptr;
        next_code->options = options | SUPERVISOR_NEXT_CODE_OPT_NEWLY_SET;
        memcpy(&next_code->filename, filename, len);
        next_code->filename[len] = '\0';
    } else {
        next_code_allocation = NULL;
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(supervisor_set_next_code_file_obj, 0, supervisor_set_next_code_file);

//| def reset_monotonic_epoch() -> None:
//|     """Reset the epoch used by the monotonic clock functions."""
//|     ...
//|
STATIC mp_obj_t supervisor_reset_monotonic_epoch(void) {
    common_hal_time_reset_monotonic_epoch();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(supervisor_reset_monotonic_epoch_obj, supervisor_reset_monotonic_epoch);

//| def ticks() -> int:
//|     """Return the time in milliseconds since an unknown reference point, wrapping after `(1<<29)`ms"""
STATIC mp_obj_t supervisor_ticks_ms(void) {
    uint64_t ticks_ms = common_hal_time_monotonic_ms();
    return mp_obj_new_int((ticks_ms + 0x1fff7777) % (1 << 29));
}
MP_DEFINE_CONST_FUN_OBJ_0(supervisor_ticks_ms_obj, supervisor_ticks_ms);



STATIC const mp_rom_map_elem_t supervisor_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_supervisor) },
    { MP_ROM_QSTR(MP_QSTR_enable_autoreload),  MP_ROM_PTR(&supervisor_enable_autoreload_obj) },
    { MP_ROM_QSTR(MP_QSTR_disable_autoreload),  MP_ROM_PTR(&supervisor_disable_autoreload_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_rgb_status_brightness),  MP_ROM_PTR(&supervisor_set_rgb_status_brightness_obj) },
    { MP_ROM_QSTR(MP_QSTR_runtime),  MP_ROM_PTR(&common_hal_supervisor_runtime_obj) },
    { MP_ROM_QSTR(MP_QSTR_reload),  MP_ROM_PTR(&supervisor_reload_obj) },
    { MP_ROM_QSTR(MP_QSTR_RunReason),  MP_ROM_PTR(&supervisor_run_reason_type) },
    { MP_ROM_QSTR(MP_QSTR_set_next_stack_limit),  MP_ROM_PTR(&supervisor_set_next_stack_limit_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_next_code_file),  MP_ROM_PTR(&supervisor_set_next_code_file_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset_monotonic_epoch),  MP_ROM_PTR(&supervisor_reset_monotonic_epoch_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms),  MP_ROM_PTR(&supervisor_ticks_ms_obj) },
};

STATIC MP_DEFINE_CONST_DICT(supervisor_module_globals, supervisor_module_globals_table);

const mp_obj_module_t supervisor_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&supervisor_module_globals,
};
