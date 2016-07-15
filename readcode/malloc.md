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
|     |        |     |  |     |        |  |     |  |     |     |         runs_dirty 中删去该 run 的 map_misc
|     |        |     |  |     |        |  |     |  |     |     |     因为 runs_dirty 是一个双向环形链表
|     |        |     |  |     |        |  |     |  |     |     |     删除的时候将该run自己的map_misc的指针
|     |        |     |  |     |        |  |     |  |     |     |         进行修改就可以完成在链表中删除自己
|     |        |     |  |     |        |  |     |  |     |     |
|     |        |     |  |     |        |  |     |  |     |     +--初始化 run 的 mapbits
|     |        |     |  |     |        |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     +--上一步失败，调用 arean_chunk_alloc
|     |        |     |  |     |        |  |     |  |     |  |
|     |        |     |  |     |        |  |     |  |     |  +--TODO
|     |        |     |  |     |        |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     |
|     |        |     |  |     |        |  |     |  |     +-[?] chunk 分配成功
|     |        |     |  |     |        |  |     |  |        |  chunk 分配成功初始化的时候，会自动有一个maxrun
|     |        |     |  |     |        |  |     |  |        |
|     |        |     |  |     |        |  |     |  |        Y--arena_run_split_small
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
