## 数据结构
jemalloc 为了减少锁的竞争，提高性能，设计了 arena、run、chunk、bin、tcache 等主要数据机构。
其中，chunk、run 是对地址空间进行划分的单元，jemalloc 每次向操作系统申请一个或多个 chunk，
然后将 chunk 切分成 run 在内部进行分配。
arena、bin 是 jemalloc 内部用来集中管理分配、回收的数据结构。tcache 是每个线程私有的管理结构，
用来缓存从 arena、bin 申请来的内存。
```
      thread                 thread     
      /   \                   /  \
     |     |                 |    |
  tcache   |             tcache   |
           |                      |
        +--+---------------+   +--+--------------+
        |                  |   |                 |
        |                  |   |                 |
   +---------+          +---------+         +---------+
   |  arena  |          |  arena  |         |  arena  |   
   +---------+          +---------+         +---------+
   |   bin   |          |   bin   |         |   bin   |
   |   ...   |          |   ...   |         |   ...   |
   |   bin   |          |   bin   |         |   bin   |
   +---------+          +---------+         +---------+
        |
        +---+-------------------------------+
            |                               |
            |                               |
    +-----chunk-----+               +-----chunk-----+      
    | chunk header  |               | chunk header  |       
    +---------------+               +---------------+  
    |  +---run---+  |               |  +---run---+  |
    |  |  region |  |               |  |  region |  |     
    |  |  ...    |  |               |  |  ...    |  |
    |  +---------+  |               |  +---------+  |    
    |  +---run---+  |               |  +---run---+  |  
    |  |  region |  |               |  |  region |  |        
    |  |  ...    |  |               |  |  ...    |  |
    |  +---------+  |               |  +---------+  |
    |  ... ... ...  |               |  ... ... ...  |     
    +---------------+               +---------------+         

```

