/******************************************************************************/
#ifdef JEMALLOC_H_TYPES

#define	LARGE_MINCLASS		(ZU(1) << LG_LARGE_MINCLASS)

/* Maximum number of regions in one run. */
#define	LG_RUN_MAXREGS		(LG_PAGE - LG_TINY_MIN)
#define	RUN_MAXREGS		(1U << LG_RUN_MAXREGS)

/*
 * Minimum redzone size.  Redzones may be larger than this if necessary to
 * preserve region alignment.
 */
#define	REDZONE_MINSIZE		16

/*
 * The minimum ratio of active:dirty pages per arena is computed as:
 *
 *   (nactive >> lg_dirty_mult) >= ndirty
 *
 * So, supposing that lg_dirty_mult is 3, there can be no less than 8 times as
 * many active pages as dirty pages.
 */
#define	LG_DIRTY_MULT_DEFAULT	3

/*
 * 内存清理模式
 * ratio : 清理尽可能少的 run/chunk 使得 arena->ndirty <= ndirty_limit
 * decay : 清理尽可能多的 run/chunk 使得 arena->ndirty >= ndirty_limit
 */
typedef enum {
	purge_mode_ratio = 0,
	purge_mode_decay = 1,

	purge_mode_limit = 2
} purge_mode_t;
#define	PURGE_DEFAULT		purge_mode_ratio
/* Default decay time in seconds. */
#define	DECAY_TIME_DEFAULT	10
/* Number of event ticks between time checks. */
#define	DECAY_NTICKS_PER_UPDATE	1000

typedef struct arena_runs_dirty_link_s arena_runs_dirty_link_t;
typedef struct arena_avail_links_s arena_avail_links_t;
typedef struct arena_run_s arena_run_t;
typedef struct arena_chunk_map_bits_s arena_chunk_map_bits_t;
typedef struct arena_chunk_map_misc_s arena_chunk_map_misc_t;
typedef struct arena_chunk_s arena_chunk_t;
typedef struct arena_bin_info_s arena_bin_info_t;
typedef struct arena_bin_s arena_bin_t;
typedef struct arena_s arena_t;
typedef struct arena_tdata_s arena_tdata_t;

#endif /* JEMALLOC_H_TYPES */
/******************************************************************************/
#ifdef JEMALLOC_H_STRUCTS

#ifdef JEMALLOC_ARENA_STRUCTS_A
/*
 * small run 的元数据
 * 存放在 arena_chunk_map_misc_t 中 (arena_chunk_map_misc_t 存在 chunk_header)
 */
struct arena_run_s {
	/* 该 run 的 bin index */
	szind_t		binind;

	/* 可用的 region 的数量 */
	unsigned	nfree;

	/* 标记每个 region 是否可用的 bitmap */
	bitmap_t	bitmap[BITMAP_GROUPS_MAX];
};

/* arena_chunk_map_bits_s 标记 chunk 中每一个 page 的状态，存在 chunk_header中 */
struct arena_chunk_map_bits_s {
	/*
	 * Run address (or size) and various flags are stored together.  The bit
	 * layout looks like (assuming 32-bit system):
	 *
	 *   ???????? ???????? ???nnnnn nnndumla
	 *
	 * ? : Unallocated: Run address for first/last pages, unset for internal
	 *                  pages.
	 *     Small: Run page offset.
	 *     Large: Run page count for first page, unset for trailing pages.
	 * n : binind for small size class, BININD_INVALID for large size class.
	 * d : dirty?
	 * u : unzeroed?
	 * m : decommitted?
	 * l : large?
	 * a : allocated?
	 *
	 * Following are example bit patterns for the three types of runs.
	 *
	 * p : run page offset
	 * s : run size
	 * n : binind for size class; large objects set these to BININD_INVALID
	 * x : don't care
	 * - : 0
	 * + : 1
	 * [DUMLA] : bit set
	 * [dumla] : bit unset
	 *
	 *   Unallocated (clean):                    
	 *     ssssssss ssssssss sss+++++ +++dum-a  ---- first page mapbit
	 *     xxxxxxxx xxxxxxxx xxxxxxxx xxx-Uxxx  ---- internal page mapbit
	 *     ssssssss ssssssss sss+++++ +++dUm-a  ---- last page mapbit
	 *
	 *   Unallocated (dirty):
	 *     ssssssss ssssssss sss+++++ +++D-m-a
	 *     xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
	 *     ssssssss ssssssss sss+++++ +++D-m-a
	 *
	 *   Small:
	 *     pppppppp pppppppp pppnnnnn nnnd---A
	 *     pppppppp pppppppp pppnnnnn nnn----A
	 *     pppppppp pppppppp pppnnnnn nnnd---A
	 *
	 *   Large:
	 *     ssssssss ssssssss sss+++++ +++D--LA
	 *     xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
	 *     -------- -------- ---+++++ +++D--LA
	 *
	 *   Large (sampled, size <= LARGE_MINCLASS):
	 *     ssssssss ssssssss sssnnnnn nnnD--LA
	 *     xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
	 *     -------- -------- ---+++++ +++D--LA
	 *
	 *   Large (not sampled, size == LARGE_MINCLASS):
	 *     ssssssss ssssssss sss+++++ +++D--LA
	 *     xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
	 *     -------- -------- ---+++++ +++D--LA
	 */
	size_t				bits;
#define	CHUNK_MAP_ALLOCATED	((size_t)0x01U)
#define	CHUNK_MAP_LARGE		((size_t)0x02U)
#define	CHUNK_MAP_STATE_MASK	((size_t)0x3U)

#define	CHUNK_MAP_DECOMMITTED	((size_t)0x04U)
#define	CHUNK_MAP_UNZEROED	((size_t)0x08U)
#define	CHUNK_MAP_DIRTY		((size_t)0x10U)
#define	CHUNK_MAP_FLAGS_MASK	((size_t)0x1cU)

#define	CHUNK_MAP_BININD_SHIFT	5
#define	BININD_INVALID		((size_t)0xffU)
#define	CHUNK_MAP_BININD_MASK	(BININD_INVALID << CHUNK_MAP_BININD_SHIFT)
#define	CHUNK_MAP_BININD_INVALID CHUNK_MAP_BININD_MASK

#define	CHUNK_MAP_RUNIND_SHIFT	(CHUNK_MAP_BININD_SHIFT + 8)
#define	CHUNK_MAP_SIZE_SHIFT	(CHUNK_MAP_RUNIND_SHIFT - LG_PAGE)
#define	CHUNK_MAP_SIZE_MASK						\
    (~(CHUNK_MAP_BININD_MASK | CHUNK_MAP_FLAGS_MASK | CHUNK_MAP_STATE_MASK))
};

/*
 * qr 是通过宏实现的 双向环形列表
 */
struct arena_runs_dirty_link_s {
	qr(arena_runs_dirty_link_t)	rd_link;
};

/*
 * 每一个 arena_chunk_map_misc_t 表示 chunk 中的一页，就像 arena_chunk_map_bits_t
 */
struct arena_chunk_map_misc_s {
	/*
	 * ph_link 用于构建 run 的堆，有两个互斥的使用场景：
	 * 1) arena 的 runs_avail 堆，管理 arena 的 run
	 * 2) arena 的 bin 的 runs 堆，管理分配给某个 bin 的 run
	 */
	phn(arena_chunk_map_misc_t)		ph_link;

	union {
		/* 用来链接 dirty run, arena 的 runs_dirty 就是使用 rd 构建 */
		arena_runs_dirty_link_t		rd;

		/* 用于 profile */
		union {
			void			*prof_tctx_pun;
			prof_tctx_t		*prof_tctx;
		};

		/* small region 的元数据 */
		arena_run_t			run;
	};
};
typedef ph(arena_chunk_map_misc_t) arena_run_heap_t;
#endif /* JEMALLOC_ARENA_STRUCTS_A */

