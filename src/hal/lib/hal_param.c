// HAL param API

#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_internal.h"

static hal_param_t *alloc_param_struct(void);

/***********************************************************************
*                       "PARAM" FUNCTIONS                              *
************************************************************************/

static int halinst_param_newfv(hal_type_t type,
			       hal_param_dir_t dir,
			       void *data_addr,
			       int comp_id,
			       int inst_id,
			       const char *fmt,
			       va_list ap)
{
    char name[HAL_NAME_LEN + 1];
    int sz;
    sz = rtapi_vsnprintf(name, sizeof(name), fmt, ap);
    if(sz == -1 || sz > HAL_NAME_LEN) {
        hal_print_msg(RTAPI_MSG_ERR,
	    "hal_param_newfv: length %d too long for name starting '%s'\n",
	    sz, name);
	return -ENOMEM;
    }
    return halinst_param_new(name, type, dir, (void *) data_addr, comp_id, inst_id);
}

int hal_param_bit_newf(hal_param_dir_t dir, hal_bit_t * data_addr,
		       int comp_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halinst_param_newfv(HAL_BIT, dir, (void*)data_addr, comp_id, 0, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_param_float_newf(hal_param_dir_t dir, hal_float_t * data_addr,
			 int comp_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halinst_param_newfv(HAL_FLOAT, dir, (void*)data_addr, comp_id, 0, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_param_u32_newf(hal_param_dir_t dir, hal_u32_t * data_addr,
		       int comp_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halinst_param_newfv(HAL_U32, dir, (void*)data_addr, comp_id, 0, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_param_s32_newf(hal_param_dir_t dir, hal_s32_t * data_addr,
		       int comp_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halinst_param_newfv(HAL_S32, dir, (void*)data_addr, comp_id, 0, fmt, ap);
    va_end(ap);
    return ret;
}

// printf-style version of hal_param_new()
int hal_param_newf(hal_type_t type,
		   hal_param_dir_t dir,
		   void * data_addr,
		   int comp_id,
		   const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_param_newfv(type, dir, data_addr, comp_id, fmt, ap);
    va_end(ap);
    return ret;
}

int halinst_param_bit_newf(hal_param_dir_t dir, hal_bit_t * data_addr,
			   int comp_id, int inst_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halinst_param_newfv(HAL_BIT, dir, (void*)data_addr, comp_id, inst_id, fmt, ap);
    va_end(ap);
    return ret;
}

int halinst_param_float_newf(hal_param_dir_t dir, hal_float_t * data_addr,
			 int comp_id, int inst_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halinst_param_newfv(HAL_FLOAT, dir, (void*)data_addr, comp_id, inst_id, fmt, ap);
    va_end(ap);
    return ret;
}

int halinst_param_u32_newf(hal_param_dir_t dir, hal_u32_t * data_addr,
		       int comp_id, int inst_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halinst_param_newfv(HAL_U32, dir, (void*)data_addr, comp_id, inst_id, fmt, ap);
    va_end(ap);
    return ret;
}

int halinst_param_s32_newf(hal_param_dir_t dir, hal_s32_t * data_addr,
		       int comp_id, int inst_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = halinst_param_newfv(HAL_S32, dir, (void*)data_addr, comp_id, inst_id, fmt, ap);
    va_end(ap);
    return ret;
}

/* this is a generic function that does the majority of the work. */

int halinst_param_new(const char *name, hal_type_t type, hal_param_dir_t dir, volatile void *data_addr,
		      int comp_id, int inst_id)
{
    int *prev, next, cmp;
    hal_param_t *new, *ptr;

    if (hal_data == 0) {
	hal_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: param_new called before init\n");
	return -EINVAL;
    }

    if (type != HAL_BIT && type != HAL_FLOAT && type != HAL_S32 && type != HAL_U32) {
	hal_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: pin type not one of HAL_BIT, HAL_FLOAT, HAL_S32 or HAL_U32\n");
	return -EINVAL;
    }

    if (dir != HAL_RO && dir != HAL_RW) {
	hal_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: param direction not one of HAL_RO, or HAL_RW\n");
	return -EINVAL;
    }

    if (strlen(name) > HAL_NAME_LEN) {
	hal_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: parameter name '%s' is too long\n", name);
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_LOAD)  {
	hal_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: param_new called while HAL locked\n");
	return -EPERM;
    }
    {
	hal_comp_t *comp  __attribute__((cleanup(halpr_autorelease_mutex)));
	hal_inst_t *inst = NULL;

	/* get mutex before accessing shared data */
	rtapi_mutex_get(&(hal_data->mutex));

	hal_print_msg(RTAPI_MSG_DBG, "HAL: creating parameter '%s'\n", name);

	/* validate comp_id */
	comp = halpr_find_comp_by_id(comp_id);
	if (comp == 0) {
	    /* bad comp_id */
	    hal_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: component %d not found\n", comp_id);
	    return -EINVAL;
	}

	// validate inst_id if given
	if (inst_id) { // pin is in an instantiable comp
	    inst = halpr_find_inst_by_id(inst_id);
	    if (inst == NULL) {
		hal_print_error("%s: instance %d not found", __FUNCTION__, inst_id);
		return -EINVAL;
	    }
	    // validate that the param actually is allocated in the instance data blob
	    if (!halpr_ptr_in_inst(inst, (void *) data_addr)) {
		hal_print_error("memory for param %s not within instance %s/%d memory range",
				name, inst->name, inst_id);
		return -EINVAL;
	    }
	}
	/* validate passed in pointer - must point to HAL shmem */
	if (! SHMCHK(data_addr)) {
	    /* bad pointer */
	    hal_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: data_addr not in shared memory\n");
	    return -EINVAL;
	}
	// instances may create params post hal_ready
	if ((inst_id == 0) && (comp->state > COMP_INITIALIZING)) {
	    hal_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: param_new called after hal_ready\n");
	    return -EINVAL;
	}
	/* allocate a new parameter structure */
	new = alloc_param_struct();
	if (new == 0) {
	    /* alloc failed */
	    hal_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: insufficient memory for parameter '%s'\n", name);
	    return -ENOMEM;
	}
	/* initialize the structure */
	new->owner_ptr = SHMOFF(comp);
	new->instance_ptr = (inst_id == 0) ? 0 : SHMOFF(inst);
	new->data_ptr = SHMOFF(data_addr);
	new->type = type;
	new->dir = dir;
	new->handle = rtapi_next_handle();
	rtapi_snprintf(new->name, sizeof(new->name), "%s", name);
	/* search list for 'name' and insert new structure */
	prev = &(hal_data->param_list_ptr);
	next = *prev;
	while (1) {
	    if (next == 0) {
		/* reached end of list, insert here */
		new->next_ptr = next;
		*prev = SHMOFF(new);
		return 0;
	    }
	    ptr = SHMPTR(next);
	    cmp = strcmp(ptr->name, new->name);
	    if (cmp > 0) {
		/* found the right place for it, insert here */
		new->next_ptr = next;
		*prev = SHMOFF(new);
		return 0;
	    }
	    if (cmp == 0) {
		/* name already in list, can't insert */
		free_param_struct(new);
		hal_print_msg(RTAPI_MSG_ERR,
				"HAL: ERROR: duplicate parameter '%s'\n", name);
		return -EINVAL;
	    }
	    /* didn't find it yet, look at next one */
	    prev = &(ptr->next_ptr);
	    next = *prev;
	}
    }
}

/* wrapper functs for typed params - these call the generic funct below */

int hal_param_bit_set(const char *name, int value)
{
    return hal_param_set(name, HAL_BIT, &value);
}

int hal_param_float_set(const char *name, double value)
{
    return hal_param_set(name, HAL_FLOAT, &value);
}

int hal_param_u32_set(const char *name, unsigned long value)
{
    return hal_param_set(name, HAL_U32, &value);
}

int hal_param_s32_set(const char *name, signed long value)
{
    return hal_param_set(name, HAL_S32, &value);
}

/* this is a generic function that does the majority of the work */

int hal_param_set(const char *name, hal_type_t type, void *value_addr)
{

    void *d_ptr;

    if (hal_data == 0) {
	hal_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: param_set called before init\n");
	return -EINVAL;
    }

    if (hal_data->lock & HAL_LOCK_PARAMS)  {
	hal_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: param_set called while HAL locked\n");
	return -EPERM;
    }

    hal_print_msg(RTAPI_MSG_DBG, "HAL: setting parameter '%s'\n", name);

    {
	hal_param_t *param __attribute__((cleanup(halpr_autorelease_mutex)));

	/* get mutex before accessing shared data */
	rtapi_mutex_get(&(hal_data->mutex));

	/* search param list for name */
	param = halpr_find_param_by_name(name);
	if (param == 0) {
	    /* parameter not found */
	    hal_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: parameter '%s' not found\n", name);
	    return -EINVAL;
	}
	/* found it, is type compatible? */
	if (param->type != type) {
	    hal_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: type mismatch setting param '%s'\n",
			    name);
	    return -EINVAL;
	}
	/* is it read only? */
	if (param->dir == HAL_RO) {
	    hal_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: param '%s' is not writable\n",
			    name);
	    return -EINVAL;
	}
	/* everything is OK, set the value */
	d_ptr = SHMPTR(param->data_ptr);
	switch (param->type) {
	case HAL_BIT:
	    if (*((int *) value_addr) == 0) {
		*(hal_bit_t *) (d_ptr) = 0;
	    } else {
		*(hal_bit_t *) (d_ptr) = 1;
	    }
	    break;
	case HAL_FLOAT:
	    *((hal_float_t *) (d_ptr)) = *((double *) (value_addr));
	    break;
	case HAL_S32:
	    *((hal_s32_t *) (d_ptr)) = *((signed long *) (value_addr));
	    break;
	case HAL_U32:
	    *((hal_u32_t *) (d_ptr)) = *((unsigned long *) (value_addr));
	    break;
	default:
	    /* Shouldn't get here, but just in case... */
	    hal_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: bad type %d setting param\n", param->type);
	    return -EINVAL;
	}
    }
    return 0;
}

int hal_param_alias(const char *param_name, const char *alias)
{
    int *prev, next, cmp;
    hal_param_t *param, *ptr;


    if (hal_data == 0) {
	hal_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: param_alias called before init\n");
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_CONFIG)  {
	hal_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: param_alias called while HAL locked\n");
	return -EPERM;
    }
    if (alias != NULL ) {
	if (strlen(alias) > HAL_NAME_LEN) {
	    hal_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: alias name '%s' is too long\n", alias);
	    return -EINVAL;
	}
    }

    {
	hal_oldname_t *oldname __attribute__((cleanup(halpr_autorelease_mutex)));

	/* get mutex before accessing shared data */
	rtapi_mutex_get(&(hal_data->mutex));

	if (alias != NULL ) {
	    param = halpr_find_param_by_name(alias);
	    if ( param != NULL ) {
		hal_print_msg(RTAPI_MSG_ERR,
				"HAL: ERROR: duplicate pin/alias name '%s'\n", alias);
		return -EINVAL;
	    }
	}
	/* once we unlink the param from the list, we don't want to have to
	   abort the change and repair things.  So we allocate an oldname
	   struct here, then free it (which puts it on the free list).  This
	   allocation might fail, in which case we abort the command.  But
	   if we actually need the struct later, the next alloc is guaranteed
	   to succeed since at least one struct is on the free list. */
	oldname = halpr_alloc_oldname_struct();
	if ( oldname == NULL ) {
	    hal_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: insufficient memory for param_alias\n");
	    return -EINVAL;
	}
	free_oldname_struct(oldname);
	/* find the param and unlink it from pin list */
	prev = &(hal_data->param_list_ptr);
	next = *prev;
	while (1) {
	    if (next == 0) {
		/* reached end of list, not found */
		hal_print_msg(RTAPI_MSG_ERR,
				"HAL: ERROR: param '%s' not found\n", param_name);
		return -EINVAL;
	    }
	    param = SHMPTR(next);
	    if ( strcmp(param->name, param_name) == 0 ) {
		/* found it, unlink from list */
		*prev = param->next_ptr;
		break;
	    }
	    if (param->oldname != 0 ) {
		oldname = SHMPTR(param->oldname);
		if (strcmp(oldname->name, param_name) == 0) {
		    /* found it, unlink from list */
		    *prev = param->next_ptr;
		    break;
		}
	    }
	    /* didn't find it yet, look at next one */
	    prev = &(param->next_ptr);
	    next = *prev;
	}
	if ( alias != NULL ) {
	    /* adding a new alias */
	    if ( param->oldname == 0 ) {
		/* save old name (only if not already saved) */
		oldname = halpr_alloc_oldname_struct();
		param->oldname = SHMOFF(oldname);
		rtapi_snprintf(oldname->name, sizeof(oldname->name), "%s", param->name);
	    }
	    /* change param's name to 'alias' */
	    rtapi_snprintf(param->name, sizeof(param->name), "%s", alias);
	} else {
	    /* removing an alias */
	    if ( param->oldname != 0 ) {
		/* restore old name (only if param is aliased) */
		oldname = SHMPTR(param->oldname);
		rtapi_snprintf(param->name, sizeof(param->name), "%s", oldname->name);
		param->oldname = 0;
		free_oldname_struct(oldname);
	    }
	}
	/* insert param back into list in proper place */
	prev = &(hal_data->param_list_ptr);
	next = *prev;
	while (1) {
	    if (next == 0) {
		/* reached end of list, insert here */
		param->next_ptr = next;
		*prev = SHMOFF(param);
		return 0;
	    }
	    ptr = SHMPTR(next);
	    cmp = strcmp(ptr->name, param->name);
	    if (cmp > 0) {
		/* found the right place for it, insert here */
		param->next_ptr = next;
		*prev = SHMOFF(param);
		return 0;
	    }
	    /* didn't find it yet, look at next one */
	    prev = &(ptr->next_ptr);
	    next = *prev;
	}
    }
}

hal_param_t *halpr_find_param_by_name(const char *name)
{
    int next;
    hal_param_t *param;
    hal_oldname_t *oldname;

    /* search parameter list for 'name' */
    next = hal_data->param_list_ptr;
    while (next != 0) {
	param = SHMPTR(next);
	if (strcmp(param->name, name) == 0) {
	    /* found a match */
	    return param;
	}
	if (param->oldname != 0 ) {
	    oldname = SHMPTR(param->oldname);
	    if (strcmp(oldname->name, name) == 0) {
		/* found a match */
		return param;
	    }
	}
	/* didn't find it yet, look at next one */
	next = param->next_ptr;
    }
    /* if loop terminates, we reached end of list with no match */
    return 0;
}

hal_param_t *halpr_find_param_by_owner(hal_comp_t * owner,
    hal_param_t * start)
{
    int owner_ptr, next;
    hal_param_t *param;

    /* get offset of 'owner' component */
    owner_ptr = SHMOFF(owner);
    /* is this the first call? */
    if (start == 0) {
	/* yes, start at beginning of param list */
	next = hal_data->param_list_ptr;
    } else {
	/* no, start at next param */
	next = start->next_ptr;
    }
    while (next != 0) {
	param = SHMPTR(next);
	if (param->owner_ptr == owner_ptr) {
	    /* found a match */
	    return param;
	}
	/* didn't find it yet, look at next one */
	next = param->next_ptr;
    }
    /* if loop terminates, we reached end of list without finding a match */
    return 0;
}

hal_param_t *alloc_param_struct(void)
{
    hal_param_t *p;

    /* check the free list */
    if (hal_data->param_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->param_free_ptr);
	/* unlink it from the free list */
	hal_data->param_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_param_t));
    }
    if (p) {
	/* make sure it's empty */
	p->next_ptr = 0;
	p->data_ptr = 0;
	p->owner_ptr = 0;
	p->type = 0;
	p->name[0] = '\0';
    }
    return p;
}

void free_param_struct(hal_param_t * p)
{
    /* clear contents of struct */
    if ( p->oldname != 0 ) free_oldname_struct(SHMPTR(p->oldname));
    p->data_ptr = 0;
    p->owner_ptr = 0;
    p->instance_ptr = 0;
    p->type = 0;
    p->name[0] = '\0';
    p->handle = -1;
    /* add it to free list (params use the same struct as src vars) */
    p->next_ptr = hal_data->param_free_ptr;
    hal_data->param_free_ptr = SHMOFF(p);
}