### arena
arena 是 jemalloc 中央管理的核心，其管理其拥有的 chunk 和 run，并向上提供分配服务。
```
struct arena_s {
	/* 该 arena 在 arena 数组中的编号 */
	unsigned		ind;

	/*
	 * Number of threads currently assigned to this arena, synchronized via
	 * atomic operations.  Each thread has two distinct assignments, one for
	 * application-serving allocation, and the other for internal metadata
	 * allocation.  Internal metadata must not be allocated from arenas
	 * created via the arenas.extend mallctl, because the arena.<i>.reset
	 * mallctl indiscriminately discards all allocations for the affected
	 * arena.
	 *
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
	 * List of tcaches for extant threads associated with this arena.
	 * Stats from these are merged incrementally, and at exit if
	 * opt_stats_print is enabled.
	 */
	ql_head(tcache_t)	tcache_ql;

	uint64_t		prof_accumbytes;

	/*
	 * PRNG state for cache index randomization of large allocation base
	 * pointers.
	 */
	uint64_t		offset_state;

	dss_prec_t		dss_prec;


	/* Extant arena chunks. */
	ql_head(extent_node_t)	achunks;

	/*
	 * In order to avoid rapid chunk allocation/deallocation when an arena
	 * oscillates right on the cusp of needing a new chunk, cache the most
	 * recently freed chunk.  The spare is left in the arena's chunk trees
	 * until it is deleted.
	 *
	 * There is one spare chunk per arena, rather than one spare total, in
	 * order to avoid interactions between multiple threads that could make
	 * a single spare inadequate.
	 */
	arena_chunk_t		*spare;

	/* Minimum ratio (log base 2) of nactive:ndirty. */
	ssize_t			lg_dirty_mult;

	/* True if a thread is currently executing arena_purge_to_limit(). */
	bool			purging;

	/* Number of pages in active runs and huge regions. */
	size_t			nactive;

	/*
	 * Current count of pages within unused runs that are potentially
	 * dirty, and for which madvise(... MADV_DONTNEED) has not been called.
	 * By tracking this, we can institute a limit on how much dirty unused
	 * memory is mapped for each arena.
	 */
	size_t			ndirty;

	/*
	 * Unused dirty memory this arena manages.  Dirty memory is conceptually
	 * tracked as an arbitrarily interleaved LRU of dirty runs and cached
	 * chunks, but the list linkage is actually semi-duplicated in order to
	 * avoid extra arena_chunk_map_misc_t space overhead.
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
	 */
	/*
	 * commented by yuanmu.lb
	 * rd of run is not stored in run, it is in arena_chunk_map_misc_t
	 * associated with this run.
	 * rd of chunk is stored in the extent_node in chunk header
	 */
	arena_runs_dirty_link_t	runs_dirty;
	extent_node_t		chunks_cache;

	/*
	 * Approximate time in seconds from the creation of a set of unused
	 * dirty pages until an equivalent set of unused dirty pages is purged
	 * and/or reused.
	 */
	ssize_t			decay_time;
	/* decay_time / SMOOTHSTEP_NSTEPS. */
	nstime_t		decay_interval;
	/*
	 * Time at which the current decay interval logically started.  We do
	 * not actually advance to a new epoch until sometime after it starts
	 * because of scheduling and computation delays, and it is even possible
	 * to completely skip epochs.  In all cases, during epoch advancement we
	 * merge all relevant activity into the most recently recorded epoch.
	 */
	nstime_t		decay_epoch;
	/* decay_deadline randomness generator. */
	uint64_t		decay_jitter_state;
	/*
	 * Deadline for current epoch.  This is the sum of decay_interval and
	 * per epoch jitter which is a uniform random variable in
	 * [0..decay_interval).  Epochs always advance by precise multiples of
	 * decay_interval, but we randomize the deadline to reduce the
	 * likelihood of arenas purging in lockstep.
	 */
	nstime_t		decay_deadline;
	/*
	 * Number of dirty pages at beginning of current epoch.  During epoch
	 * advancement we use the delta between decay_ndirty and ndirty to
	 * determine how many dirty pages, if any, were generated, and record
	 * the result in decay_backlog.
	 */
	size_t			decay_ndirty;
	/*
	 * Memoized result of arena_decay_backlog_npages_limit() corresponding
	 * to the current contents of decay_backlog, i.e. the limit on how many
	 * pages are allowed to exist for the decay epochs.
	 */
	size_t			decay_backlog_npages_limit;
	/*
	 * Trailing log of how many unused dirty pages were generated during
	 * each of the past SMOOTHSTEP_NSTEPS decay epochs, where the last
	 * element is the most recent epoch.  Corresponding epoch times are
	 * relative to decay_epoch.
	 */
	size_t			decay_backlog[SMOOTHSTEP_NSTEPS];

	/* Extant huge allocations. */
	ql_head(extent_node_t)	huge;
	/* Synchronizes all huge allocation/update/deallocation. */
	malloc_mutex_t		huge_mtx;

	/*
	 * Trees of chunks that were previously allocated (trees differ only in
	 * node ordering).  These are used when allocating chunks, in an attempt
	 * to re-use address space.  Depending on function, different tree
	 * orderings are needed, which is why there are two trees with the same
	 * contents.
	 */
	extent_tree_t		chunks_szad_cached;
	extent_tree_t		chunks_ad_cached;
	extent_tree_t		chunks_szad_retained;
	extent_tree_t		chunks_ad_retained;

	malloc_mutex_t		chunks_mtx;
	/* Cache of nodes that were allocated via base_alloc(). */
	ql_head(extent_node_t)	node_cache;
	malloc_mutex_t		node_cache_mtx;

	/* User-configurable chunk hook functions. */
	chunk_hooks_t		chunk_hooks;

	/* bins is used to store trees of free regions. */
	arena_bin_t		bins[NBINS];

	/*
	 * Quantized address-ordered heaps of this arena's available runs.  The
	 * heaps are used for first-best-fit run allocation.
	 */
	arena_run_heap_t	runs_avail[1]; /* Dynamically sized. */
};
```

### bin
bin 是实际分配的大小，其将分配尺寸分成很多个类型，每一类是一个 bin。
```
struct arena_bin_info_s {
	/* Size of regions in a run for this bin's size class. */
	size_t			reg_size;

	/* Redzone size. */
	size_t			redzone_size;

	/* Interval between regions (reg_size + (redzone_size << 1)). */
	size_t			reg_interval;

	/* Total size of a run for this bin's size class. */
	size_t			run_size;

	/* Total number of regions in a run for this bin's size class. */
	uint32_t		nregs;

	/*
	 * Metadata used to manipulate bitmaps for runs associated with this
	 * bin.
	 */
	bitmap_info_t		bitmap_info;

	/* Offset of first region in a run for this bin's size class. */
	uint32_t		reg0_offset;
};

struct arena_bin_s {
	/*
	 * All operations on runcur, runs, and stats require that lock be
	 * locked.  Run allocation/deallocation are protected by the arena lock,
	 * which may be acquired while holding one or more bin locks, but not
	 * vise versa.
	 */
	malloc_mutex_t		lock;

	/*
	 * Current run being used to service allocations of this bin's size
	 * class.
	 */
	arena_run_t		*runcur;

	/*
	 * Heap of non-full runs.  This heap is used when looking for an
	 * existing run when runcur is no longer usable.  We choose the
	 * non-full run that is lowest in memory; this policy tends to keep
	 * objects packed well, and it can also help reduce the number of
	 * almost-empty chunks.
	 */
	arena_run_heap_t	runs;

	/* Bin statistics. */
	malloc_bin_stats_t	stats;
};
```