#ifdef JEMALLOC_ARENA_STRUCTS_B
/* Arena chunk header. */
/*
 * chunk 的内存布局:
 *
 *   /-------chunk--------\   \
 *   |    extent_node     |   |
 *   |                    |   |
 *   |      map_bits      |   |
 *   |      ... ...       |    > chunk header
 *   |      map_bits      |   |  
 *   |                    |   |
 *   |      map_misc      |   |
 *   |      ... ...       |   |
 *   |      map_misc      |   |
 *   |                    |   /
 *   |--------------------|              \
 *   |        Page        |   |-> run    |
 *   |--------------------|              |
 *   |        Page        |   \          |
 *   |--------------------|   |          |
 *   |        Page        |    > run     |
 *   |--------------------|   |           > pages grouped as runs 
 *   |        Page        |   /          |  
 *   |--------------------|              |
 *   |        Page        |   \          |
 *   |--------------------|    > run     |
 *   |        Page        |   /          |
 *   |--------------------|              |
 *   ...      ...       ...   ...        |
 *   |                    |   ...        |
 *   \--------------------/              /
 *
 * * 每个 map_bits 对应一个page
 * * 每个 map_misc 对应一个page
 * * chunk 中的 pages 会组成一个个 run，一个 run 由一个或者多个 page 组成
 * * 通过 map_bits 可以直到哪些 pages 组成一个 run，以及 run 的状态
 * * 通过 map_misc 可以将 run 链接起来，组成链表或者堆，从而进行管理
 * * map_bits、map_misc、page 的在 chunk 中的 offset 是有映射关系的，直到其中
 *       任意一个的位置，就可以根据 offset 关系得出其他两个的位置
 */
struct arena_chunk_s {
	/*
	 * node 中记录一些 chunk 的属性信息(如 addr)、管理信息(如 cc_link)
	 */
	extent_node_t		node;

	/*
	 * map_bits 记录着 chunk 中 page 的状态，chunk header空间是不需要记录的
	 * 所以，需要去掉头部空间，记录剩下的页面
	 * 这里 map_bits 只是一个占位符，大小是运行时计算出来的
	 */
	arena_chunk_map_bits_t	map_bits[1]; 
	/*
	 * 在 map_bits 后面是 map_misc 数组，由于 map_bits 大小未定
	 * 所以，map_misc 不好定义
	 * 实际运行时，会在 map_bits 后面留出空间给 map_misc
	 * 并且可以通通果地址偏移读写 map_misc
	 */
};

/*
 * Run 布局如下 :
 *
 *               /--------------------\
 *               | pad?               |
 *               |--------------------|
 *               | redzone            |
 *   reg0_offset | region 0           |
 *               | redzone            |
 *               |--------------------| \
 *               | redzone            | |
 *               | region 1           |  > reg_interval
 *               | redzone            | /
 *               |--------------------|
 *               | ...                |
 *               | ...                |
 *               | ...                |
 *               |--------------------|
 *               | redzone            |
 *               | region nregs-1     |
 *               | redzone            |
 *               |--------------------|
 *               | alignment pad?     |
 *               \--------------------/
 *
 * 实际上，一般情况下 redzone、pad 为0，所以很多时候布局如下：
 *               /--------------------\
 *   reg0_offset | region 0           |
 *               |--------------------| \
 *               | region 1           |  > reg_interval
 *               |--------------------| /
 *               | ...                |
 *               | ...                |
 *               | ...                |
 *               |--------------------|
 *               | region nregs-1     |
 *               \--------------------/
 *
 */
/*
 * small bin 的基本信息，全局共享一份
 */
struct arena_bin_info_s {
	/* region size */
	size_t			reg_size;

	/* Redzone size. */
	size_t			redzone_size;

	/* Interval between regions (reg_size + (redzone_size << 1)). */
	size_t			reg_interval;

	/* 该 run 的总大小，一个 run 由多个 page 组成，可以分成 整数个 region */
	/* 比如，arena_bin_info[3]，reg_size=48, run_size=12288,由3个页组成 */
	size_t			run_size;

	/* run 中 region 个数 */
	uint32_t		nregs;

	/*
	 * bitmap 的基本信息，用于生成 bitmap
	 */
	bitmap_info_t		bitmap_info;

	/* region 0 在run 中的偏移 */
	uint32_t		reg0_offset;
};

/*
 * arena_bin_s 是 arena 用来管理 small bin 的数据结构
 */
struct arena_bin_s {
	/*
	 * 对 runcur,runs,stats 的操作需要该lock
	 */
	malloc_mutex_t		lock;

	/*
	 * runcur : 当前用于分配 bin 的run
	 */
	arena_run_t		*runcur;

	/*
	 * heap of non-full runs
	 * 当 runcur 用完时，需要在 runs 中寻找使用最少的run作为新runcur
	 */
	arena_run_heap_t	runs;

	/* bin 统计数据 */
	malloc_bin_stats_t	stats;
};

/*
 * arena 数据结构
 */
struct arena_s {
	/* 该 arena 在 arena 数组中的 index */
	unsigned		ind;

	/*
	 * 每个线程选择两个 arena,一个用于 application,一个用于 internal metadata 
	 * 这里 nthreads 是统计使用该 arena 的线程数量
	 *   0: Application allocation.
	 *   1: Internal metadata allocation.
	 */
	unsigned		nthreads[2];

	/*
	 * There are three classes of arena operations from a locking
	 * perspective:
	 * 1) Thread assignment (modifies nthreads) is synchronized via atomics.
	 * 2) Bin-related operations are protected by bin locks.
	 * 3) Chunk- and run-related operations are protected by this mutex.
	 */
	malloc_mutex_t		lock;

	arena_stats_t		stats;
	/*
	 * 与该 arena 相关的 tcache
	 */
	ql_head(tcache_t)	tcache_ql;

	uint64_t		prof_accumbytes;

	/*
	 * PRNG state for cache index randomization of large allocation base
	 * pointers.
	 */
	uint64_t		offset_state;

	dss_prec_t		dss_prec;


	/* 正在使用的 chunks */
	ql_head(extent_node_t)	achunks;

	/*
	 * spare 用于暂存刚刚释放的 chunk,以便之后再使用
	 */
	arena_chunk_t		*spare;

	/* Minimum ratio (log base 2) of nactive:ndirty. */
	ssize_t			lg_dirty_mult;

	/* 置为 True，如果正在执行 arena_purge_to_limit(). */
	bool			purging;

	/* 使用中的 runs,huge 的页面数 */
	size_t			nactive;

	/*
	 * 脏页面数量，不使用但是有物理页面映射的页面属于脏页
	 */
	size_t			ndirty;

	/*
	 * chunks_cache,runs_dirty 用于管理不使用的脏内存
	 *
	 *   LRU-----------------------------------------------------------MRU
	 *
	 *        /-- arena ---\
	 *        |            |
	 *        |            |
	 *        |------------|                             /- chunk -\
	 *   ...->|chunks_cache|<--------------------------->|  /----\ |<--...
	 *        |------------|                             |  |node| |
	 *        |            |                             |  |    | |
	 *        |            |    /- run -\    /- run -\   |  |    | |
	 *        |            |    |       |    |       |   |  |    | |
	 *        |            |    |       |    |       |   |  |    | |
	 *        |------------|    |-------|    |-------|   |  |----| |
	 *   ...->|runs_dirty  |<-->|rd     |<-->|rd     |<---->|rd  |<----...
	 *        |------------|    |-------|    |-------|   |  |----| |
	 *        |            |    |       |    |       |   |  |    | |
	 *        |            |    |       |    |       |   |  \----/ |
	 *        |            |    \-------/    \-------/   |         |
	 *        |            |                             |         |
	 *        |            |                             |         |
	 *        \------------/                             \---------/
	 *
	 * * run 的 rd 不在 run 中，在 run 的 map_misc 中
	 * * 如果 chunk 是 简单的chunk， chunk 的 rd 在chunk头部的 extent node 中
	 * * 如果 chunk 是 huge， huge 的 node 是额外分配的，rd在额外分配的node中
	 */
	arena_runs_dirty_link_t	runs_dirty;
	extent_node_t		chunks_cache;

