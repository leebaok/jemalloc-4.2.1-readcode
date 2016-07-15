## 初始化流程
```
jemalloc.c:jemalloc_constructor
使用 gcc 的 constructor 特性将 jemalloc 初始化 在 main 之前执行
|
+--malloc_init 
   初始化 malloc 
   |
   +--malloc_init_hard
   |  |
   |  +--malloc_init_hard_needed
   |  |  如果自己是初始化执行者，返回
   |  |  如果自己不是初始化执行者，等待别人初始化完成
   |  |	
   |  +--malloc_init_hard_a0_locked
   |  |  自己是初始化的执行者，执行初始化
   |  |  a0 指 arena 0，这里指该过程执行初始化直到 arena 0 初始化完成
   |  |  |
   |  |  +--malloc_conf_init
   |  |  |  设置 malloc 的参数，比如 junk 等
   |  |  |
   |  |  +--pages_boot
   |  |  |  设置 mmap_flags
   |  |  |
   |  |  +--base_boot
   |  |  |  初始化 base_avail_szad 为 size-address-ordered 红黑树
   |  |  |
   |  |  +--chunk_boot
   |  |  |  |
   |  |  |  +--初始化 chunk 参数
   |  |  |  |  如chunksize、chunksize_mask、chunk_npages 
   |  |  |  |
   |  |  |  +--调用 chunk_dss_boot 初始化 sbrk 状态
   |  |  |  |
   |  |  |  +--rtree_new 初始化 chunk_rtree
   |  |  |     将 chunks_rtree_node_alloc 作为 rtree 的内存分配器
   |  |  |     chunks_rtree_node_alloc 使用 base_alloc 完成内存分配
   |  |  |
   |  |  +--ctl_boot
   |  |  |  ???
   |  |  |
   |  |  +--arena_boot
   |  |  |  |
   |  |  |  +--设置 dirty_mult、decay_time 参数
   |  |  |  |  这两个参数用来指导维护 active pages、dirty pages 平衡
   |  |  |  | 
   |  |  |  +--通过3次迭代确定 chunk_header 大小
   |  |  |  |  并确定 map_bias,arena_maxrun,nlclasses,nhclasses等
   |  |  |  |
   |  |  |  +--bin_info_init
   |  |  |  |  |
   |  |  |  |  +--初始化 arena_bin_info 
   |  |  |  |  |  使用 size_classes 及 BIN_INFO_INIT_bin_yes 初始化
   |  |  |  |  |  只初始化 small bin
   |  |  |  |  |  small bin 从 run 的 region 分配
   |  |  |  |  |  large 直接使用 run (multi pages) 分配
   |  |  |  |  |
   |  |  |  |  +--bin_info_run_size_calc
   |  |  |  |  |  为 small bin 计算合适的 run size，一个 run 由多个
   |  |  |  |  |  page 组成，正好可以切成 整数个 bin 大小的 region
   |  |  |  |  |  比如，arena_bin_info[3] reg_size=48,run_size=12288,
   |  |  |  |  |  nregs=256, 该run就是由3个page组成
   |  |  |  |  |
   |  |  |  |  +--bitmap_info_init
   |  |  |  |     计算每个 small bin 的 bitmap info 
   |  |  |  |
   |  |  |  +--small_run_size_init
   |  |  |  |  使用 base_alloc 为 small_run_tab 分配内存
   |  |  |  |  small_run_tab 记录着 多少个page 可以组成一个 run 
   |  |  |  |  或者说，一个真实的 run 可能由多少page 组成
   |  |  |  |  (一个真实的 run 可以切成整数个 region)
   |  |  |  |
   |  |  |  +--run_quantize_init
   |  |  |  |  计算 run_quantize_ceil_tab, run_quantize_floor_tab
   |  |  |  |  run_quantize_ceil_tab[i] 记录比 i 个 page 多的最小的真实 run
   |  |  |  |  run_quantize_floor_tab[i] 记录比 i 个 page 少的最大的真实 run
   |  |  |  |
   |  |  |  +--确定 runs_avail_nclasses
   |  |  |     runs_avail_nclasses = size2index(maxrun) + 1 - size2index(PAGE)
   |  |  |     将 run 按照 size2index 对齐，虽然会浪费一些空间
   |  |  |     但是，对齐后的 run 既可以满足本次分配，后续释放后还可以用来
   |  |  |             当作 large run 再分配
   |  |  |     而且，为了让浪费更少，每次分配会将多余的部分再重新放回来
   |  |  |
   |  |  +--tcache_boot
   |  |  |  |
   |  |  |  +--确定 tcache_maxclass (tcache_maxclass 是 tcache 最大的 size)
   |  |  |  |  确定 nhbins (tcache 中 bin 的种数)
   |  |  |  |  (NBINS 是 small bin 的种数，我的环境下：NBINS=36，nhbins=41)
   |  |  |  |
   |  |  |  +--初始化 tcache_bin_info
   |  |  |     使用 base_alloc 为 tcache_bin_info 分配空间
   |  |  |     确定每种 tcache bin 本地缓存的数量 ncached_max
   |  |  |     确定所有 tcache bin 缓存的总数 stack_nelms
   |  |  |
   |  |  +--初始化 arenas 数组
   |  |  |  初始 arenas 数组中只有一个元素 a0 : arena 0
   |  |  |
   |  |  +--arena_init (jemalloc.c)
   |  |  |  初始化 0 号 arena, arena[0] 初始时候不绑定 tsd/thread
   |  |  |  初始化流程只初始化 arena[0]，更多的 arena 在用的时候再初始化
   |  |  |  |
   |  |  |  +--arena_init_locked
   |  |  |     |
   |  |  |     +--确定该 arena 还未初始化
   |  |  |     |
   |  |  |     +--arena_new
   |  |  |        |
   |  |  |        +--计算 arean_size
   |  |  |        |  需要动态计算 runs_avail 的长度
   |  |  |        |  
   |  |  |        +--使用 base_alloc 申请 arena 空间
   |  |  |        |  
   |  |  |        +--初始化 arena 的 nthreads 和 统计数据
   |  |  |        |  
   |  |  |        +--初始化 arena->achunks (单链表)
   |  |  |        |  
   |  |  |        +--初始化 arena->runs_avail[i] (每个 runs_avail 为 heap)
   |  |  |        |  
   |  |  |        +--初始化 arena->runs_dirty (双向链表)  
   |  |  |        |  
   |  |  |        +--初始化 arena->chunks_cache (双向链表)
   |  |  |        |  
   |  |  |        +--(arena_decay_init, 默认使用 purge ratio,所以不执行) 
   |  |  |        |  
   |  |  |        +--初始化 arena->huge (单链表)
   |  |  |        |  
   |  |  |        +--初始化 arena->chunks_szad_cached  
   |  |  |        |  初始化 arena->chunks_ad_cached  
   |  |  |        |  初始化 arena->chunks_szad_retained 
   |  |  |        |  初始化 arena->chunks_ad_retained
   |  |  |        |  (均为红黑树，节点为 extent_node)
   |  |  |        |  
   |  |  |        +--初始化 arena->node_cache (单链表)  
   |  |  |        |  
   |  |  |        +--初始化 arena->chunk_hooks 为 默认的chunk hooks default  
   |  |  |        |  
   |  |  |        +--初始化 arena->bins 
   |  |  |           初始化每个 arena->bins[i] 的 lock, runcur, runs(heap)
   |  |  |
   |  |  +--设置 malloc 状态为 malloc_init_a0_initialized
   |  |
   |  +--malloc_tsd_boot0
   |  |  |
   |  |  +--tsd_boot0
   |  |  |  tsd_boot0 是通过 malloc_tsd_protos 生成的
   |  |  |  通过 pthread_key_create 生成 thread specific data
   |  |  |  tcache指针 等数据就存放在 tsd 中
   |  |  |
   |  |  +--tsd_fetch
   |  |  |  当 tsd 状态为 unitialized 时，fetch会将 tsd 的状态置为 nominal 
   |  |  |
   |  |  +--设置 tsd_arenas_tdata_bypass 为 true
   |  |     tsd 的 arenas_tdata 是 ticker 的计数值
   |  |     但是，arenas_tdata_bypass 是 ??? 
   |  |
   |  +--malloc_init_hard_recursible
   |  |  |
   |  |  +--设置 malloc 状态为 malloc_init_recursible
   |  |  |
   |  |  +--pthread_atfork
   |  |     通过 pthread_atfork 设置 prefork, postfork 的 hook
   |  |     从而减少 multi-thread 及 fork 情况下的 deadlock
   |  |
   |  +--malloc_init_hard_finish
   |  |  |
   |  |  +--根据 cpu 核数重新调整 arena 的个数
   |  |  |
   |  |  +--使用 base_alloc 为 arenas 数组重新分配内存
   |  |  |
   |  |  +--设置 malloc 状态为 malloc_init_intialized
   |  |  |
   |  |  +--malloc_slow_flag_init
   |  |     设置 malloc_slow 标志，大多数情况下为 false,即 fast path
   |  |
   |  +--malloc_tsd_boot1
   |     |
   |     +--tsd_boot1 : do nothing
   |     |
   |     +--设置 tsd_arenas_tdata_bypass 为 false, ???
   |
   +--malloc_thread_init : do nothing

```

config_cache_oblirvious 随机化地址，保证cache对齐，但是带有随机化 ???
