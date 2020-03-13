/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Jeff Epler for Adafruit Industries
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

#include "py/obj.h"
#include "py/objproperty.h"
#include "py/runtime.h"
#include "common-hal/_protomatter/Protomatter.h"
#include "shared-bindings/_protomatter/Protomatter.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "shared-bindings/microcontroller/__init__.h"
#include "shared-bindings/util.h"

extern Protomatter_core *_PM_protoPtr;
extern void _PM_convert_565_byte(Protomatter_core *core, uint16_t *source,
  uint16_t width);

STATIC mp_obj_t protomatter_protomatter_deinit(mp_obj_t self_in);

STATIC uint8_t validate_pin(mp_obj_t obj) {
    mcu_pin_obj_t *result = validate_obj_is_free_pin(obj);
    return common_hal_mcu_pin_number(result);
}

STATIC uint8_t *validate_pins(mp_obj_t seq, uint8_t *count_out) {
    mp_int_t len = MP_OBJ_SMALL_INT_VALUE(mp_obj_len(seq));
    for (mp_int_t i=0; i<len; i++) {
        validate_obj_is_free_pin(mp_obj_subscr(seq, MP_OBJ_NEW_SMALL_INT(i), MP_OBJ_SENTINEL));
    }
    *count_out = len;
    uint8_t *pin_nos = m_new(uint8_t, len);
    for (mp_int_t i=0; i<len; i++) {
            pin_nos[i] = common_hal_mcu_pin_number(mp_obj_subscr(seq, MP_OBJ_NEW_SMALL_INT(i), MP_OBJ_SENTINEL));
    }
    return pin_nos;
}

STATIC void claim_pins(mp_obj_t seq) {
    mp_int_t len = MP_OBJ_SMALL_INT_VALUE(mp_obj_len(seq));
    for (mp_int_t i=0; i<len; i++) {
        common_hal_mcu_pin_claim(mp_obj_subscr(seq, MP_OBJ_NEW_SMALL_INT(i), MP_OBJ_SENTINEL));
    }
}

STATIC mp_obj_t protomatter_protomatter_make_new(const mp_obj_type_t *type, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_bit_width, ARG_bit_depth, ARG_rgb_list, ARG_addr_list,
        ARG_clock_pin, ARG_latch_pin, ARG_oe_pin, ARG_doublebuffer };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_bit_width, MP_ARG_INT | MP_ARG_REQUIRED },
        { MP_QSTR_bit_depth, MP_ARG_INT | MP_ARG_REQUIRED },
        { MP_QSTR_rgb_pins, MP_ARG_OBJ | MP_ARG_REQUIRED },
        { MP_QSTR_addr_pins, MP_ARG_OBJ | MP_ARG_REQUIRED },
        { MP_QSTR_clock_pin, MP_ARG_OBJ | MP_ARG_REQUIRED },
        { MP_QSTR_latch_pin, MP_ARG_OBJ | MP_ARG_REQUIRED },
        { MP_QSTR_oe_pin, MP_ARG_OBJ | MP_ARG_REQUIRED },
        { MP_QSTR_doublebuffer, MP_ARG_BOOL, { .u_bool = false } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    protomatter_protomatter_obj_t *self =
            m_new_obj(protomatter_protomatter_obj_t);
    self->base.type = &protomatter_Protomatter_type;

    self->rgb_pins = validate_pins(args[ARG_rgb_list].u_obj, &self->rgb_count);
    self->addr_pins = validate_pins(args[ARG_addr_list].u_obj, &self->addr_count);
    self->clock_pin = validate_pin(args[ARG_clock_pin].u_obj);
    self->oe_pin = validate_pin(args[ARG_oe_pin].u_obj);
    self->latch_pin = validate_pin(args[ARG_latch_pin].u_obj);

    self->timer = common_hal_protomatter_timer_allocate(); 
    if (self->timer == NULL) {
        mp_raise_ValueError(translate("No timer available"));
    }

    claim_pins(args[ARG_rgb_list].u_obj);
    claim_pins(args[ARG_addr_list].u_obj);
    common_hal_mcu_pin_claim(args[ARG_clock_pin].u_obj);
    common_hal_mcu_pin_claim(args[ARG_oe_pin].u_obj);
    common_hal_mcu_pin_claim(args[ARG_latch_pin].u_obj);

    ProtomatterStatus stat = _PM_init(&self->core,
        args[ARG_bit_width].u_int, args[ARG_bit_depth].u_int,
        self->rgb_count/6, self->rgb_pins,
        self->addr_count, self->addr_pins,
        self->clock_pin, self->latch_pin, self->oe_pin,
        args[ARG_doublebuffer].u_bool, self->timer);

    if (stat == PROTOMATTER_OK) {
        _PM_protoPtr = &self->core;

        uint16_t *framebuffer = m_malloc(2*64*32, 0);
        for(int i=0; i<64*32; i++) {
            int r = (i % 2) ^ ((i / 64) % 2);
            framebuffer[i] = r ? 0xf000 : 0x000f;
        }
        for(int i=0; i<64; i++) {
            framebuffer[i] = i >> 1;
            framebuffer[64+i] = i << 5;
            framebuffer[128+i] = (i >> 1) << 11;
            framebuffer[192+i] = (i >> 1) | (i << 5) | (i >> 1) << 11;
        }

common_hal_mcu_disable_interrupts();
        stat = _PM_begin(&self->core);
        _PM_convert_565_byte(&self->core, framebuffer, 64);

//        while(true) _PM_row_handler(&self->core);
        common_hal_protomatter_timer_enable(self->timer);
common_hal_mcu_enable_interrupts();
        m_free(framebuffer);
    }

    if (stat != PROTOMATTER_OK) {
        // XXX this deinit() actually makes crashy-crashy
        // can trigger it by sending inappropriate pins
        protomatter_protomatter_deinit(self);
        switch (stat) {
        case PROTOMATTER_ERR_PINS:
            mp_raise_ValueError(translate("Invalid pin"));
            break;
        case PROTOMATTER_ERR_ARG:
            mp_raise_ValueError(translate("Invalid argument"));
            break;
        case PROTOMATTER_ERR_MALLOC: /// should have already been signaled as NLR
        default:
            mp_raise_msg_varg(&mp_type_RuntimeError,
                translate("Protomatter internal error #%d"), (int)stat);
            break;
        }
    }

    self->paused = 0;

    return MP_OBJ_FROM_PTR(self);
}