	/*
	 * 以下忽略与 decay 相关的一些参数
	 */
	ssize_t			decay_time;
	nstime_t		decay_interval;
	nstime_t		decay_epoch;
	uint64_t		decay_jitter_state;
	nstime_t		decay_deadline;
	size_t			decay_ndirty;
	size_t			decay_backlog_npages_limit;
	size_t			decay_backlog[SMOOTHSTEP_NSTEPS];

	/* huge 分配的内存 */
	ql_head(extent_node_t)	huge;
	/* Synchronizes all huge allocation/update/deallocation. */
	malloc_mutex_t		huge_mtx;

	/*
	 * 缓存的可以复用的 chunks，均使用 红黑树 管理
	 * szad 表示 size-address-ordered，按照 大小 排序，大小相同，则按照 地址 排序
	 * ad 表示 address-ordered，按照地址排序
	 * cached 表示 地址空间还在，物理地址映射还在
	 * retained 表示 地址空间还在，物理地址映射不在
	 * 所以，使用的时候，cached复用更快，优先级更高
	 */
	extent_tree_t		chunks_szad_cached;
	extent_tree_t		chunks_ad_cached;
	extent_tree_t		chunks_szad_retained;
	extent_tree_t		chunks_ad_retained;

	malloc_mutex_t		chunks_mtx;
	/* 缓存用 base_alloc 分配的 extent node */
	ql_head(extent_node_t)	node_cache;
	malloc_mutex_t		node_cache_mtx;

	/* 用户自定义 chunk 的操作函数 */
	chunk_hooks_t		chunk_hooks;

	/* 管理该 arena 的 bin */
	arena_bin_t		bins[NBINS];

	/*
	 * 管理该 arena 可用的 runs
	 * runs_avail 有多个，运行时会动态创建
	 * runs_avail 的分组是按照某种方式对齐并划分的，目的是为了更容易复用
	 */
	arena_run_heap_t	runs_avail[1]; /* Dynamically sized. */
};

/* Used in conjunction with tsd for fast arena-related context lookup. */
struct arena_tdata_s {
	ticker_t		decay_ticker;
};
#endif /* JEMALLOC_ARENA_STRUCTS_B */

#endif /* JEMALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef JEMALLOC_H_EXTERNS

static const size_t	large_pad =
#ifdef JEMALLOC_CACHE_OBLIVIOUS
    PAGE
#else
    0
#endif
    ;

extern purge_mode_t	opt_purge;
extern const char	*purge_mode_names[];
extern ssize_t		opt_lg_dirty_mult;
extern ssize_t		opt_decay_time;

extern arena_bin_info_t	arena_bin_info[NBINS];

extern size_t		map_bias; /* Number of arena chunk header pages. */
extern size_t		map_misc_offset;
extern size_t		arena_maxrun; /* Max run size for arenas. */
extern size_t		large_maxclass; /* Max large size class. */
extern size_t		run_quantize_max; /* Max run_quantize_*() input. */
extern unsigned		nlclasses; /* Number of large size classes. */
extern unsigned		nhclasses; /* Number of huge size classes. */

#ifdef JEMALLOC_JET
typedef size_t (run_quantize_t)(size_t);
extern run_quantize_t *run_quantize_floor;
extern run_quantize_t *run_quantize_ceil;
#endif
void	arena_chunk_cache_maybe_insert(arena_t *arena, extent_node_t *node,
    bool cache);
void	arena_chunk_cache_maybe_remove(arena_t *arena, extent_node_t *node,
    bool cache);
extent_node_t	*arena_node_alloc(tsdn_t *tsdn, arena_t *arena);
void	arena_node_dalloc(tsdn_t *tsdn, arena_t *arena, extent_node_t *node);
void	*arena_chunk_alloc_huge(tsdn_t *tsdn, arena_t *arena, size_t usize,
    size_t alignment, bool *zero);
void	arena_chunk_dalloc_huge(tsdn_t *tsdn, arena_t *arena, void *chunk,
    size_t usize);
void	arena_chunk_ralloc_huge_similar(tsdn_t *tsdn, arena_t *arena,
    void *chunk, size_t oldsize, size_t usize);
void	arena_chunk_ralloc_huge_shrink(tsdn_t *tsdn, arena_t *arena,
    void *chunk, size_t oldsize, size_t usize);
bool	arena_chunk_ralloc_huge_expand(tsdn_t *tsdn, arena_t *arena,
    void *chunk, size_t oldsize, size_t usize, bool *zero);
ssize_t	arena_lg_dirty_mult_get(tsdn_t *tsdn, arena_t *arena);
bool	arena_lg_dirty_mult_set(tsdn_t *tsdn, arena_t *arena,
    ssize_t lg_dirty_mult);
ssize_t	arena_decay_time_get(tsdn_t *tsdn, arena_t *arena);
bool	arena_decay_time_set(tsdn_t *tsdn, arena_t *arena, ssize_t decay_time);
void	arena_purge(tsdn_t *tsdn, arena_t *arena, bool all);
void	arena_maybe_purge(tsdn_t *tsdn, arena_t *arena);
void	arena_reset(tsd_t *tsd, arena_t *arena);
void	arena_tcache_fill_small(tsdn_t *tsdn, arena_t *arena,
    tcache_bin_t *tbin, szind_t binind, uint64_t prof_accumbytes);
void	arena_alloc_junk_small(void *ptr, arena_bin_info_t *bin_info,
    bool zero);
#ifdef JEMALLOC_JET
typedef void (arena_redzone_corruption_t)(void *, size_t, bool, size_t,
    uint8_t);
extern arena_redzone_corruption_t *arena_redzone_corruption;
typedef void (arena_dalloc_junk_small_t)(void *, arena_bin_info_t *);
extern arena_dalloc_junk_small_t *arena_dalloc_junk_small;
#else
void	arena_dalloc_junk_small(void *ptr, arena_bin_info_t *bin_info);
#endif
void	arena_quarantine_junk_small(void *ptr, size_t usize);
void	*arena_malloc_large(tsdn_t *tsdn, arena_t *arena, szind_t ind,
    bool zero);
void	*arena_malloc_hard(tsdn_t *tsdn, arena_t *arena, size_t size,
    szind_t ind, bool zero);
void	*arena_palloc(tsdn_t *tsdn, arena_t *arena, size_t usize,
    size_t alignment, bool zero, tcache_t *tcache);
void	arena_prof_promoted(tsdn_t *tsdn, const void *ptr, size_t size);
void	arena_dalloc_bin_junked_locked(tsdn_t *tsdn, arena_t *arena,
    arena_chunk_t *chunk, void *ptr, arena_chunk_map_bits_t *bitselm);
void	arena_dalloc_bin(tsdn_t *tsdn, arena_t *arena, arena_chunk_t *chunk,
    void *ptr, size_t pageind, arena_chunk_map_bits_t *bitselm);
void	arena_dalloc_small(tsdn_t *tsdn, arena_t *arena, arena_chunk_t *chunk,
    void *ptr, size_t pageind);
#ifdef JEMALLOC_JET
typedef void (arena_dalloc_junk_large_t)(void *, size_t);
extern arena_dalloc_junk_large_t *arena_dalloc_junk_large;
#else
void	arena_dalloc_junk_large(void *ptr, size_t usize);
#endif
void	arena_dalloc_large_junked_locked(tsdn_t *tsdn, arena_t *arena,
    arena_chunk_t *chunk, void *ptr);
void	arena_dalloc_large(tsdn_t *tsdn, arena_t *arena, arena_chunk_t *chunk,
    void *ptr);
#ifdef JEMALLOC_JET
typedef void (arena_ralloc_junk_large_t)(void *, size_t, size_t);
extern arena_ralloc_junk_large_t *arena_ralloc_junk_large;
#endif
bool	arena_ralloc_no_move(tsdn_t *tsdn, void *ptr, size_t oldsize,
    size_t size, size_t extra, bool zero);