### chunk
chunk 是一块内存空间，在我的机器上是 2M
```
/* Each element of the chunk map corresponds to one page within the chunk. */
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
	 *   Unallocated (clean):                    | commented by yuanmu.lb
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

struct arena_chunk_map_misc_s {
	/*
	 * Linkage for run heaps.  There are two disjoint uses:
	 *
	 * 1) arena_t's runs_avail heaps.
	 * 2) arena_run_t conceptually uses this linkage for in-use non-full
	 *    runs, rather than directly embedding linkage.
	 */
	phn(arena_chunk_map_misc_t)		ph_link;

	union {
		/* Linkage for list of dirty runs. */
		arena_runs_dirty_link_t		rd;

		/* Profile counters, used for large object runs. */
		union {
			void			*prof_tctx_pun;
			prof_tctx_t		*prof_tctx;
		};

		/* Small region run metadata. */
		arena_run_t			run;
	};
};

/* Arena chunk header. */
/*
 * commented by yuanmu.lb
 *
 * Below is the actual layout of chunk :
 *
 *   /-------chunk--------\   \
 *   |    extent_node     |   |
 *   |      map_bits      |   |
 *   |      ... ...       |    > chunk header
 *   |      map_bits      |   |  
 *   |      map_misc      |   |
 *   |      ... ...       |   |
 *   |      map_misc      |   |
 *   |--------------------|   /
 *   |        Page        |
 *   |--------------------|
 *   |        Page        |
 *   |--------------------|
 *   ...      ...       ...
 *   |                    |
 *   \--------------------/
 *
 */
struct arena_chunk_s {
	/*
	 * A pointer to the arena that owns the chunk is stored within the node.
	 * This field as a whole is used by chunks_rtree to support both
	 * ivsalloc() and core-based debugging.
	 */
	extent_node_t		node;

	/*
	 * Map of pages within chunk that keeps track of free/large/small.  The
	 * first map_bias entries are omitted, since the chunk header does not
	 * need to be tracked in the map.  This omission saves a header page
	 * for common chunk sizes (e.g. 4 MiB).
	 */
	arena_chunk_map_bits_t	map_bits[1]; /* Dynamically sized. */
	/*
	 * commented by yuanmu.lb
	 * map_bits is an array more than one element
	 * and there is an arena_chunk_map_misc_t array following map_bits
	 * map_bits here is just a placeholder to calculate offset of map
	 *   -- how to calculate offset? see line 3810 in arena.c
	 */
};

```

### run
chunk 划分成 run，run 又划分成 region。对于 small size，分配的是一个 region，
对于 large size，分配的是一个run，对于 huge，分配的是一个或多个 chunk。
```
struct arena_run_s {
	/* Index of bin this run is associated with. */
	szind_t		binind;

	/* Number of free regions in run. */
	unsigned	nfree;

	/* Per region allocated/deallocated bitmap. */
	bitmap_t	bitmap[BITMAP_GROUPS_MAX];
};

/*
 * Read-only information associated with each element of arena_t's bins array
 * is stored separately, partly to reduce memory usage (only one copy, rather
 * than one per arena), but mainly to avoid false cacheline sharing.
 *
 * Each run has the following layout:
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
 * reg_interval has at least the same minimum alignment as reg_size; this
 * preserves the alignment constraint that sa2u() depends on.  Alignment pad is
 * either 0 or redzone_size; it is present only if needed to align reg0_offset.
 */
```

### tcache
tcache 是每个 thread 的私有仓库，他对 run、region 进行了缓存，很多时候 thread 只需要
在本地的 tcache 中就可以获得需要的内存。