STATIC void free_pin(uint8_t *pin) {
    if (*pin != COMMON_HAL_MCU_NO_PIN) {
        common_hal_mcu_pin_reset_number(*pin);
    }
    *pin = COMMON_HAL_MCU_NO_PIN;
}

STATIC void free_pin_seq(uint8_t **seq, int count) {
    if (!*seq) {
        return;
    }

    for (int i=0; i<count; i++) {
        uint8_t pin = (*seq)[i];
        if (pin != COMMON_HAL_MCU_NO_PIN) {
            common_hal_mcu_pin_reset_number(pin);
        }
        (*seq)[i] = COMMON_HAL_MCU_NO_PIN;
    }

    m_free(*seq);
    *seq = NULL;
}

STATIC mp_obj_t protomatter_protomatter_deinit(mp_obj_t self_in) {
    protomatter_protomatter_obj_t *self = (protomatter_protomatter_obj_t*)self_in;
    if (self->timer) {
        common_hal_protomatter_timer_free(self->timer);
        self->timer = 0;
    }

    free_pin_seq(&self->rgb_pins, self->rgb_count);
    free_pin_seq(&self->addr_pins, self->addr_count);
    free_pin(&self->clock_pin);
    free_pin(&self->latch_pin);
    free_pin(&self->oe_pin);

    if (self->core.rgbPins) {
        _PM_free(&self->core);
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(protomatter_protomatter_deinit_obj, protomatter_protomatter_deinit);

static void check_for_deinit(protomatter_protomatter_obj_t *self) {
    if(!self->core.rgbPins) {
        raise_deinited_error();
    }
}

STATIC mp_obj_t protomatter_protomatter_get_paused(mp_obj_t self_in) {
    protomatter_protomatter_obj_t *self = (protomatter_protomatter_obj_t*)self_in;
    check_for_deinit(self);
    return mp_obj_new_bool(self->paused);
}
MP_DEFINE_CONST_FUN_OBJ_1(protomatter_protomatter_get_paused_obj, protomatter_protomatter_get_paused);

STATIC mp_obj_t protomatter_protomatter_set_paused(mp_obj_t self_in, mp_obj_t value_in)  {
    protomatter_protomatter_obj_t *self = (protomatter_protomatter_obj_t*)self_in;
    check_for_deinit(self);
    bool paused = mp_obj_is_true(value_in);
    if (paused && !self->paused) {
        _PM_stop(&self->core);
    } else if(!paused && self->paused) {
        _PM_resume(&self->core);
    }
    
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(protomatter_protomatter_set_paused_obj, protomatter_protomatter_set_paused);

const mp_obj_property_t protomatter_protomatter_paused_obj = {
    .base.type = &mp_type_property,
    .proxy = {(mp_obj_t)&protomatter_protomatter_get_paused_obj,
              (mp_obj_t)&protomatter_protomatter_set_paused_obj,
              (mp_obj_t)&mp_const_none_obj},
};

STATIC mp_obj_t protomatter_protomatter_get_frame_count(mp_obj_t self_in) {
    protomatter_protomatter_obj_t *self = (protomatter_protomatter_obj_t*)self_in;
    check_for_deinit(self);
    return MP_OBJ_NEW_SMALL_INT(_PM_getFrameCount(&self->core));
}
MP_DEFINE_CONST_FUN_OBJ_1(protomatter_protomatter_get_frame_count_obj, protomatter_protomatter_get_frame_count);

const mp_obj_property_t protomatter_protomatter_frame_count_obj = {
    .base.type = &mp_type_property,
    .proxy = {(mp_obj_t)&protomatter_protomatter_get_frame_count_obj,
              (mp_obj_t)&mp_const_none_obj,
              (mp_obj_t)&mp_const_none_obj},
};

STATIC const mp_rom_map_elem_t protomatter_protomatter_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&protomatter_protomatter_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_paused), MP_ROM_PTR(&protomatter_protomatter_paused_obj) },
    { MP_ROM_QSTR(MP_QSTR_frame_count), MP_ROM_PTR(&protomatter_protomatter_frame_count_obj) },
// begin (method)
// stop (method)
// resume (method)
// frameCount (property)
};
STATIC MP_DEFINE_CONST_DICT(protomatter_protomatter_locals_dict, protomatter_protomatter_locals_dict_table);

const mp_obj_type_t protomatter_Protomatter_type = {
    { &mp_type_type },
    .name = MP_QSTR_Protomatter,
    .make_new = protomatter_protomatter_make_new,
    .locals_dict = (mp_obj_dict_t*)&protomatter_protomatter_locals_dict,
};
