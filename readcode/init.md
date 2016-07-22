## 初始化流程
jemalloc 在使用之前会先初始化，初始化基本就是初始化在数据结构一章解释的数据结构 以及
一些其他相关的初始化工作，下面给出整个初始化的执行流程：
```
jemalloc_constructor (jemalloc.c)
使用 gcc 的 constructor 特性将 jemalloc 初始化 在 main 之前执行
|
+--malloc_init (jemalloc.c)
   初始化 malloc
   |
   +--malloc_init_hard (jemalloc.c)
   |  |
   |  +--malloc_init_hard_needed (jemalloc.c)
   |  |  如果自己是初始化执行者，返回
   |  |  如果自己不是初始化执行者，等待别人初始化完成
   |  |
   |  +--malloc_init_hard_a0_locked (jemalloc.c)
   |  |  自己是初始化的执行者，执行初始化
   |  |  a0 指 arena 0，这里指该过程执行初始化直到 arena 0 初始化完成
   |  |  |
   |  |  +--malloc_conf_init (jemalloc.c)
   |  |  |  设置 malloc 的参数，比如 junk 等
   |  |  |
   |  |  +--pages_boot (pages.c)
   |  |  |  获取 overcommit 参数
   |  |  |  设置 mmap_flags
   |  |  |
   |  |  +--base_boot (base.c)
   |  |  |  初始化 base_avail_szad 为 size-address-ordered 红黑树
   |  |  |
   |  |  +--chunk_boot (chunk.c)
   |  |  |  |
   |  |  |  +--初始化 chunk 参数
   |  |  |  |  如chunksize、chunksize_mask、chunk_npages
   |  |  |  |
   |  |  |  +--调用 chunk_dss_boot 初始化 sbrk 状态 (chunk_dss.c)
   |  |  |  |
   |  |  |  +--rtree_new 初始化 chunk_rtree (rtree_new 在 rtree.c 中)
   |  |  |     将 chunks_rtree_node_alloc 作为 rtree 的内存分配器
   |  |  |     chunks_rtree_node_alloc 使用 base_alloc 完成内存分配
   |  |  |
   |  |  +--ctl_boot (ctl.c)
   |  |  |  ???
   |  |  |
   |  |  +--arena_boot (arena.c)
   |  |  |  |
   |  |  |  +--设置 dirty_mult、decay_time 参数
   |  |  |  |  这两个参数用来指导维护 active pages、dirty pages 平衡
   |  |  |  |
   |  |  |  +--通过3次迭代确定 chunk_header 大小
   |  |  |  |  并确定 map_bias,arena_maxrun,nlclasses,nhclasses等
   |  |  |  |
   |  |  |  +--bin_info_init (arena.c)
   |  |  |  |  |
   |  |  |  |  +--初始化 arena_bin_info
   |  |  |  |  |  使用 size_classes 及 BIN_INFO_INIT_bin_yes 初始化
   |  |  |  |  |  只初始化 small bin
   |  |  |  |  |  ( small bin 从 run 的 region 分配
   |  |  |  |  |    large 直接使用 run (multi pages) 分配 )
   |  |  |  |  |
   |  |  |  |  +--bin_info_run_size_calc (arena.c)
   |  |  |  |  |  为 small bin 计算合适的 run size，一个 run 由多个
   |  |  |  |  |  page 组成，正好可以切成 整数个 bin 大小的 region
   |  |  |  |  |  比如，arena_bin_info[3] reg_size=48,run_size=12288,
   |  |  |  |  |  nregs=256, 该run就是由3个page组成
   |  |  |  |  |
   |  |  |  |  +--bitmap_info_init (bitmap.c)
   |  |  |  |     计算每个 small bin 的 bitmap info
   |  |  |  |
   |  |  |  +--small_run_size_init (arena.c)
   |  |  |  |  使用 base_alloc 为 small_run_tab 分配内存
   |  |  |  |  small_run_tab 记录着 多少个page 可以组成一个 run
   |  |  |  |  或者说，一个真实的 run 可能由多少page 组成
   |  |  |  |  (一个真实的 run 可以切成整数个 region)
   |  |  |  |
   |  |  |  +--run_quantize_init (arena.c)
   |  |  |  |  使用 base_alloc 为 run_quantize_ceil/floor_tab 分配空间
   |  |  |  |  计算 run_quantize_ceil_tab, run_quantize_floor_tab
   |  |  |  |  run_quantize_ceil_tab[i] 记录比 i 个 page 多的最小的真实 run
   |  |  |  |  run_quantize_floor_tab[i] 记录比 i 个 page 少的最大的真实 run
   |  |  |  |  (真实的 run 分为两种：small run 和 large run，对于 small run，
   |  |  |  |   其真实 run 大小就是 small run 的大小，对于 large run，其
   |  |  |  |   真实 run 的大小是 run_size+large_pad，默认 large_pad=PAGE)
   |  |  |  |
   |  |  |  +--确定 runs_avail_nclasses
   |  |  |     runs_avail_nclasses = size2index(maxrun) + 1 - size2index(PAGE)
   |  |  |     将 run 按 size2index 分类，将某一范围内的 run 放在一个 runs_avail 中
   |  |  |     实际分配时，将 run 多余请求的部分再重新放回 runs_avail
   |  |  |
   |  |  +--tcache_boot (tcache.c)
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
   |  |  |  +--arena_init_locked (jemalloc.c)
   |  |  |     |
   |  |  |     +--确定该 arena 还未初始化
   |  |  |     |
   |  |  |     +--arena_new (arena.c)
   |  |  |        |
   |  |  |        +--计算 arena_size
   |  |  |        |  需要动态计算 runs_avail 的长度
   |  |  |        |  
   |  |  |        +--使用 base_alloc 申请 arena 空间
   |  |  |        |  
   |  |  |        +--初始化 arena 的 nthreads 和 统计数据
   |  |  |        |  
   |  |  |        +--初始化 arena->achunks (单链表)
   |  |  |        |  achunks 记录正在占用的 chunks
   |  |  |        |  
   |  |  |        +--初始化 arena->runs_avail[i] (每个 runs_avail 是一个 堆)
   |  |  |        |  每个 runs_avail 使用 heap 维护该类可用的 runs
   |  |  |        |  
   |  |  |        +--初始化 arena->runs_dirty (双向链表)  
   |  |  |        |  runs_dirty 链接 dirty runs (同时将 dirty chunks 链进来)
   |  |  |        |  
   |  |  |        +--初始化 arena->chunks_cache (双向链表)
   |  |  |        |  chunks_cache 链接 dirty chunks/huges
   |  |  |        |  
   |  |  |        +--(arena_decay_init, 默认使用 purge ratio,所以不执行)
   |  |  |        |  
   |  |  |        +--初始化 arena->huge (单链表)
   |  |  |        |  huge 用来链接正在使用的 huge
   |  |  |        |  
   |  |  |        +--初始化 arena->chunks_szad_cached  
   |  |  |        |  初始化 arena->chunks_ad_cached  
   |  |  |        |  初始化 arena->chunks_szad_retained
   |  |  |        |  初始化 arena->chunks_ad_retained
   |  |  |        |  ( 均为红黑树，节点为 extent_node，chunks_szad/ad_cached
   |  |  |        |    管理 dirty chunks，即 有物理内存映射的，这两颗树管理同
   |  |  |        |    一组 chunks，使用 extent_node 中的不同边来链接，完成复杂
   |  |  |        |    管理。chunks_szad/ad_retained 管理 没有物理内存映射的
   |  |  |        |    chunks 地址空间。 szad:size-address, ad:address )
   |  |  |        |  
   |  |  |        +--初始化 arena->node_cache (单链表)  
   |  |  |        |  node_cache 缓存使用 base_alloc 申请的 extent node 空间
   |  |  |        |  
   |  |  |        +--初始化 arena->chunk_hooks 为 默认的chunk hooks default  
   |  |  |        |  
   |  |  |        +--初始化 arena->bins
   |  |  |           初始化每个 arena->bins[i] 的 lock, runcur, runs(heap)
   |  |  |
   |  |  +--设置 malloc 状态为 malloc_init_a0_initialized
   |  |
   |  +--malloc_tsd_boot0 (tsd.c)
   |  |  |
   |  |  +--tsd_boot0 (tsd.h)
   |  |  |  tsd_boot0 是通过 malloc_tsd_protos 生成的
   |  |  |  通过 pthread_key_create 生成 thread specific data
   |  |  |  tcache指针 等数据就存放在 tsd 中
   |  |  |
   |  |  +--tsd_fetch (tsd.h)
   |  |  |  当 tsd 状态为 unitialized 时，fetch会将 tsd 的状态置为 nominal
   |  |  |
   |  |  +--设置 tsd_arenas_tdata_bypass 为 true
   |  |     tsd 的 arenas_tdata 是 ticker 的计数值
   |  |     但是，arenas_tdata_bypass 是 ???
   |  |
   |  +--malloc_init_hard_recursible (jemalloc.c)
   |  |  |
   |  |  +--设置 malloc 状态为 malloc_init_recursible
   |  |  |
   |  |  +--pthread_atfork
   |  |     通过 pthread_atfork 设置 prefork, postfork 的 hook
   |  |     从而减少 multi-thread 及 fork 情况下的 deadlock
   |  |
   |  +--malloc_init_hard_finish (jemalloc.c)
   |  |  |
   |  |  +--根据 cpu 核数重新调整 arena 的个数
   |  |  |
   |  |  +--使用 base_alloc 为 arenas 数组重新分配内存
   |  |  |
   |  |  +--设置 malloc 状态为 malloc_init_intialized
   |  |  |
   |  |  +--malloc_slow_flag_init (jemalloc.c)
   |  |     设置 malloc_slow 标志，大多数情况下为 false,即 fast path
   |  |
   |  +--malloc_tsd_boot1 (tsd.c)
   |     |
   |     +--tsd_boot1 : do nothing
   |     |
   |     +--设置 tsd_arenas_tdata_bypass 为 false, ???
   |
   +--malloc_thread_init (jemalloc.c): do nothing
```