void	*arena_ralloc(tsd_t *tsd, arena_t *arena, void *ptr, size_t oldsize,
    size_t size, size_t alignment, bool zero, tcache_t *tcache);
dss_prec_t	arena_dss_prec_get(tsdn_t *tsdn, arena_t *arena);
bool	arena_dss_prec_set(tsdn_t *tsdn, arena_t *arena, dss_prec_t dss_prec);
ssize_t	arena_lg_dirty_mult_default_get(void);
bool	arena_lg_dirty_mult_default_set(ssize_t lg_dirty_mult);
ssize_t	arena_decay_time_default_get(void);
bool	arena_decay_time_default_set(ssize_t decay_time);
void	arena_basic_stats_merge(tsdn_t *tsdn, arena_t *arena,
    unsigned *nthreads, const char **dss, ssize_t *lg_dirty_mult,
    ssize_t *decay_time, size_t *nactive, size_t *ndirty);
void	arena_stats_merge(tsdn_t *tsdn, arena_t *arena, unsigned *nthreads,
    const char **dss, ssize_t *lg_dirty_mult, ssize_t *decay_time,
    size_t *nactive, size_t *ndirty, arena_stats_t *astats,
    malloc_bin_stats_t *bstats, malloc_large_stats_t *lstats,
    malloc_huge_stats_t *hstats);
unsigned	arena_nthreads_get(arena_t *arena, bool internal);
void	arena_nthreads_inc(arena_t *arena, bool internal);
void	arena_nthreads_dec(arena_t *arena, bool internal);
arena_t	*arena_new(tsdn_t *tsdn, unsigned ind);
bool	arena_boot(void);
void	arena_prefork0(tsdn_t *tsdn, arena_t *arena);
void	arena_prefork1(tsdn_t *tsdn, arena_t *arena);
void	arena_prefork2(tsdn_t *tsdn, arena_t *arena);
void	arena_prefork3(tsdn_t *tsdn, arena_t *arena);
void	arena_postfork_parent(tsdn_t *tsdn, arena_t *arena);
void	arena_postfork_child(tsdn_t *tsdn, arena_t *arena);

#endif /* JEMALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef JEMALLOC_H_INLINES

#ifndef JEMALLOC_ENABLE_INLINE
arena_chunk_map_bits_t	*arena_bitselm_get_mutable(arena_chunk_t *chunk,
    size_t pageind);
const arena_chunk_map_bits_t	*arena_bitselm_get_const(
    const arena_chunk_t *chunk, size_t pageind);
arena_chunk_map_misc_t	*arena_miscelm_get_mutable(arena_chunk_t *chunk,
    size_t pageind);
const arena_chunk_map_misc_t	*arena_miscelm_get_const(
    const arena_chunk_t *chunk, size_t pageind);
size_t	arena_miscelm_to_pageind(const arena_chunk_map_misc_t *miscelm);
void	*arena_miscelm_to_rpages(const arena_chunk_map_misc_t *miscelm);
arena_chunk_map_misc_t	*arena_rd_to_miscelm(arena_runs_dirty_link_t *rd);
arena_chunk_map_misc_t	*arena_run_to_miscelm(arena_run_t *run);
size_t	*arena_mapbitsp_get_mutable(arena_chunk_t *chunk, size_t pageind);
const size_t	*arena_mapbitsp_get_const(const arena_chunk_t *chunk,
    size_t pageind);
size_t	arena_mapbitsp_read(const size_t *mapbitsp);
size_t	arena_mapbits_get(const arena_chunk_t *chunk, size_t pageind);
size_t	arena_mapbits_size_decode(size_t mapbits);
size_t	arena_mapbits_unallocated_size_get(const arena_chunk_t *chunk,
    size_t pageind);
size_t	arena_mapbits_large_size_get(const arena_chunk_t *chunk,
    size_t pageind);
size_t	arena_mapbits_small_runind_get(const arena_chunk_t *chunk,
    size_t pageind);
szind_t	arena_mapbits_binind_get(const arena_chunk_t *chunk, size_t pageind);
size_t	arena_mapbits_dirty_get(const arena_chunk_t *chunk, size_t pageind);
size_t	arena_mapbits_unzeroed_get(const arena_chunk_t *chunk, size_t pageind);
size_t	arena_mapbits_decommitted_get(const arena_chunk_t *chunk,
    size_t pageind);
size_t	arena_mapbits_large_get(const arena_chunk_t *chunk, size_t pageind);
size_t	arena_mapbits_allocated_get(const arena_chunk_t *chunk, size_t pageind);
void	arena_mapbitsp_write(size_t *mapbitsp, size_t mapbits);
size_t	arena_mapbits_size_encode(size_t size);
void	arena_mapbits_unallocated_set(arena_chunk_t *chunk, size_t pageind,
    size_t size, size_t flags);
void	arena_mapbits_unallocated_size_set(arena_chunk_t *chunk, size_t pageind,
    size_t size);
void	arena_mapbits_internal_set(arena_chunk_t *chunk, size_t pageind,
    size_t flags);
void	arena_mapbits_large_set(arena_chunk_t *chunk, size_t pageind,
    size_t size, size_t flags);
void	arena_mapbits_large_binind_set(arena_chunk_t *chunk, size_t pageind,
    szind_t binind);
void	arena_mapbits_small_set(arena_chunk_t *chunk, size_t pageind,
    size_t runind, szind_t binind, size_t flags);
void	arena_metadata_allocated_add(arena_t *arena, size_t size);
void	arena_metadata_allocated_sub(arena_t *arena, size_t size);
size_t	arena_metadata_allocated_get(arena_t *arena);
bool	arena_prof_accum_impl(arena_t *arena, uint64_t accumbytes);
bool	arena_prof_accum_locked(arena_t *arena, uint64_t accumbytes);
bool	arena_prof_accum(tsdn_t *tsdn, arena_t *arena, uint64_t accumbytes);
szind_t	arena_ptr_small_binind_get(const void *ptr, size_t mapbits);
szind_t	arena_bin_index(arena_t *arena, arena_bin_t *bin);
size_t	arena_run_regind(arena_run_t *run, arena_bin_info_t *bin_info,
    const void *ptr);
prof_tctx_t	*arena_prof_tctx_get(tsdn_t *tsdn, const void *ptr);
void	arena_prof_tctx_set(tsdn_t *tsdn, const void *ptr, size_t usize,
    prof_tctx_t *tctx);
void	arena_prof_tctx_reset(tsdn_t *tsdn, const void *ptr, size_t usize,
    const void *old_ptr, prof_tctx_t *old_tctx);
void	arena_decay_ticks(tsdn_t *tsdn, arena_t *arena, unsigned nticks);
void	arena_decay_tick(tsdn_t *tsdn, arena_t *arena);
void	*arena_malloc(tsdn_t *tsdn, arena_t *arena, size_t size, szind_t ind,
    bool zero, tcache_t *tcache, bool slow_path);
arena_t	*arena_aalloc(const void *ptr);
size_t	arena_salloc(tsdn_t *tsdn, const void *ptr, bool demote);
void	arena_dalloc(tsdn_t *tsdn, void *ptr, tcache_t *tcache, bool slow_path);
void	arena_sdalloc(tsdn_t *tsdn, void *ptr, size_t size, tcache_t *tcache,
    bool slow_path);
#endif

#if (defined(JEMALLOC_ENABLE_INLINE) || defined(JEMALLOC_ARENA_C_))
#  ifdef JEMALLOC_ARENA_INLINE_A
JEMALLOC_ALWAYS_INLINE arena_chunk_map_bits_t *
arena_bitselm_get_mutable(arena_chunk_t *chunk, size_t pageind)
{

	assert(pageind >= map_bias);
	assert(pageind < chunk_npages);

	return (&chunk->map_bits[pageind-map_bias]);
}

