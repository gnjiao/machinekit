#include "config.h"
#include "hal.h"
#include "hal_priv.h"
#include "hal_accessor.h"

// allocators for accessor-style pins and sigs
// passing NULL for data_ptr_addr makes them v2 pins

bit_pin_ptr halx_pin_bit_newf(const hal_pin_dir_t dir,
			      const int owner_id,
			      const char *fmt, ...)
{
    va_list ap;
    bit_pin_ptr p;
    va_start(ap, fmt);
    p._bp = halg_pin_newfv(1, HAL_BIT, dir, NULL, owner_id, fmt, ap);
    va_end(ap);
    return p;
}

float_pin_ptr halx_pin_float_newf(const hal_pin_dir_t dir,
			const int owner_id,
			const char *fmt, ...)
{
    va_list ap;
    float_pin_ptr p;
    va_start(ap, fmt);
    p._fp = halg_pin_newfv(1, HAL_FLOAT, dir, NULL, owner_id, fmt, ap);
    va_end(ap);
    return p;
}

u32_pin_ptr halx_pin_u32_newf(const hal_pin_dir_t dir,
		      const int owner_id,
		      const char *fmt, ...)
{
    va_list ap;
    u32_pin_ptr p;
    va_start(ap, fmt);
    p._up = halg_pin_newfv(1, HAL_U32, dir, NULL, owner_id, fmt, ap);
    va_end(ap);
    return p;
}

s32_pin_ptr halx_pin_s32_newf(const hal_pin_dir_t dir,
		      const int owner_id,
		      const char *fmt, ...)
{
    va_list ap;
    s32_pin_ptr p;
    va_start(ap, fmt);
    p._sp = halg_pin_newfv(1,HAL_S32, dir, NULL, owner_id, fmt, ap);
    va_end(ap);
    return p;
}
