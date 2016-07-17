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
|     |        |     |  |     |  +-[?] tcache!=NULL
|     |        |     |  |     |  |  |
|     |        |     |  |     |  |  Y--+--size<=SMALL_MAXCLASS
|     |        |     |  |     |  |     |  tcache_alloc_small
|     |        |     |  |     |  |     |  |
|     |        |     |  |     |  |     |  +--tcache_alloc_easy
|     |        |     |  |     |  |     |  |  如果 tbin->avail 中有元素，则分配成功
|     |        |     |  |     |  |     |  |  同时 更新 tbin->low_water
|     |        |     |  |     |  |     |  |
|     |        |     |  |     |  |     |  +--上一步失败，tcache_alloc_small_hard
|     |        |     |  |     |  |     |  |  |
|     |        |     |  |     |  |     |  |  +--arena_tcache_fill_small
|     |        |     |  |     |  |     |  |  |  |
|     |        |     |  |     |  |     |  |  |  +--根据 tbin->lg_fill_div, tbin->ncached_max 计算需要填充的数量
|     |        |     |  |     |  |     |  |  |  |
|     |        |     |  |     |  |     |  |  |  +--重复调用 arena_run_reg_alloc,arena_bin_malloc_hard填充 tbin
|     |        |     |  |     |  |     |  |  |  |
|     |        |     |  |     |  |     |  |  |  +--arena_decay_tick
|     |        |     |  |     |  |     |  |  |
|     |        |     |  |     |  |     |  |  +--tcache_alloc_easy
|     |        |     |  |     |  |     |  |
|     |        |     |  |     |  |     |  +--tcache_event
|     |        |     |  |     |  |     |     |
|     |        |     |  |     |  |     |     +-[?] ticker_tick
|     |        |     |  |     |  |     |        |
|     |        |     |  |     |  |     |        +--tcache_event_hard
|     |        |     |  |     |  |     |           对某一个 tbin 进行回收
|     |        |     |  |     |  |     |           |
|     |        |     |  |     |  |     |           +--获取本次回收对象 tcache->next_gc_bin
|     |        |     |  |     |  |     |           |
|     |        |     |  |     |  |     |           +-[?] binind < NBINS
|     |        |     |  |     |  |     |           |  |
|     |        |     |  |     |  |     |           |  Y--tcache_bin_flush_small
|     |        |     |  |     |  |     |           |  |  使用 arean_dalloc_bin_junked_locked 重复释放 bin
|     |        |     |  |     |  |     |           |  |  直到达到要求
|     |        |     |  |     |  |     |           |  |  (具体实现见代码)
|     |        |     |  |     |  |     |           |  |
|     |        |     |  |     |  |     |           |  N--tcache_bin_flush_large
|     |        |     |  |     |  |     |           |     使用 arean_dalloc_large_junked_locked 重复释放 run
|     |        |     |  |     |  |     |           |     直到达到要求
|     |        |     |  |     |  |     |           |     (具体实现见代码)
|     |        |     |  |     |  |     |           |  
|     |        |     |  |     |  |     |           +--根据 low_water 动态调整填充度lg_fill_div  
|     |        |     |  |     |  |     |           |  
|     |        |     |  |     |  |     |           +--设置下一次回收的ind
|     |        |     |  |     |  |     |
|     |        |     |  |     |  |     +--size<=tcache_maxclass
|     |        |     |  |     |  |        tcache_alloc_large
|     |        |     |  |     |  |        |
|     |        |     |  |     |  |        +--tcache_alloc_easy
|     |        |     |  |     |  |        |  如果 tbin->avail 中有元素，则分配成功
|     |        |     |  |     |  |        |  同时 更新 tbin->low_water
|     |        |     |  |     |  |        |
|     |        |     |  |     |  |        +--上一步失败，arena_malloc_large
|     |        |     |  |     |  |        |
|     |        |     |  |     |  |        +--tcache_event
|     |        |     |  |     |  |  
|     |        |     |  |     |  +--tcache为NULL 或者 size > tcache_maxclass
|     |        |     |  |     |     arena_malloc_hard (arena.c)
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
|     |        |     |  |     |        |  |     |  |     +--上一步失败，调用 arena_chunk_alloc (arena.c)
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
|     |        |     |  |     |        |  |     |  |     |  |     |              |  (默认os_overcommit不为0，所以什么都不做)
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
|     |        |     |  |     |        |  |     |  |     |     将该 chunk 的 maxrun 插入 runs_avasl
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
|     |        |     |  |     |        |  |     +--runcur=run
|     |        |     |  |     |        |  |        arena_run_reg_alloc 从 runcur 分配 reg
|     |        |     |  |     |        |  |   
|     |        |     |  |     |        |  +--更新统计数据   
|     |        |     |  |     |        |  |   
|     |        |     |  |     |        |  +--根据 junk 等参数配置本次分配
|     |        |     |  |     |        |  |   
|     |        |     |  |     |        |  +--arena_decay_tick (arena.h)
|     |        |     |  |     |        |     |
|     |        |     |  |     |        |     +--arena_decay_ticks (arena.h)
|     |        |     |  |     |        |        |
|     |        |     |  |     |        |        +--decay_ticker_get
|     |        |     |  |     |        |        |  从 tsd 中拿到属于对应该 arena 的 ticker
|     |        |     |  |     |        |        |  (详细过程见下文)
|     |        |     |  |     |        |        |
|     |        |     |  |     |        |        +-[?] ticker_ticks
|     |        |     |  |     |        |           ticker 到了，返回 true，否则 ticker 减去某个值
|     |        |     |  |     |        |           |
|     |        |     |  |     |        |           Y--arena_purge
|     |        |     |  |     |        |              调用 arena_purge 清理内存(all=false)
|     |        |     |  |     |        |              (详细过程见下文)
|     |        |     |  |     |        |      
|     |        |     |  |     |       [?] size <= large_maxclass
|     |        |     |  |     |        |
|     |        |     |  |     |        Y--arena_malloc_large
|     |        |     |  |     |        |  |
|     |        |     |  |     |        |  +--如果设置了 cache_oblivious,对地址进行随机化
|     |        |     |  |     |        |  |
|     |        |     |  |     |        |  +--arena_run_alloc_large
|     |        |     |  |     |        |  |  |
|     |        |     |  |     |        |  |  +--arena_run_alloc_large_helper
|     |        |     |  |     |        |  |  |  |
|     |        |     |  |     |        |  |  |  +--arena_run_first_best_fit
|     |        |     |  |     |        |  |  |  |
|     |        |     |  |     |        |  |  |  +--arena_run_split_large
|     |        |     |  |     |        |  |  |     |
|     |        |     |  |     |        |  |  |     +--arena_run_split_large_helper
|     |        |     |  |     |        |  |  |        |
|     |        |     |  |     |        |  |  |        +--arena_run_split_remove
|     |        |     |  |     |        |  |  |        |  |
|     |        |     |  |     |        |  |  |        |  +--用 arena_avail_remove 从 runs_avail 中 移除 该run
|     |        |     |  |     |        |  |  |        |  |  如果该run是dirty，用 arena_run_dirty_remove从runs_dirty中移除该run
|     |        |     |  |     |        |  |  |        |  |
|     |        |     |  |     |        |  |  |        |  +--切分run，如果有剩余，则返回 runs_avail 及 runs_dirty
|     |        |     |  |     |        |  |  |        |
|     |        |     |  |     |        |  |  |        +--对分配的 run 初始化并设置一些标记
|     |        |     |  |     |        |  |  |  
|     |        |     |  |     |        |  |  +--上一步分配失败，说明没有可用run
|     |        |     |  |     |        |  |  |  arena_chunk_alloc 分配 chunk
|     |        |     |  |     |        |  |  |
|     |        |     |  |     |        |  |  +-[?] chunk 分配成功
|     |        |     |  |     |        |  |     |
|     |        |     |  |     |        |  |     Y--arena_run_split_large
|     |        |     |  |     |        |  |     |
|     |        |     |  |     |        |  |     N--arena_run_alloc_large_helper
|     |        |     |  |     |        |  |        上述换锁时，可能有其他线程添加了run，再试一次
|     |        |     |  |     |        |  |
|     |        |     |  |     |        |  +--更新统计参数
|     |        |     |  |     |        |  |
|     |        |     |  |     |        |  +--arena_decay_tick
|     |        |     |  |     |        |
|     |        |     |  |     |        N--huge_malloc
|     |        |     |  |     |           |
|     |        |     |  |     |           +--huge_palloc
|     |        |     |  |     |              |
|     |        |     |  |     |              +--ipallocztm
|     |        |     |  |     |              |  为 chunk 的 extent node 分配空间
|     |        |     |  |     |              |
|     |        |     |  |     |              +--arena_chunk_alloc_huge
|     |        |     |  |     |              |  |
|     |        |     |  |     |              |  +--chunk_alloc_cache
|     |        |     |  |     |              |  |
|     |        |     |  |     |              |  +--上一步失败，调用 arena_chunk_alloc_huge_hard
|     |        |     |  |     |              |     |
|     |        |     |  |     |              |     +--chunk_alloc_wrapper
|     |        |     |  |     |              |
|     |        |     |  |     |              +--上一步分配失败，调用 idalloctm 释放 extent node
|     |        |     |  |     |              |  idalloctm 会调用 arena_dalloc 来释放空间
|     |        |     |  |     |              |
|     |        |     |  |     |              +--huge_node_set
|     |        |     |  |     |              |  huge_node_set 会调用 chunk_register 在 基数树中注册
|     |        |     |  |     |              |  如果该步失败，则释放 node、huge chunk
|     |        |     |  |     |              |
|     |        |     |  |     |              +--调用 ql_tail_insert 将 node 插入 arena->huge
|     |        |     |  |     |              |
|     |        |     |  |     |              +--arena_decay_tick
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
|     |        |     |  |     |  为 large size 但是 alignment 大于 page 对齐的分配服务
|     |        |     |  |     |  思路是在头部添加空隙，使得对齐满足要求，然后用新的尺寸申请空间
|     |        |     |  |     |  最后再回收多余的头部、尾部
|     |        |     |  |     |  (具体实现见代码)
|     |        |     |  |     |
|     |        |     |  |    [?] alignment <= chunksize
|     |        |     |  |     |
|     |        |     |  |     Y--huge_malloc
|     |        |     |  |     |
|     |        |     |  |     N--huge_palloc
|     |        |     |  |
|     |        |     |  |
|     |        |     |  +--(arena_metadata_allocated_add : 统计相关，忽略)
|     |        |     |   
|     |        |     +--tcache_arena_associate
|     |        |     |  将 tcache 放入 arena->tcache_ql
|     |        |     |  
|     |        |     +--ticker_init
|     |        |     |  初始化 ticker
|     |        |     |  
|     |        |     +--初始化 tcache->tbins[i].avail，指向各 bin 的 stack 
|     |        | 
|     |        +--tsd_tcache_set
|     |           将 tcache 设置到 tsd 中 
|     |
|     +--iallocztm
|        |
|        +--arena_malloc
|
+--ialloc_post_check