JEMALLOC_ALWAYS_INLINE const arena_chunk_map_bits_t *
arena_bitselm_get_const(const arena_chunk_t *chunk, size_t pageind)
{

	return (arena_bitselm_get_mutable((arena_chunk_t *)chunk, pageind));
}

JEMALLOC_ALWAYS_INLINE arena_chunk_map_misc_t *
arena_miscelm_get_mutable(arena_chunk_t *chunk, size_t pageind)
{

	assert(pageind >= map_bias);
	assert(pageind < chunk_npages);

	return ((arena_chunk_map_misc_t *)((uintptr_t)chunk +
	    (uintptr_t)map_misc_offset) + pageind-map_bias);
}

JEMALLOC_ALWAYS_INLINE const arena_chunk_map_misc_t *
arena_miscelm_get_const(const arena_chunk_t *chunk, size_t pageind)
{

	return (arena_miscelm_get_mutable((arena_chunk_t *)chunk, pageind));
}

JEMALLOC_ALWAYS_INLINE size_t
arena_miscelm_to_pageind(const arena_chunk_map_misc_t *miscelm)
{
	arena_chunk_t *chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(miscelm);
	size_t pageind = ((uintptr_t)miscelm - ((uintptr_t)chunk +
	    map_misc_offset)) / sizeof(arena_chunk_map_misc_t) + map_bias;

	assert(pageind >= map_bias);
	assert(pageind < chunk_npages);

	return (pageind);
}

JEMALLOC_ALWAYS_INLINE void *
arena_miscelm_to_rpages(const arena_chunk_map_misc_t *miscelm)
{
	arena_chunk_t *chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(miscelm);
	size_t pageind = arena_miscelm_to_pageind(miscelm);

	return ((void *)((uintptr_t)chunk + (pageind << LG_PAGE)));
}

JEMALLOC_ALWAYS_INLINE arena_chunk_map_misc_t *
arena_rd_to_miscelm(arena_runs_dirty_link_t *rd)
{
	arena_chunk_map_misc_t *miscelm = (arena_chunk_map_misc_t
	    *)((uintptr_t)rd - offsetof(arena_chunk_map_misc_t, rd));

	assert(arena_miscelm_to_pageind(miscelm) >= map_bias);
	assert(arena_miscelm_to_pageind(miscelm) < chunk_npages);

	return (miscelm);
}

JEMALLOC_ALWAYS_INLINE arena_chunk_map_misc_t *
arena_run_to_miscelm(arena_run_t *run)
{
	arena_chunk_map_misc_t *miscelm = (arena_chunk_map_misc_t
	    *)((uintptr_t)run - offsetof(arena_chunk_map_misc_t, run));

	assert(arena_miscelm_to_pageind(miscelm) >= map_bias);
	assert(arena_miscelm_to_pageind(miscelm) < chunk_npages);

	return (miscelm);
}

JEMALLOC_ALWAYS_INLINE size_t *
arena_mapbitsp_get_mutable(arena_chunk_t *chunk, size_t pageind)
{

	return (&arena_bitselm_get_mutable(chunk, pageind)->bits);
}

JEMALLOC_ALWAYS_INLINE const size_t *
arena_mapbitsp_get_const(const arena_chunk_t *chunk, size_t pageind)
{

	return (arena_mapbitsp_get_mutable((arena_chunk_t *)chunk, pageind));
}

JEMALLOC_ALWAYS_INLINE size_t
arena_mapbitsp_read(const size_t *mapbitsp)
{

	return (*mapbitsp);
}

JEMALLOC_ALWAYS_INLINE size_t
arena_mapbits_get(const arena_chunk_t *chunk, size_t pageind)
{

	return (arena_mapbitsp_read(arena_mapbitsp_get_const(chunk, pageind)));
}

JEMALLOC_ALWAYS_INLINE size_t
arena_mapbits_size_decode(size_t mapbits)
{
	size_t size;

#if CHUNK_MAP_SIZE_SHIFT > 0
	size = (mapbits & CHUNK_MAP_SIZE_MASK) >> CHUNK_MAP_SIZE_SHIFT;
#elif CHUNK_MAP_SIZE_SHIFT == 0
	size = mapbits & CHUNK_MAP_SIZE_MASK;
#else
	size = (mapbits & CHUNK_MAP_SIZE_MASK) << -CHUNK_MAP_SIZE_SHIFT;
#endif

	return (size);
}

JEMALLOC_ALWAYS_INLINE size_t
arena_mapbits_unallocated_size_get(const arena_chunk_t *chunk, size_t pageind)
{
	size_t mapbits;

	mapbits = arena_mapbits_get(chunk, pageind);
	assert((mapbits & (CHUNK_MAP_LARGE|CHUNK_MAP_ALLOCATED)) == 0);
	return (arena_mapbits_size_decode(mapbits));
}

JEMALLOC_ALWAYS_INLINE size_t
arena_mapbits_large_size_get(const arena_chunk_t *chunk, size_t pageind)
{
	size_t mapbits;

	mapbits = arena_mapbits_get(chunk, pageind);
	assert((mapbits & (CHUNK_MAP_LARGE|CHUNK_MAP_ALLOCATED)) ==
	    (CHUNK_MAP_LARGE|CHUNK_MAP_ALLOCATED));
	return (arena_mapbits_size_decode(mapbits));
}

JEMALLOC_ALWAYS_INLINE size_t
arena_mapbits_small_runind_get(const arena_chunk_t *chunk, size_t pageind)
{
	size_t mapbits;

	mapbits = arena_mapbits_get(chunk, pageind);
	assert((mapbits & (CHUNK_MAP_LARGE|CHUNK_MAP_ALLOCATED)) ==
	    CHUNK_MAP_ALLOCATED);
	return (mapbits >> CHUNK_MAP_RUNIND_SHIFT);
}

JEMALLOC_ALWAYS_INLINE szind_t
arena_mapbits_binind_get(const arena_chunk_t *chunk, size_t pageind)
{
	size_t mapbits;
	szind_t binind;

	mapbits = arena_mapbits_get(chunk, pageind);
	binind = (mapbits & CHUNK_MAP_BININD_MASK) >> CHUNK_MAP_BININD_SHIFT;
	assert(binind < NBINS || binind == BININD_INVALID);
	return (binind);
}

JEMALLOC_ALWAYS_INLINE size_t
arena_mapbits_dirty_get(const arena_chunk_t *chunk, size_t pageind)
{
	size_t mapbits;

	mapbits = arena_mapbits_get(chunk, pageind);
	assert((mapbits & CHUNK_MAP_DECOMMITTED) == 0 || (mapbits &
	    (CHUNK_MAP_DIRTY|CHUNK_MAP_UNZEROED)) == 0);
	return (mapbits & CHUNK_MAP_DIRTY);
}

JEMALLOC_ALWAYS_INLINE size_t
arena_mapbits_unzeroed_get(const arena_chunk_t *chunk, size_t pageind)
{
	size_t mapbits;

	mapbits = arena_mapbits_get(chunk, pageind);
	assert((mapbits & CHUNK_MAP_DECOMMITTED) == 0 || (mapbits &
	    (CHUNK_MAP_DIRTY|CHUNK_MAP_UNZEROED)) == 0);
	return (mapbits & CHUNK_MAP_UNZEROED);
}

JEMALLOC_ALWAYS_INLINE size_t
arena_mapbits_decommitted_get(const arena_chunk_t *chunk, size_t pageind)
{
	size_t mapbits;

	mapbits = arena_mapbits_get(chunk, pageind);
	assert((mapbits & CHUNK_MAP_DECOMMITTED) == 0 || (mapbits &
	    (CHUNK_MAP_DIRTY|CHUNK_MAP_UNZEROED)) == 0);
	return (mapbits & CHUNK_MAP_DECOMMITTED);
}