至于上述流程中每一个函数详细的执行内容，可以阅读源码，我们在源码中重要的地方都加了
注释，阅读起来应该难度不大。

不过这里还是需要对上述流程中的一些地方做一些解释：
* base_alloc

jemalloc 本身是做内存分配的，然而 jemalloc 自己的数据结构也需要分配内存来存放，
而此时 jemalloc 还没有初始化完成，所以 jemalloc 中提供了一个 base 分配器，用来
做十分简单的分配工作，jemalloc 启动时的数据结构就是使用 base 完成分配的。base
分配器的代码在 base.c 中，代码功能十分简单，很容易看懂，这里不做过多解释。

* pthread_atfork

jemalloc 使用 pthread_atfork 绑定 prefork、postfork 的 hook function 是为了
减少 多线程、多进程 情况下出现的 deadlock，如果不使用 prefork、postfork
的 hook function，下面的情况就会出现死锁：
```
                           malloc-lock
                                |
            +---- thread-1 ----<1>--------------------    
            +---- thread-2 ---------------------------  +--------+
    main ---+-- main thread ---(a)--<2>---(b)---------  | Parent |
                                     |                  +--------+
    ---------------------------------|----------------------------
                                     +-- child --(c)--  +--------+
                                                        | Child  |
                                                        +--------+
```
**问题：**
父进程有多个线程，thread-1 在<1>处持有 malloc-lock 锁，而 main thread
在 <2> 处 fork 一个子进程，这个子进程会复制父进程中 main thread 的内存空间，
同时也将被 thread-1 持有的 malloc-lock 复制进来，而子进程中没有 thread-1，
该 malloc-lock 在子进程中存在而永远不会被释放，从而导致死锁。(fork 只复制
调用 fork 的线程，不会将父进程所有线程都复制过来)

**解决方法：**
main thread 在 fork 之前，即(a)处，获取所有锁，然后在 fork 之后，父进程
在 (b) 处，子进程在 (c) 处分别再释放所有锁。这就是 atfork 绑定 hook function
所做的事情。

* thread specific data

jemalloc 使用 pthread_key_create 为每个线程生成私有的存储空间，该 api 只
需要调用一次，所有线程都会拥有该同名的数据结构，而且线程独立访问，互不干扰。
在 jemalloc 中，每个线程的计时器、tcache指针就是存放在 TSD 中。

* 其他

流程中还有一些部分需要花点时间学习、思考的，比如 radix tree(基数树)、很多计算
过程具体的计算步骤 等等，由于我也没有看得特别详细，所以就不做过多解释。