```

```
decay_ticker_get (jemalloc_internal.h)
在线程 tsd 中获取指定 arena 的 ticker (如果没有，就新建一下)
每个线程都为所有 arena 保存有 ticker，但是一般一个线程只用两个 arena
|
+--arena_tdata_get (jemalloc_internal.h)
   |
   +--arenas_tdata = tsd_arenas_tdata_get
   |  获取 tsd 中的 arenas 的 ticker 数组
   |
   +-[?] arenas_tdata == NULL
   |  数组是否为空，为空则需要初始化
   |  |
   |  Y--arena_tdata_get_hard---------------+
   |                                        |
   +-[?] ind >= tsd_narenas_tdata_get       |
   |  要获得的 ticker 超出数组的长度        |
   |  |                                     |
   |  Y--arena_tdata_get_hard---------------+
   |                                        |
   +--arena_tdata_get_hard                  |
             |                              |
             |                              |
             +------------------------------+
             |
             +--如果 ticker 数组太小，就新建数组，并将原数组复制到新数组 
                如果原来没有 ticker 数组，就新建数组并初始化
                (具体实现见源码)
                数组空间的分配/释放使用 a0malloc/a0dalloc
                a0malloc--a0ialloc--iallocztm--arena_malloc 在 arena 0 上分配
                a0dalloc--a0idalloc--idalloctm--arena_dalloc 完成释放
