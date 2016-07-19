## 数据结构
前面简要介绍了 jemalloc 的数据结构和系统架构，下面来结合代码详细看看 jemalloc
的数据结构。

### chunk,run
首先来看两个和物理布局相关的数据结构：chunk，run。

```
struct arena_chunk_s {
	extent_node_t		node;
	arena_chunk_map_bits_t	map_bits[1];
};
```
chunk 数据结构很简洁，然而并不简单。node 中记录了 chunk 的属性信息(如 addr)、
管理信息(如 cc_link) 等等。
map_bits 记录着 chunk 中除了头部空间以外每一个 Page 的属性信息，这里 map_bits
数组长度并不是1，map_bits 在这里只是起到占位的作用，可以用来计算偏移，
知道 map_bits 的起始地址，而 map_bits 真正的长度是在运行时计算出来的，
计算过程见 arena.c 的 arena_boot 函数。
map_bits 之后还有一个 arena_chunk_map_misc_t(以下简称 map_misc) 数组，
其长度和 map_bits 一样，也是和每一个 Page 对应的，其真正的目的是用来记录、
管理 run 的，由于每一页都可能是 run，所以 map_misc 最多也是和页数一样多(除去头部
空间的页数)。

现在来看看和 chunk 相关的 map_bits、map_misc、run 的信息。

```
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
```
上述是 map_bits 的数据结构，其注释已经交代得十分清楚了，这里再说明一下。首先，map_bits 中
只有一个 `size_t bits;` ，一个32位(在32位系统上)的标记，该标记分为三类：
unallocated，small，large。对于一个 unallocated 或 small 或 large 的 run，其往往有
多个页面组成，因此对应页面的 map_bits 也会联合表示一个 run 的状态属性。上述注释中的五个样例
中每一个都给出了三行，其中第一行是该run第一页对应的map_bits的含义，第二行是该run中间页面对应的
map_bits的含义，第三行是该run最后一页对应的 map_bits 的含义。
最后，上述数据结构中还定义了很多宏，用来从 map_bits 中提取对应的信息。

```
typedef struct arena_run_s arena_run_t;

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
	/* 这是一个 多级 的 bitmap，具体实现见 bitmap.h */
	bitmap_t	bitmap[BITMAP_GROUPS_MAX];
};

/* qr 是通过宏实现的 双向环形列表 */
struct arena_runs_dirty_link_s {
	qr(arena_runs_dirty_link_t)	rd_link;
};

/* map_misc 保存在 chunk 的 header 中，并不和其相关的 run 存在一起 */
struct arena_chunk_map_misc_s {
	/*
	 * ph_link 用于构建 run 的堆，有两个互斥的使用场景：
	 * 1) arena 的 runs_avail 堆，管理 arena 中可用的 run
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

		/* small region 的元数据，指向上面的 arena_run_s */
		arena_run_t			run;
	};
};
```
上面是和 run 相关的一些重要的数据结构，对于数据结构中的每一个属性，上面都给出了解释，这里不
赘述。不过，要强调的是，map_misc 是非常重要的数据结构，其记录了 run 相关的重要信息，然而其
不存储在 run 中，而在chunk header 中，并且 map_misc 中有 ph_link、rd 等链接，用于链接
成 堆、链表，并且可以同时挂在 堆和链表中，完成复杂的数据管理。

现在再看看 extent_node_t :
```
struct extent_node_s {
	/* 记录该 node 隶属哪个 arena */
	arena_t			*en_arena;

	/* 指向该 node 表示的区域的地址 */
	void			*en_addr;

	/* 该 node 表示的区域的大小 */
	size_t			en_size;

	/* 该 node 表示的区域是否是全0 */
	bool			en_zeroed;

	/* 标记物理内存是否 commit，该标记和操作系统的 overcommit 有一定相关性 */
	bool			en_committed;

	/* 标记该 node 的是否是 arena 的 chunk？ 或者是是 huge 的 node？ */
	bool			en_achunk;

	/* Profile 相关 */
	prof_tctx_t		*en_prof_tctx;

	/*
	 * rd, cc_link 用来将 node 链接到 arena 的 runs_dirty 和 chunks_cache 中
	 * 虽然 node 表示的是 chunk 或 huge，但还是会被链到 runs_dirty 中，从而使
	 * chunks_cache 成为 runs_dirty 的子序列，方便 arena_purge_to_limit 遍历回收
	 */
	arena_runs_dirty_link_t	rd;
	qr(extent_node_t)	cc_link;

	union {
		/* 用来链接成 首先按照大小，其次按照地址排序的 红黑树 */
		rb_node(extent_node_t)	szad_link;

		/* 用来链接到 arena 的 achunks 或者 huge 或者 node_cache 列表中 */
		ql_elm(extent_node_t)	ql_link;
	};

	/* 用来连接成 地址排序的 红黑树*/
	rb_node(extent_node_t)	ad_link;
};
```
上述展示了 extent_node_s 的内容及每个成员的含义，其中可以看出 node 中包含很多用来链接的成员，
这样一个 node 可以同时链接到多个数据结构中，比如 szad_link、ad_link 可以使 node 同时挂在
两棵红黑树中，实际上代码中也是这么使用的。这样可以通过多种方式管理 node。

下面给出 chunk/run 在内存中的实际布局：
![chunk and run layout](pictures/chunk-run.png)

补充：commit/decommit 及 overcommit
