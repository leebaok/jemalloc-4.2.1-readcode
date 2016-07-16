## malloc 流程
```
je_malloc
|
+--ialloc_body (malloc_slow 一般为 false)
|  |
|  +--tsd_fetch 获取 tsd ( thread specific data )
|  |
|  +--根据 size 获得 ind
|  |
|  +--ialloc
|     |
|     +--tcache_get
|     |  |
|     |  +--tsd_tcache_get
|     |  |
|     |  +-[?] tcache==NULL 及 tsd 状态为 nominal
|     |     |
|     |     Y--+--tcache_get_hard
|     |        |  选择一个 arena，在该 arena 上创建 tcache
|     |        |  |
|     |        |  +--arena_choose
|     |        |  |  选择 application arena
|     |        |  |  |
|     |        |  |  +--arena_choose_impl (internal=false)
|     |        |  |     |
|     |        |  |     +--tsd_arena_get
|     |        |  |     |
|     |        |  |     +-[?] arena==NULL
|     |        |  |        |
|     |        |  |        Y--arena_choose_hard 
|     |        |  |           同时选取 application arean 和 internal arean 
|     |        |  |           按照 负载为零>未初始化>负载最轻 的优先级选取
|     |        |  |           如果选了未初始化的 arena, 则调用 arena_init_locked 
|     |        |  |               先初始化 (初始化流程见 init.md)
|     |        |  |           选择完成后，使用 arena_bind 绑定 tsd、arena
|     |        |  |           根据 internal 参数返回 结果
|     |        |  |
|     |        |  +--tcache_create
|     |        |     |
|     |        |     +--计算 tcache 的大小，包括 tcache_bin 和 bin 的 stack elements
|     |        |     |
|     |        |     +--ipallocztm (jemalloc_internal.h)
|     |        |     |  (arena=arena[0], is_metadata=true) 
|     |        |     |  在 arena[0] 中为 tcache 分配空间
|     |        |     |  |
|     |        |     |  +--arena_palloc (arena.c)
|     |        |     |  |  |
|     |        |     |  |  +-[?] usize <= SMALL_MAXCLASS
|     |        |     |  |     |
|     |        |     |  |     Y--arena_malloc (arena.h)
|     |        |     |  |     |  |
|     |        |     |  |     |  +--tcache 此时为 NULL，跳过 tcache_alloc_small/large
|     |        |     |  |     |  |
|     |        |     |  |     |  +--arena_malloc_hard (arena.c)
|     |        |     |  |     |     |
|     |        |     |  |     |     +--arena_choose
|     |        |     |  |     |     |  这里将 arena[0] 传给 arena_choose
|     |        |     |  |     |     |  arena_choose 会直接将 arena[0] 返回
|     |        |     |  |     |     |
|     |        |     |  |     |     +-[?] size <= SMALL_MAXCLASS
|     |        |     |  |     |        |
|     |        |     |  |     |        Y--arena_malloc_small
|     |        |     |  |     |        |  |
|     |        |     |  |     |        |  +--根据 ind 选出 bin=arena->bins[ind]
|     |        |     |  |     |        |  |
|     |        |     |  |     |        |  +-[?] (run=bin->runcur)!=NULL & run->nfree>0
|     |        |     |  |     |        |  |  | 
|     |        |     |  |     |        |  |  Y--arena_run_reg_alloc
|     |        |     |  |     |        |  |  |  根据 run 的 bitmap 找出第一个可用的 region
|     |        |     |  |     |        |  |  |  根据 ind、offset 等信息算出 region 地址 
|     |        |     |  |     |        |  |  | 
|     |        |     |  |     |        |  |  N--arena_bin_malloc_hard
|     |        |     |  |     |        |  |     |
|     |        |     |  |     |        |  |     +--run = arena_bin_nonfull_run_get
|     |        |     |  |     |        |  |     |  |
|     |        |     |  |     |        |  |     |  +--arena_bin_nonfull_run_tryget
|     |        |     |  |     |        |  |     |  |  从 bin->runs 中尝试获得一个 run
|     |        |     |  |     |        |  |     |  |
|     |        |     |  |     |        |  |     |  +-[?] 上一步 tryget 失败
|     |        |     |  |     |        |  |     |  |  |
|     |        |     |  |     |        |  |     |  |  Y--arena_run_alloc_small
|     |        |     |  |     |        |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     +--arena_run_alloc_small_helper
|     |        |     |  |     |        |  |     |  |     |  |
|     |        |     |  |     |        |  |     |  |     |  +--arena_run_first_best_fit
|     |        |     |  |     |        |  |     |  |     |  |  |
|     |        |     |  |     |        |  |     |  |     |  |  +--size->run_quantize_ceil->size2index 
|     |        |     |  |     |        |  |     |  |     |  |  |  将 size 通过两次调整，会浪费一些空间
|     |        |     |  |     |        |  |     |  |     |  |  |  但是，得到的尺寸既可以用作 
|     |        |     |  |     |        |  |     |  |     |  |  |     small bin run,又可以作 large run
|     |        |     |  |     |        |  |     |  |     |  |  |  所以，后续复用会非常有利
|     |        |     |  |     |        |  |     |  |     |  |  |  而且，多余的空间会在split中放回来
|     |        |     |  |     |        |  |     |  |     |  |  |
|     |        |     |  |     |        |  |     |  |     |  |  +--在arena->runs_avail中找到合适的run
|     |        |     |  |     |        |  |     |  |     |  |
|     |        |     |  |     |        |  |     |  |     |  +--arena_run_split_small
|     |        |     |  |     |        |  |     |  |     |     |
|     |        |     |  |     |        |  |     |  |     |     +--获得一些参数
|     |        |     |  |     |        |  |     |  |     |     |
|     |        |     |  |     |        |  |     |  |     |     +--arena_run_split_remove
|     |        |     |  |     |        |  |     |  |     |     |  |
|     |        |     |  |     |        |  |     |  |     |     |  +--arena_avail_remove
|     |        |     |  |     |        |  |     |  |     |     |  |  从 runs_avail 中移除该 run
|     |        |     |  |     |        |  |     |  |     |     |  |  此处使用 run_quantize_floor 去调整
|     |        |     |  |     |        |  |     |  |     |     |  |  因为实际 run 的尺寸会大于其所在 ind的尺寸
|     |        |     |  |     |        |  |     |  |     |     |  |
|     |        |     |  |     |        |  |     |  |     |     |  +--如果是dirty，则用 arean_run_dirty_remove从
|     |        |     |  |     |        |  |     |  |     |     |  |      runs_dirty 中删去该 run 的 map_misc
|     |        |     |  |     |        |  |     |  |     |     |  |  因为 runs_dirty 是一个双向环形链表
|     |        |     |  |     |        |  |     |  |     |     |  |  删除的时候将该run自己的map_misc的指针
|     |        |     |  |     |        |  |     |  |     |     |  |      进行修改就可以完成在链表中删除自己
|     |        |     |  |     |        |  |     |  |     |     |  |
|     |        |     |  |     |        |  |     |  |     |     |  +--arena_avail_insert
|     |        |     |  |     |        |  |     |  |     |     |     将多余的页面返回到 arena->runs_avail
|     |        |     |  |     |        |  |     |  |     |     |     多余的页面使用 run_quantize_floor 确定 ind
|     |        |     |  |     |        |  |     |  |     |     |
|     |        |     |  |     |        |  |     |  |     |     +--初始化 run 的 mapbits
|     |        |     |  |     |        |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     +--上一步失败，调用 arean_chunk_alloc (arena.c)
|     |        |     |  |     |        |  |     |  |     |  上一步失败，说明没有可用的 run，需要申请新的 chunk
|     |        |     |  |     |        |  |     |  |     |  |
|     |        |     |  |     |        |  |     |  |     |  +-[?] arena->spare != NULL
|     |        |     |  |     |        |  |     |  |     |  |  |
|     |        |     |  |     |        |  |     |  |     |  |  Y--arena_chunk_init_spare
|     |        |     |  |     |        |  |     |  |     |  |  |  将arena->spare作为新chunk，并将spare置为 NULL
|     |        |     |  |     |        |  |     |  |     |  |  |
|     |        |     |  |     |        |  |     |  |     |  |  N--arena_chunk_init_hard
|     |        |     |  |     |        |  |     |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     |  |     +--arena_chunk_alloc_internal
|     |        |     |  |     |        |  |     |  |     |  |     |  |
|     |        |     |  |     |        |  |     |  |     |  |     |  +--chunk_alloc_cache
|     |        |     |  |     |        |  |     |  |     |  |     |  |  |
|     |        |     |  |     |        |  |     |  |     |  |     |  |  +--chunk_recycle
|     |        |     |  |     |        |  |     |  |     |  |     |  |     在 chunks_szad_cache,chunks_ad_cache 中回收 
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     +-[?] new_addr != NULL，根据地址分配
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  Y--+--extent_node_init
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |  |  使用 addr,size 初始化node
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |  |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |  +--extent_tree_ad_search
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |     使用地址在 chunks_ad 中查找
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  N--chunk_first_best_fit
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |     使用 extent_tree_szad_nsearch 
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |     在 chunk_szad 中查找
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     +--没找到node，或者找到的node太小
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  返回 NULL
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     +--根据对齐要求，得到多余的头部、尾部
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  (leadsize, trailsize)
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     +--extent_tree_szad_remove
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  extent_tree_ad_remove
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  从chunks_szad, chunks_ad删除node
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     +--arena_chunk_cache_maybe_remove
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  如果node是dirty，则从arena->chunks_cache
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |      arena->runs_dirty 中删除
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     +--如果头部有多余，那么
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  extent_tree_szad_insert 插入 chunks_szad
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  extent_tree_ad_insert 插入 chunks_ad
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  arena_chunk_cache_maybe_insert 插入 
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |       chunks_cache, runs_dirty
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     +--如果尾部有多余，那么
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  +--chunk_hooks->split
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |  实际调用 chunk_split_default
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |  地址空间就是一个数值，切分没有实际操作
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |  chunk_split_default 返回 false,表示成功
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  +--如果 node 为 NULL, 调用 arena_node_alloc 
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |  arean_node_alloc使用base_alloc为node分配空间
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |  分配失败，则调用 chunk_record
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |  这里调用 chunk_record 只是为了再次尝试
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |      分配空间，并将 多余空间 放回树中
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |  红黑树的节点都是base_alloc分配的，所以如果
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |      不用，需要调用 arena_node_dalloc 放到
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |      arena->node_cache 缓存起来
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |  +--将尾部空间放回 chunks_sazd,chunks_ad 及
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |         runs_dirty,chunks_cache
|     |        |     |  |     |        |  |     |  |     |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     |  |     |  |     +--如果有必要，arena_node_dalloc(node)将node放回
|     |        |     |  |     |        |  |     |  |     |  |     |  |            arena->node_cache
|     |        |     |  |     |        |  |     |  |     |  |     |  |
|     |        |     |  |     |        |  |     |  |     |  |     |  +-[?] 上一步分配成功
|     |        |     |  |     |        |  |     |  |     |  |     |     |
|     |        |     |  |     |        |  |     |  |     |  |     |     Y--+--arena_chunk_register
|     |        |     |  |     |        |  |     |  |     |  |     |     |  |  |
|     |        |     |  |     |        |  |     |  |     |  |     |     |  |  +--chunk_register
|     |        |     |  |     |        |  |     |  |     |  |     |     |  |     将 chunk 在 rtree 中登记
|     |        |     |  |     |        |  |     |  |     |  |     |     |  |     rtree--radix tree--基数速
|     |        |     |  |     |        |  |     |  |     |  |     |     |  |
|     |        |     |  |     |        |  |     |  |     |  |     |     |  +--注册失败，调用chunk_dalloc_cache
|     |        |     |  |     |        |  |     |  |     |  |     |     |     (chunk_dalloc_cache解释在后面)
|     |        |     |  |     |        |  |     |  |     |  |     |     |
|     |        |     |  |     |        |  |     |  |     |  |     |     +--arena_chunk_alloc_internal_hard (arena.c)
|     |        |     |  |     |        |  |     |  |     |  |     |        |
|     |        |     |  |     |        |  |     |  |     |  |     |        +--chunk_alloc_wrapper
|     |        |     |  |     |        |  |     |  |     |  |     |        |  |
|     |        |     |  |     |        |  |     |  |     |  |     |        |  +--chunk_alloc_retained
|     |        |     |  |     |        |  |     |  |     |  |     |        |  |  |
|     |        |     |  |     |        |  |     |  |     |  |     |        |  |  +--chunk_recycle
|     |        |     |  |     |        |  |     |  |     |  |     |        |  |     在chunks_szad_retained,chunks_ad_retained
|     |        |     |  |     |        |  |     |  |     |  |     |        |  |     中回收 chunk
|     |        |     |  |     |        |  |     |  |     |  |     |        |  |
|     |        |     |  |     |        |  |     |  |     |  |     |        |  +-[?] 上一步失败
|     |        |     |  |     |        |  |     |  |     |  |     |        |     |
|     |        |     |  |     |        |  |     |  |     |  |     |        |     +--chunk_alloc_default_impl
|     |        |     |  |     |        |  |     |  |     |  |     |        |        |
|     |        |     |  |     |        |  |     |  |     |  |     |        |        +--chunk_alloc_core
|     |        |     |  |     |        |  |     |  |     |  |     |        |           根据策略使用 chunk_alloc_dss 或者
|     |        |     |  |     |        |  |     |  |     |  |     |        |           chunk_alloc_mmap 分配 chunk
|     |        |     |  |     |        |  |     |  |     |  |     |        |           (默认使用 mmap)
|     |        |     |  |     |        |  |     |  |     |  |     |        |           |
|     |        |     |  |     |        |  |     |  |     |  |     |        |           +--chunk_alloc_dss
|     |        |     |  |     |        |  |     |  |     |  |     |        |           |  使用 sbrk 申请地址空间
|     |        |     |  |     |        |  |     |  |     |  |     |        |           |  (详细过程见代码及注释)
|     |        |     |  |     |        |  |     |  |     |  |     |        |           | 
|     |        |     |  |     |        |  |     |  |     |  |     |        |           +--chunk_alloc_mmap
|     |        |     |  |     |        |  |     |  |     |  |     |        |              使用 mmap 申请地址空间
|     |        |     |  |     |        |  |     |  |     |  |     |        |              (详细过程见代码及注释)
|     |        |     |  |     |        |  |     |  |     |  |     |        |
|     |        |     |  |     |        |  |     |  |     |  |     |        +--如果overcommit=0，则检验是否内存是否 commit 了
|     |        |     |  |     |        |  |     |  |     |  |     |        |  如果是，则chunk_dalloc_wrapper
|     |        |     |  |     |        |  |     |  |     |  |     |        |  (大部分平台默认是 overcommit=1/2 的)
|     |        |     |  |     |        |  |     |  |     |  |     |        |
|     |        |     |  |     |        |  |     |  |     |  |     |        +-[?] arena_chunk_register 成功
|     |        |     |  |     |        |  |     |  |     |  |     |           |
|     |        |     |  |     |        |  |     |  |     |  |     |           N--chunk_dalloc_wrapper  
|     |        |     |  |     |        |  |     |  |     |  |     |              |
|     |        |     |  |     |        |  |     |  |     |  |     |              +--chunk_dalloc_default_impl
|     |        |     |  |     |        |  |     |  |     |  |     |              |  如果addr不在dss中，使用 
|     |        |     |  |     |        |  |     |  |     |  |     |              |  chunk_dalloc_mmap/pages_unmap 释放空间
|     |        |     |  |     |        |  |     |  |     |  |     |              |
|     |        |     |  |     |        |  |     |  |     |  |     |              +--上述释放成功，返回
|     |        |     |  |     |        |  |     |  |     |  |     |              |
|     |        |     |  |     |        |  |     |  |     |  |     |              +--chunk_decommit_default
|     |        |     |  |     |        |  |     |  |     |  |     |              |  调用pages_decommit/pages_commit_impl
|     |        |     |  |     |        |  |     |  |     |  |     |              |  来 decommit 地址，如果 os_overcommit!=0,
|     |        |     |  |     |        |  |     |  |     |  |     |              |  则不 decommit，否则使用mmap(PROT_NONE)来
|     |        |     |  |     |        |  |     |  |     |  |     |              |  decommit 地址空间
|     |        |     |  |     |        |  |     |  |     |  |     |              |
|     |        |     |  |     |        |  |     |  |     |  |     |              +--如果decommit失败，调用chunk_purge_default
|     |        |     |  |     |        |  |     |  |     |  |     |              |  使用 madvise 来 释放地址空间
|     |        |     |  |     |        |  |     |  |     |  |     |              |
|     |        |     |  |     |        |  |     |  |     |  |     |              +--chunk_record
|     |        |     |  |     |        |  |     |  |     |  |     |                 将 地址空间 放到 chunks_szad/ad_retained 
|     |        |     |  |     |        |  |     |  |     |  |     |                 树中，可供之后的chunk申请使用。
|     |        |     |  |     |        |  |     |  |     |  |     |                 (retained 树中是有地址空间，但是没有实际
|     |        |     |  |     |        |  |     |  |     |  |     |                  物理内存的，而cached树中是有物理内存映射
|     |        |     |  |     |        |  |     |  |     |  |     |                  的，所以申请chunk时，cached树优先级更高)
|     |        |     |  |     |        |  |     |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     |  |     +--调用 arena_mapbits_unallocated_set
|     |        |     |  |     |        |  |     |  |     |  |        初始化 chunk 的 mapbits
|     |        |     |  |     |        |  |     |  |     |  |
|     |        |     |  |     |        |  |     |  |     |  +--ql_tail_insert
|     |        |     |  |     |        |  |     |  |     |  |  将该 chunk 插入到 arena->achunks
|     |        |     |  |     |        |  |     |  |     |  |
|     |        |     |  |     |        |  |     |  |     |  +--arena_avail_insert
|     |        |     |  |     |        |  |     |  |     |     将该 chunk 的 maxrun 插入 runs_avail
|     |        |     |  |     |        |  |     |  |     |  
|     |        |     |  |     |        |  |     |  |     +-[?] chunk 分配成功
|     |        |     |  |     |        |  |     |  |        |  chunk 分配成功初始化的时候，会自动有一个maxrun
|     |        |     |  |     |        |  |     |  |        |
|     |        |     |  |     |        |  |     |  |        Y--arena_run_split_small (见上面的流程)
|     |        |     |  |     |        |  |     |  |        |
|     |        |     |  |     |        |  |     |  |        N--其他线程可能给该 arena 分配了 chunk
|     |        |     |  |     |        |  |     |  |           再试一次arena_run_alloc_small_helper
|     |        |     |  |     |        |  |     |  |
|     |        |     |  |     |        |  |     |  +--如果上一步 arean_run_alloc_small 也失败了
|     |        |     |  |     |        |  |     |     再尝试一次 arena_bin_nonfull_run_tryget
|     |        |     |  |     |        |  |     |     (因为中间有换锁，可能有其他线程填充了runs)
|     |        |     |  |     |        |  |     |
|     |        |     |  |     |        |  |     +--如果其他线程填充了 runcur
|     |        |     |  |     |        |  |     |  |
|     |        |     |  |     |        |  |     |  +--arena_run_reg_alloc 从 runcur 分配 reg
|     |        |     |  |     |        |  |     |  |
|     |        |     |  |     |        |  |     |  +--如果 run 是满的，用 arean_dalloc_bin_run 回收
|     |        |     |  |     |        |  |     |     否则调用 arena_bin_lower_run 将 run、runcur 
|     |        |     |  |     |        |  |     |           中较少的变成新的 run，另一个放回 runs
|     |        |     |  |     |        |  |     |  
|     |        |     |  |     |        |  |     +--arena_run_reg_alloc
|     |        |     |  |     |        |  |   
|     |        |     |  |     |        |  +--更新统计数据   
|     |        |     |  |     |        |  |   
|     |        |     |  |     |        |  +--根据 junk 等参数配置本次分配
|     |        |     |  |     |        |  |   
|     |        |     |  |     |        |  +--arena_decay_tick
|     |        |     |  |     |        |      
|     |        |     |  |     |       [?] size <= large_maxclass
|     |        |     |  |     |        |
|     |        |     |  |     |        Y--arene_malloc_large
|     |        |     |  |     |        |
|     |        |     |  |     |        N--huge_malloc
|     |        |     |  |     | 
|     |        |     |  |    [?] usize <= large_maxclass & alignment <= PAGE
|     |        |     |  |     |
|     |        |     |  |     Y--+--arena_malloc (同上 arena_malloc)
|     |        |     |  |     |  |
|     |        |     |  |     |  +--如果设置了 config_cache_oblivious，正则一下地址
|     |        |     |  |     |
|     |        |     |  |    [?] usize <= large_maxclass
|     |        |     |  |     |
|     |        |     |  |     Y--arena_palloc_large
|     |        |     |  |     |
|     |        |     |  |    [?] alignment <= chunksize
|     |        |     |  |     |
|     |        |     |  |     Y--huge_malloc
|     |        |     |  |     |
|     |        |     |  |     N--huge_pmalloc
|     |        |     |  |
|     |        |     |  |
|     |        |     |  +--(arena_metadata_allocated_add : 统计相关，忽略)
|     |        |     |   
|     |        |     +--tcache_arena_associate
|     |        |     |
|     |        |     +--ticker_init
|     |        |     |
|     |        |     +--初始化 tcache->tbins[i].avail，指向各 bin 的 stack 
|     |        | 
|     |        | 
|     |        | 
|     |        | 
|     |        | 
|     |        | 
|     |        | 
|     |        | 
|     |        +--tsd_tcache_set
|     |
|     +--iallocztm
|
+--ialloc_post_check

```