```

```
arena_purge
|
+-[?] all
   |
   Y--arena_purge_to_limit
   |  清除 dirty 的内存地址
   |  两种模式：ratio，decay
   |  ratio: 清除尽可能少的run/chunk使得 arena->ndirty <= ndirty_limit
   |  decay: 清除尽可能多的run/chunk使得 arena->ndirty >= ndirty_limit
   |  |
   |  +--新建 purge_runs_sentinel, purge_chunks_sentinel 链表
   |  |  用来暂存需要释放的 run 和 chunk
   |  |
   |  +--arena_stash_dirty
   |  |  从 runs_dirty,chunks_cache 中获得 dirty run 和 dirty chunk
   |  |  如果是 dirty chunk, 从 chunk_szad/ad_cached 中移除，并放入 purge_chunks_sentinel
   |  |  如果是 dirty run, 使用 arena_run_split_large 分配出来，并放入 purge_runs_sentinel
   |  |  暂存过程中根据 ratio、decay 的条件，选择结束的时机
   |  |  (具体实现见代码)
   |  |
   |  +--arena_purge_stashed
   |  |  如果是 chunk，暂不清理，因为有些 run 依赖 chunk
   |  |  如果是 run，则清理：释放物理地址映射、设置 mapbit 标记
   |  |  (具体实现见代码)
   |  |
   |  +--arena_unstash_purged
   |     对上一步未清理的 chunk 进行实际的清理 (chunk_dalloc_wrapper)
   |     对上一步的 run，调用 arena_run_dalloc 对一些数据结构更新，及后续的清理
   |     (具体实现见代码)
   |
   N--arena_maybe_purge
      |
      +-[?] opt_purge == purge_mode_ratio
         |
         Y--arena_maybe_purge_ratio
         |  根据 lg_dirty_mult 计算需要清理的页面数
         |  根据计算结果决定是否调用 arena_purge_to_limit 清理
         |
         N--arena_maybe_purge_decay
            根据 时钟、decay 参数 计算需要清理的页面数(具体计算过程没有细看)
            根据计算结果决定是否调用 arena_purge_to_limit 清理
         
```








