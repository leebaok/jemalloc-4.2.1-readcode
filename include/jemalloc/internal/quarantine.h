/*
 * commented by yuanmu.lb
 * quarantine is default disabled.
 * below is from doc/jemalloc.xml
 *    " Per thread quarantine size in bytes.  If non-zero, each
        thread maintains a FIFO object quarantine that stores up to the
        specified number of bytes of memory.  The quarantined memory is not
        freed until it is released from quarantine, though it is immediately
        junk-filled if the opt.junk option is
        enabled.  This feature is of particular use in combination with Valgrind
		which can detect attempts
        to access quarantined objects.  This is intended for debugging and will
        impact performance negatively.  The default quarantine size is 0 unless
        running inside Valgrind, in which case the default is 16
        MiB. "
 */
/******************************************************************************/
#ifdef JEMALLOC_H_TYPES

typedef struct quarantine_obj_s quarantine_obj_t;
typedef struct quarantine_s quarantine_t;

/* Default per thread quarantine size if valgrind is enabled. */
#define	JEMALLOC_VALGRIND_QUARANTINE_DEFAULT	(ZU(1) << 24)

#endif /* JEMALLOC_H_TYPES */
/******************************************************************************/
#ifdef JEMALLOC_H_STRUCTS

struct quarantine_obj_s {
	void	*ptr;
	size_t	usize;
};

struct quarantine_s {
	size_t			curbytes;
	size_t			curobjs;
	size_t			first;
#define	LG_MAXOBJS_INIT 10
	size_t			lg_maxobjs;
	quarantine_obj_t	objs[1]; /* Dynamically sized ring buffer. */
};

#endif /* JEMALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef JEMALLOC_H_EXTERNS

void	quarantine_alloc_hook_work(tsd_t *tsd);
void	quarantine(tsd_t *tsd, void *ptr);
void	quarantine_cleanup(tsd_t *tsd);

#endif /* JEMALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef JEMALLOC_H_INLINES

#ifndef JEMALLOC_ENABLE_INLINE
void	quarantine_alloc_hook(void);
#endif

#if (defined(JEMALLOC_ENABLE_INLINE) || defined(JEMALLOC_QUARANTINE_C_))
JEMALLOC_ALWAYS_INLINE void
quarantine_alloc_hook(void)
{
	tsd_t *tsd;

	assert(config_fill && opt_quarantine);

	tsd = tsd_fetch();
	if (tsd_quarantine_get(tsd) == NULL)
		quarantine_alloc_hook_work(tsd);
}
#endif

#endif /* JEMALLOC_H_INLINES */
/******************************************************************************/

