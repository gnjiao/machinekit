// HAL pin API

#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_group.h"
#include "hal_internal.h"

/***********************************************************************
*                        "PIN" FUNCTIONS                               *
************************************************************************/

int halg_pin_newfv(const int use_hal_mutex,
		   hal_type_t type,
		   hal_pin_dir_t dir,
		   void ** data_ptr_addr,
		   int owner_id,
		   const char *fmt,
		   va_list ap)
{
    char name[HAL_NAME_LEN + 1];
    int sz;
    sz = rtapi_vsnprintf(name, sizeof(name), fmt, ap);
    if(sz == -1 || sz > HAL_NAME_LEN) {
        HALERR("length %d invalid for name starting '%s'",
	       sz, name);
        return -ENOMEM;
    }
    return (halg_pin_new(use_hal_mutex, name, type, dir, data_ptr_addr, owner_id) == NULL) ? _halerrno: 0;
}

int hal_pin_bit_newf(hal_pin_dir_t dir,
    hal_bit_t ** data_ptr_addr, int owner_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halg_pin_newfv(1, HAL_BIT, dir, (void**)data_ptr_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_pin_float_newf(hal_pin_dir_t dir,
    hal_float_t ** data_ptr_addr, int owner_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halg_pin_newfv(1, HAL_FLOAT, dir, (void**)data_ptr_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_pin_u32_newf(hal_pin_dir_t dir,
    hal_u32_t ** data_ptr_addr, int owner_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halg_pin_newfv(1, HAL_U32, dir, (void**)data_ptr_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_pin_s32_newf(hal_pin_dir_t dir,
    hal_s32_t ** data_ptr_addr, int owner_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halg_pin_newfv(1,HAL_S32, dir, (void**)data_ptr_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

// printf-style version of hal_pin_new()
int hal_pin_newf(hal_type_t type,
		 hal_pin_dir_t dir,
		 void ** data_ptr_addr,
		 int owner_id,
		 const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halg_pin_newfv(1, type, dir, data_ptr_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

// generic printf-style version of hal_pin_new()
int halg_pin_newf(const int use_hal_mutex,
		  hal_type_t type,
		  hal_pin_dir_t dir,
		  void ** data_ptr_addr,
		  int owner_id,
		  const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halg_pin_newfv(use_hal_mutex, type, dir, data_ptr_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

/* this is a generic function that does the majority of the work. */

hal_pin_t *halg_pin_new(const int use_hal_mutex,
			const char *name,
			hal_type_t type,
			hal_pin_dir_t dir,
			void **data_ptr_addr,
			int owner_id)
{
    hal_pin_t *new;
    bool is_legacy = false;

    PCHECK_HALDATA();
    PCHECK_LOCK(HAL_LOCK_LOAD);
    PCHECK_STRLEN(name, HAL_NAME_LEN);


    if (type != HAL_BIT && type != HAL_FLOAT && type != HAL_S32 && type != HAL_U32) {
	HALERR("pin '%s': pin type not one of HAL_BIT, HAL_FLOAT, HAL_S32 or HAL_U32 (%d)",
	       name, type);
	_halerrno = -EINVAL;
	return NULL;
    }

    if (dir != HAL_IN && dir != HAL_OUT && dir != HAL_IO) {
	HALERR("pin '%s': pin direction not one of HAL_IN, HAL_OUT, or HAL_IO (%d)",
	       name, dir);
	_halerrno = -EINVAL;
	return NULL;
    }

    HALDBG("creating pin '%s'", name);

    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);

	hal_comp_t *comp;

	if (halpr_find_pin_by_name(name) != NULL) {
	    HALERR("duplicate pin '%s'", name);
	    _halerrno = -EEXIST;
	    return NULL;
	}

	/* validate comp_id */
	comp = halpr_find_owning_comp(owner_id);
	if (comp == 0) {
	    /* bad comp_id */
	    HALERR("pin '%s': owning component %d not found",
		   name, owner_id);
	    _halerrno = -EINVAL;
	    return NULL;
	}

	/* validate passed in pointer - must point to HAL shmem */
	if (data_ptr_addr != 0) {
	    if(*data_ptr_addr) {
		HALERR("pin '%s': called with already-initialized memory", name);
	    }
	    // the v2 pins dont use data_ptr_addr
	    is_legacy = true;
	    if (! SHMCHK(data_ptr_addr)) {
		/* bad pointer */
		HALERR("pin '%s': data_ptr_addr not in shared memory", name);
		_halerrno = -EINVAL;
		return NULL;
	    }
	}
	// this will be 0 for legacy comps which use comp_id to
	// refer to a comp - pins owned by an instance will refer
	// to an instance:
	hal_inst_t *inst = halpr_find_inst_by_id(owner_id);
	int inst_id = (inst ? ho_id(inst) : 0);

	// instances may create pins post hal_ready
	if ((inst_id == 0) && (comp->state > COMP_INITIALIZING)) {
	    // legacy error message. Never made sense.. why?
	    HALERR("pin '%s': hal_pin_new called after hal_ready (%d)",
		   name, comp->state);
	    _halerrno = -EINVAL;
	    return NULL;
	}

	// allocate pin descriptor
	if ((new = halg_create_object(0, sizeof(hal_pin_t),
				      HAL_PIN, owner_id, name)) == NULL) {
	    _halerrno = -EINVAL;
	    return NULL;
	}

	/* initialize the structure */
	new->type = type;
	new->dir = dir;
	new->signal = 0;
	memset(&new->dummysig, 0, sizeof(hal_data_u));
	if (is_legacy) {
	    hh_set_legacy(&new->hdr);
	    new->data_ptr_addr = SHMOFF(data_ptr_addr);
	    /* make 'data_ptr' point to dummy signal */
	    *data_ptr_addr = comp->shmem_base + SHMOFF(&(new->dummysig));
	} else {
	    // since in v2 this is just a value *, not a value**
	    // just make it point to the dummy signal
	    new->data_ptr = SHMOFF(&new->dummysig);

	    // poison the old value** to ease debugging
	    new->data_ptr_addr =  SHMOFF(&(hal_data->dead_beef));
	}
	// make object visible
	halg_add_object(false, (hal_object_ptr)new);
	return new;
    }
}

void unlink_pin(hal_pin_t * pin)
{
    hal_sig_t *sig;
    hal_comp_t *comp;
    void **data_ptr_addr;
    hal_data_u *dummy_addr, *sig_data_addr;

    /* is this pin linked to a signal? */
    if (pin->signal != 0) {
	/* yes, need to unlink it */
	sig = SHMPTR(pin->signal);

	if (hh_get_legacy(&pin->hdr)) {

	    /* make pin's 'data_ptr' point to its dummy signal */
	    data_ptr_addr = SHMPTR(pin->data_ptr_addr);
	    comp = halpr_find_owning_comp(ho_owner_id(pin));
	    dummy_addr = comp->shmem_base + SHMOFF(&(pin->dummysig));
	    *data_ptr_addr = dummy_addr;

	    dummy_addr = (hal_data_u *)(hal_shmem_base + SHMOFF(&(pin->dummysig))); // XXX use SHMPTR
	} else {
	    pin->data_ptr = SHMOFF(&(pin->dummysig));
	    dummy_addr = (hal_data_u *) &pin->dummysig;
	}
	/* copy current signal value to dummy */
	sig_data_addr = (hal_data_u *)(hal_shmem_base + sig->data_ptr);  //XXX use SHMPTR

	switch (pin->type) {
	case HAL_BIT:
	    dummy_addr->b = sig_data_addr->b;
	    break;
	case HAL_S32:
	    dummy_addr->s = sig_data_addr->s;
	    break;
	case HAL_U32:
	    dummy_addr->u = sig_data_addr->u;
	    break;
	case HAL_FLOAT:
	    dummy_addr->f = sig_data_addr->f;
	    break;
	default:
	    hal_print_msg(RTAPI_MSG_ERR,
			  "HAL: BUG: pin '%s' has invalid type %d !!\n",
			  ho_name(pin), pin->type);
	}

	/* update the signal's reader/writer counts */
	if ((pin->dir & HAL_IN) != 0) {
	    sig->readers--;
	}
	if (pin->dir == HAL_OUT) {
	    sig->writers--;
	}
	if (pin->dir == HAL_IO) {
	    sig->bidirs--;
	}
	/* mark pin as unlinked */
	pin->signal = 0;
    }
}

void free_pin_struct(hal_pin_t * pin)
{
    unlink_pin(pin);
    halg_free_object(false, (hal_object_ptr) pin);
}
