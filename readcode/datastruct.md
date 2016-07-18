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


	/* Extant arena chunks. */
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
	 * * chunk 的 rd 在chunk头部的 extent node 中
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

```

### bin
bin 是实际分配的大小，其将分配尺寸分成很多个类型，每一类是一个 bin。
```
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

```

### chunk
chunk 是一块内存空间，在我的机器上是 2M
```

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

```

### run
chunk 划分成 run，run 又划分成 region。对于 small size，分配的是一个 region，
对于 large size，分配的是一个run，对于 huge，分配的是一个或多个 chunk。
```
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
```

### tcache
tcache 是每个 thread 的私有仓库，他对 run、region 进行了缓存，很多时候 thread 只需要
在本地的 tcache 中就可以获得需要的内存。
```
/*
 * tcache 中 bin 的数据结构，记录、管理每一个bin 的状态
 */
struct tcache_bin_s {
	tcache_bin_stats_t tstats;
	int		low_water;	/* Min # cached since last GC. */
	unsigned	lg_fill_div;	/* Fill (ncached_max >> lg_fill_div). */
	/* 当前缓存的数量 */
	unsigned	ncached;	/* # of cached objects. */
	/*
	 * 根据 tcache_bin_info_s 中的 ncached_max 为该 bin 申请指定数量的指针空间
	 * 来指向缓存的 region/run
	 * avail 指向的指针数组空间是动态申请的
	 */
	void		**avail;	/* Stack of available objects. */
};

/*
 * tcache 数据结构，管理 tcache 下所有 bin
 *   
 *             +---------------------+
 *           / | link                |
 * tcache_t <  | prof_accumbytes     |
 *           | | gc_ticker           |
 *           \ | next_gc_bin         |
 *             |---------------------|
 *           / | tstats              |
 *           | | low_water           |
 * tbins[0] <  | lg_fill_div         |
 *           | | ncached             |
 *           \ | avail               |--+
 *             |---------------------|  |
 *           / | tstats              |  |
 *           | | low_water           |  |
 *           | | lg_fill_div         |  |                     Run
 * tbins[1] <  | ncached             |  |                +-----------+
 *           | | avail               |--+--+             |  region   |
 *           \ |---------------------|  |  |             |-----------|
 *             ...  ...  ...            |  |   +-------->|  region   |
 *             |---------------------|  |  |   |         |-----------|
 *             | padding             |  |  |   |         |  region   |
 *             |---------------------|<-+  |   |         |-----------|
 *             | stack[0]            |-----+---+   +---->|  region   |
 *             | stack[1]            |-----+-------+     |-----------|
 *             | ...                 |     |             |  region   |
 *             | stack[ncache_max-1] |     |             |-----------|
 *             |---------------------|<----+      +----->|  region   |
 *             | stack[0]            |------------+      |-----------|
 *             | stack[1]            |                   |           |
 *             | ...                 |                   |           |
 *             | stack[ncache_max-1] |                   |           |
 *             |---------------------|                   |           |
 *             ...  ...  ...                             ...  ...  ...
 *             +---------------------+                   +-----------+
 *
 *
 */
struct tcache_s {
	ql_elm(tcache_t) link;		/* Used for aggregating stats. */
	uint64_t	prof_accumbytes;/* Cleared after arena_prof_accum(). */
	ticker_t	gc_ticker;	/* Drives incremental GC. */
	szind_t		next_gc_bin;	/* Next bin to GC. */
	/*
	 * tbins 有多个，具体个数是运行时决定的，空间也是运行时申请的
	 */
	tcache_bin_t	tbins[1];	/* Dynamically sized. */
};

```