JEMALLOC_ALWAYS_INLINE size_t
arena_mapbits_large_get(const arena_chunk_t *chunk, size_t pageind)
{
	size_t mapbits;

	mapbits = arena_mapbits_get(chunk, pageind);
	return (mapbits & CHUNK_MAP_LARGE);
}

JEMALLOC_ALWAYS_INLINE size_t
arena_mapbits_allocated_get(const arena_chunk_t *chunk, size_t pageind)
{
	size_t mapbits;

	mapbits = arena_mapbits_get(chunk, pageind);
	return (mapbits & CHUNK_MAP_ALLOCATED);
}

JEMALLOC_ALWAYS_INLINE void
arena_mapbitsp_write(size_t *mapbitsp, size_t mapbits)
{

	*mapbitsp = mapbits;
}

JEMALLOC_ALWAYS_INLINE size_t
arena_mapbits_size_encode(size_t size)
{
	size_t mapbits;

#if CHUNK_MAP_SIZE_SHIFT > 0
	mapbits = size << CHUNK_MAP_SIZE_SHIFT;
#elif CHUNK_MAP_SIZE_SHIFT == 0
	mapbits = size;
#else
	mapbits = size >> -CHUNK_MAP_SIZE_SHIFT;
#endif

	assert((mapbits & ~CHUNK_MAP_SIZE_MASK) == 0);
	return (mapbits);
}

JEMALLOC_ALWAYS_INLINE void
arena_mapbits_unallocated_set(arena_chunk_t *chunk, size_t pageind, size_t size,
    size_t flags)
{
	size_t *mapbitsp = arena_mapbitsp_get_mutable(chunk, pageind);

	assert((size & PAGE_MASK) == 0);
	assert((flags & CHUNK_MAP_FLAGS_MASK) == flags);
	assert((flags & CHUNK_MAP_DECOMMITTED) == 0 || (flags &
	    (CHUNK_MAP_DIRTY|CHUNK_MAP_UNZEROED)) == 0);
	arena_mapbitsp_write(mapbitsp, arena_mapbits_size_encode(size) |
	    CHUNK_MAP_BININD_INVALID | flags);
}

JEMALLOC_ALWAYS_INLINE void
arena_mapbits_unallocated_size_set(arena_chunk_t *chunk, size_t pageind,
    size_t size)
{
	size_t *mapbitsp = arena_mapbitsp_get_mutable(chunk, pageind);
	size_t mapbits = arena_mapbitsp_read(mapbitsp);

	assert((size & PAGE_MASK) == 0);
	assert((mapbits & (CHUNK_MAP_LARGE|CHUNK_MAP_ALLOCATED)) == 0);
	arena_mapbitsp_write(mapbitsp, arena_mapbits_size_encode(size) |
	    (mapbits & ~CHUNK_MAP_SIZE_MASK));
}

JEMALLOC_ALWAYS_INLINE void
arena_mapbits_internal_set(arena_chunk_t *chunk, size_t pageind, size_t flags)
{
	size_t *mapbitsp = arena_mapbitsp_get_mutable(chunk, pageind);

	assert((flags & CHUNK_MAP_UNZEROED) == flags);
	arena_mapbitsp_write(mapbitsp, flags);
}

JEMALLOC_ALWAYS_INLINE void
arena_mapbits_large_set(arena_chunk_t *chunk, size_t pageind, size_t size,
    size_t flags)
{
	size_t *mapbitsp = arena_mapbitsp_get_mutable(chunk, pageind);

	assert((size & PAGE_MASK) == 0);
	assert((flags & CHUNK_MAP_FLAGS_MASK) == flags);
	assert((flags & CHUNK_MAP_DECOMMITTED) == 0 || (flags &
	    (CHUNK_MAP_DIRTY|CHUNK_MAP_UNZEROED)) == 0);
	arena_mapbitsp_write(mapbitsp, arena_mapbits_size_encode(size) |
	    CHUNK_MAP_BININD_INVALID | flags | CHUNK_MAP_LARGE |
	    CHUNK_MAP_ALLOCATED);
}

JEMALLOC_ALWAYS_INLINE void
arena_mapbits_large_binind_set(arena_chunk_t *chunk, size_t pageind,
    szind_t binind)
{
	size_t *mapbitsp = arena_mapbitsp_get_mutable(chunk, pageind);
	size_t mapbits = arena_mapbitsp_read(mapbitsp);

	assert(binind <= BININD_INVALID);
	assert(arena_mapbits_large_size_get(chunk, pageind) == LARGE_MINCLASS +
	    large_pad);
	arena_mapbitsp_write(mapbitsp, (mapbits & ~CHUNK_MAP_BININD_MASK) |
	    (binind << CHUNK_MAP_BININD_SHIFT));
}

JEMALLOC_ALWAYS_INLINE void
arena_mapbits_small_set(arena_chunk_t *chunk, size_t pageind, size_t runind,
    szind_t binind, size_t flags)
{
	size_t *mapbitsp = arena_mapbitsp_get_mutable(chunk, pageind);

	assert(binind < BININD_INVALID);
	assert(pageind - runind >= map_bias);
	assert((flags & CHUNK_MAP_UNZEROED) == flags);
	arena_mapbitsp_write(mapbitsp, (runind << CHUNK_MAP_RUNIND_SHIFT) |
	    (binind << CHUNK_MAP_BININD_SHIFT) | flags | CHUNK_MAP_ALLOCATED);
}

JEMALLOC_INLINE void
arena_metadata_allocated_add(arena_t *arena, size_t size)
{

	atomic_add_z(&arena->stats.metadata_allocated, size);
}

JEMALLOC_INLINE void
arena_metadata_allocated_sub(arena_t *arena, size_t size)
{

	atomic_sub_z(&arena->stats.metadata_allocated, size);
}

JEMALLOC_INLINE size_t
arena_metadata_allocated_get(arena_t *arena)
{

	return (atomic_read_z(&arena->stats.metadata_allocated));
}

JEMALLOC_INLINE bool
arena_prof_accum_impl(arena_t *arena, uint64_t accumbytes)
{

	cassert(config_prof);
	assert(prof_interval != 0);

	arena->prof_accumbytes += accumbytes;
	if (arena->prof_accumbytes >= prof_interval) {
		arena->prof_accumbytes -= prof_interval;
		return (true);
	}
	return (false);
}

JEMALLOC_INLINE bool
arena_prof_accum_locked(arena_t *arena, uint64_t accumbytes)
{

	cassert(config_prof);

	if (likely(prof_interval == 0))
		return (false);
	return (arena_prof_accum_impl(arena, accumbytes));
}

JEMALLOC_INLINE bool
arena_prof_accum(tsdn_t *tsdn, arena_t *arena, uint64_t accumbytes)
{

	cassert(config_prof);

	if (likely(prof_interval == 0))
		return (false);

	{
		bool ret;

		malloc_mutex_lock(tsdn, &arena->lock);
		ret = arena_prof_accum_impl(arena, accumbytes);
		malloc_mutex_unlock(tsdn, &arena->lock);
		return (ret);
	}
}

JEMALLOC_ALWAYS_INLINE szind_t
arena_ptr_small_binind_get(const void *ptr, size_t mapbits)
{
	szind_t binind;

	binind = (mapbits & CHUNK_MAP_BININD_MASK) >> CHUNK_MAP_BININD_SHIFT;

	if (config_debug) {
		arena_chunk_t *chunk;
		arena_t *arena;
		size_t pageind;
		size_t actual_mapbits;
		size_t rpages_ind;
		const arena_run_t *run;
		arena_bin_t *bin;
		szind_t run_binind, actual_binind;
		arena_bin_info_t *bin_info;
		const arena_chunk_map_misc_t *miscelm;
		const void *rpages;

		assert(binind != BININD_INVALID);
		assert(binind < NBINS);
		chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(ptr);
		arena = extent_node_arena_get(&chunk->node);
		pageind = ((uintptr_t)ptr - (uintptr_t)chunk) >> LG_PAGE;
		actual_mapbits = arena_mapbits_get(chunk, pageind);
		assert(mapbits == actual_mapbits);
		assert(arena_mapbits_large_get(chunk, pageind) == 0);
		assert(arena_mapbits_allocated_get(chunk, pageind) != 0);
		rpages_ind = pageind - arena_mapbits_small_runind_get(chunk,
		    pageind);
		miscelm = arena_miscelm_get_const(chunk, rpages_ind);
		run = &miscelm->run;
		run_binind = run->binind;
		bin = &arena->bins[run_binind];
		actual_binind = (szind_t)(bin - arena->bins);
		assert(run_binind == actual_binind);
		bin_info = &arena_bin_info[actual_binind];
		rpages = arena_miscelm_to_rpages(miscelm);
		assert(((uintptr_t)ptr - ((uintptr_t)rpages +
		    (uintptr_t)bin_info->reg0_offset)) % bin_info->reg_interval
		    == 0);
	}

	return (binind);
}
#  endif /* JEMALLOC_ARENA_INLINE_A */

#  ifdef JEMALLOC_ARENA_INLINE_B
JEMALLOC_INLINE szind_t
arena_bin_index(arena_t *arena, arena_bin_t *bin)
{
	szind_t binind = (szind_t)(bin - arena->bins);
	assert(binind < NBINS);
	return (binind);
}

JEMALLOC_INLINE size_t
arena_run_regind(arena_run_t *run, arena_bin_info_t *bin_info, const void *ptr)
{
	size_t diff, interval, shift, regind;
	arena_chunk_map_misc_t *miscelm = arena_run_to_miscelm(run);
	void *rpages = arena_miscelm_to_rpages(miscelm);

	/*
	 * Freeing a pointer lower than region zero can cause assertion
	 * failure.
	 */
	assert((uintptr_t)ptr >= (uintptr_t)rpages +
	    (uintptr_t)bin_info->reg0_offset);

	/*
	 * Avoid doing division with a variable divisor if possible.  Using
	 * actual division here can reduce allocator throughput by over 20%!
	 */
	diff = (size_t)((uintptr_t)ptr - (uintptr_t)rpages -
	    bin_info->reg0_offset);

	/* Rescale (factor powers of 2 out of the numerator and denominator). */
	interval = bin_info->reg_interval;
	shift = ffs_zu(interval) - 1;
	diff >>= shift;
	interval >>= shift;

	if (interval == 1) {
		/* The divisor was a power of 2. */
		regind = diff;
	} else {
		/*
		 * To divide by a number D that is not a power of two we
		 * multiply by (2^21 / D) and then right shift by 21 positions.
		 *
		 *   X / D
		 *
		 * becomes
		 *
		 *   (X * interval_invs[D - 3]) >> SIZE_INV_SHIFT
		 *
		 * We can omit the first three elements, because we never
		 * divide by 0, and 1 and 2 are both powers of two, which are
		 * handled above.
		 */
#define	SIZE_INV_SHIFT	((sizeof(size_t) << 3) - LG_RUN_MAXREGS)
#define	SIZE_INV(s)	(((ZU(1) << SIZE_INV_SHIFT) / (s)) + 1)
		static const size_t interval_invs[] = {
		    SIZE_INV(3),
		    SIZE_INV(4), SIZE_INV(5), SIZE_INV(6), SIZE_INV(7),
		    SIZE_INV(8), SIZE_INV(9), SIZE_INV(10), SIZE_INV(11),
		    SIZE_INV(12), SIZE_INV(13), SIZE_INV(14), SIZE_INV(15),
		    SIZE_INV(16), SIZE_INV(17), SIZE_INV(18), SIZE_INV(19),
		    SIZE_INV(20), SIZE_INV(21), SIZE_INV(22), SIZE_INV(23),
		    SIZE_INV(24), SIZE_INV(25), SIZE_INV(26), SIZE_INV(27),
		    SIZE_INV(28), SIZE_INV(29), SIZE_INV(30), SIZE_INV(31)
		};

		if (likely(interval <= ((sizeof(interval_invs) / sizeof(size_t))
		    + 2))) {
			regind = (diff * interval_invs[interval - 3]) >>
			    SIZE_INV_SHIFT;
		} else
			regind = diff / interval;
#undef SIZE_INV
#undef SIZE_INV_SHIFT
	}
	assert(diff == regind * interval);
	assert(regind < bin_info->nregs);

	return (regind);
}

JEMALLOC_INLINE prof_tctx_t *
arena_prof_tctx_get(tsdn_t *tsdn, const void *ptr)
{
	prof_tctx_t *ret;
	arena_chunk_t *chunk;

	cassert(config_prof);
	assert(ptr != NULL);

	chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(ptr);
	if (likely(chunk != ptr)) {
		size_t pageind = ((uintptr_t)ptr - (uintptr_t)chunk) >> LG_PAGE;
		size_t mapbits = arena_mapbits_get(chunk, pageind);
		assert((mapbits & CHUNK_MAP_ALLOCATED) != 0);
		if (likely((mapbits & CHUNK_MAP_LARGE) == 0))
			ret = (prof_tctx_t *)(uintptr_t)1U;
		else {
			arena_chunk_map_misc_t *elm =
			    arena_miscelm_get_mutable(chunk, pageind);
			ret = atomic_read_p(&elm->prof_tctx_pun);
		}
	} else
		ret = huge_prof_tctx_get(tsdn, ptr);

	return (ret);
}

JEMALLOC_INLINE void
arena_prof_tctx_set(tsdn_t *tsdn, const void *ptr, size_t usize,
    prof_tctx_t *tctx)
{
	arena_chunk_t *chunk;

	cassert(config_prof);
	assert(ptr != NULL);

	chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(ptr);
	if (likely(chunk != ptr)) {
		size_t pageind = ((uintptr_t)ptr - (uintptr_t)chunk) >> LG_PAGE;

		assert(arena_mapbits_allocated_get(chunk, pageind) != 0);

		if (unlikely(usize > SMALL_MAXCLASS || (uintptr_t)tctx >
		    (uintptr_t)1U)) {
			arena_chunk_map_misc_t *elm;

			assert(arena_mapbits_large_get(chunk, pageind) != 0);

			elm = arena_miscelm_get_mutable(chunk, pageind);
			atomic_write_p(&elm->prof_tctx_pun, tctx);
		} else {
			/*
			 * tctx must always be initialized for large runs.
			 * Assert that the surrounding conditional logic is
			 * equivalent to checking whether ptr refers to a large
			 * run.
			 */
			assert(arena_mapbits_large_get(chunk, pageind) == 0);
		}
	} else
		huge_prof_tctx_set(tsdn, ptr, tctx);
}

JEMALLOC_INLINE void
arena_prof_tctx_reset(tsdn_t *tsdn, const void *ptr, size_t usize,
    const void *old_ptr, prof_tctx_t *old_tctx)
{

	cassert(config_prof);
	assert(ptr != NULL);

	if (unlikely(usize > SMALL_MAXCLASS || (ptr == old_ptr &&
	    (uintptr_t)old_tctx > (uintptr_t)1U))) {
		arena_chunk_t *chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(ptr);
		if (likely(chunk != ptr)) {
			size_t pageind;
			arena_chunk_map_misc_t *elm;

			pageind = ((uintptr_t)ptr - (uintptr_t)chunk) >>
			    LG_PAGE;
			assert(arena_mapbits_allocated_get(chunk, pageind) !=
			    0);
			assert(arena_mapbits_large_get(chunk, pageind) != 0);

			elm = arena_miscelm_get_mutable(chunk, pageind);
			atomic_write_p(&elm->prof_tctx_pun,
			    (prof_tctx_t *)(uintptr_t)1U);
		} else
			huge_prof_tctx_reset(tsdn, ptr);
	}
}

JEMALLOC_ALWAYS_INLINE void
arena_decay_ticks(tsdn_t *tsdn, arena_t *arena, unsigned nticks)
{
	tsd_t *tsd;
	ticker_t *decay_ticker;

	if (unlikely(tsdn_null(tsdn)))
		return;
	tsd = tsdn_tsd(tsdn);
	decay_ticker = decay_ticker_get(tsd, arena->ind);
	if (unlikely(decay_ticker == NULL))
		return;
	if (unlikely(ticker_ticks(decay_ticker, nticks)))
		arena_purge(tsdn, arena, false);
}

JEMALLOC_ALWAYS_INLINE void
arena_decay_tick(tsdn_t *tsdn, arena_t *arena)
{

	arena_decay_ticks(tsdn, arena, 1);
}

JEMALLOC_ALWAYS_INLINE void *
arena_malloc(tsdn_t *tsdn, arena_t *arena, size_t size, szind_t ind, bool zero,
    tcache_t *tcache, bool slow_path)
{

	assert(!tsdn_null(tsdn) || tcache == NULL);
	assert(size != 0);

	if (likely(tcache != NULL)) {
		if (likely(size <= SMALL_MAXCLASS)) {
			return (tcache_alloc_small(tsdn_tsd(tsdn), arena,
			    tcache, size, ind, zero, slow_path));
		}
		if (likely(size <= tcache_maxclass)) {
			return (tcache_alloc_large(tsdn_tsd(tsdn), arena,
			    tcache, size, ind, zero, slow_path));
		}
		/* (size > tcache_maxclass) case falls through. */
		assert(size > tcache_maxclass);
	}

	return (arena_malloc_hard(tsdn, arena, size, ind, zero));
}

JEMALLOC_ALWAYS_INLINE arena_t *
arena_aalloc(const void *ptr)
{
	arena_chunk_t *chunk;

	chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(ptr);
	if (likely(chunk != ptr))
		return (extent_node_arena_get(&chunk->node));
	else
		return (huge_aalloc(ptr));
}

/* Return the size of the allocation pointed to by ptr. */
JEMALLOC_ALWAYS_INLINE size_t
arena_salloc(tsdn_t *tsdn, const void *ptr, bool demote)
{
	size_t ret;
	arena_chunk_t *chunk;
	size_t pageind;
	szind_t binind;

	assert(ptr != NULL);

	chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(ptr);
	if (likely(chunk != ptr)) {
		pageind = ((uintptr_t)ptr - (uintptr_t)chunk) >> LG_PAGE;
		assert(arena_mapbits_allocated_get(chunk, pageind) != 0);
		binind = arena_mapbits_binind_get(chunk, pageind);
		if (unlikely(binind == BININD_INVALID || (config_prof && !demote
		    && arena_mapbits_large_get(chunk, pageind) != 0))) {
			/*
			 * Large allocation.  In the common case (demote), and
			 * as this is an inline function, most callers will only
			 * end up looking at binind to determine that ptr is a
			 * small allocation.
			 */
			assert(config_cache_oblivious || ((uintptr_t)ptr &
			    PAGE_MASK) == 0);
			ret = arena_mapbits_large_size_get(chunk, pageind) -
			    large_pad;
			assert(ret != 0);
			assert(pageind + ((ret+large_pad)>>LG_PAGE) <=
			    chunk_npages);
			assert(arena_mapbits_dirty_get(chunk, pageind) ==
			    arena_mapbits_dirty_get(chunk,
			    pageind+((ret+large_pad)>>LG_PAGE)-1));
		} else {
			/*
			 * Small allocation (possibly promoted to a large
			 * object).
			 */
			assert(arena_mapbits_large_get(chunk, pageind) != 0 ||
			    arena_ptr_small_binind_get(ptr,
			    arena_mapbits_get(chunk, pageind)) == binind);
			ret = index2size(binind);
		}
	} else
		ret = huge_salloc(tsdn, ptr);

	return (ret);
}

JEMALLOC_ALWAYS_INLINE void
arena_dalloc(tsdn_t *tsdn, void *ptr, tcache_t *tcache, bool slow_path)
{
	arena_chunk_t *chunk;
	size_t pageind, mapbits;

	assert(!tsdn_null(tsdn) || tcache == NULL);
	assert(ptr != NULL);

	chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(ptr);
	if (likely(chunk != ptr)) {
		pageind = ((uintptr_t)ptr - (uintptr_t)chunk) >> LG_PAGE;
		mapbits = arena_mapbits_get(chunk, pageind);
		assert(arena_mapbits_allocated_get(chunk, pageind) != 0);
		if (likely((mapbits & CHUNK_MAP_LARGE) == 0)) {
			/* Small allocation. */
			if (likely(tcache != NULL)) {
				szind_t binind = arena_ptr_small_binind_get(ptr,
				    mapbits);
				tcache_dalloc_small(tsdn_tsd(tsdn), tcache, ptr,
				    binind, slow_path);
			} else {
				arena_dalloc_small(tsdn,
				    extent_node_arena_get(&chunk->node), chunk,
				    ptr, pageind);
			}
		} else {
			size_t size = arena_mapbits_large_size_get(chunk,
			    pageind);

			assert(config_cache_oblivious || ((uintptr_t)ptr &
			    PAGE_MASK) == 0);

			/*
			 * commented by yuanmu.lb
			 * when enable config_oblivious, large_pad is PAGE
			 *     to random the start address of large run
			 * when disable config_oblivious, large_pad is 0
			 */
			if (likely(tcache != NULL) && size - large_pad <=
			    tcache_maxclass) {
				tcache_dalloc_large(tsdn_tsd(tsdn), tcache, ptr,
				    size - large_pad, slow_path);
			} else {
				arena_dalloc_large(tsdn,
				    extent_node_arena_get(&chunk->node), chunk,
				    ptr);
			}
		}
	} else
		huge_dalloc(tsdn, ptr);
}

JEMALLOC_ALWAYS_INLINE void
arena_sdalloc(tsdn_t *tsdn, void *ptr, size_t size, tcache_t *tcache,
    bool slow_path)
{
	arena_chunk_t *chunk;

	assert(!tsdn_null(tsdn) || tcache == NULL);

	chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(ptr);
	if (likely(chunk != ptr)) {
		if (config_prof && opt_prof) {
			size_t pageind = ((uintptr_t)ptr - (uintptr_t)chunk) >>
			    LG_PAGE;
			assert(arena_mapbits_allocated_get(chunk, pageind) !=
			    0);
			if (arena_mapbits_large_get(chunk, pageind) != 0) {
				/*
				 * Make sure to use promoted size, not request
				 * size.
				 */
				size = arena_mapbits_large_size_get(chunk,
				    pageind) - large_pad;
			}
		}
		assert(s2u(size) == s2u(arena_salloc(tsdn, ptr, false)));

		if (likely(size <= SMALL_MAXCLASS)) {
			/* Small allocation. */
			if (likely(tcache != NULL)) {
				szind_t binind = size2index(size);
				tcache_dalloc_small(tsdn_tsd(tsdn), tcache, ptr,
				    binind, slow_path);
			} else {
				size_t pageind = ((uintptr_t)ptr -
				    (uintptr_t)chunk) >> LG_PAGE;
				arena_dalloc_small(tsdn,
				    extent_node_arena_get(&chunk->node), chunk,
				    ptr, pageind);
			}
		} else {
			assert(config_cache_oblivious || ((uintptr_t)ptr &
			    PAGE_MASK) == 0);

			if (likely(tcache != NULL) && size <= tcache_maxclass) {
				tcache_dalloc_large(tsdn_tsd(tsdn), tcache, ptr,
				    size, slow_path);
			} else {
				arena_dalloc_large(tsdn,
				    extent_node_arena_get(&chunk->node), chunk,
				    ptr);
			}
		}
	} else
		huge_dalloc(tsdn, ptr);
}
#  endif /* JEMALLOC_ARENA_INLINE_B */
#endif

#endif /* JEMALLOC_H_INLINES */
/******************************************************************************/
