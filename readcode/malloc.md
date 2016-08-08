## malloc 流程
这部分主要讲解 jemalloc 的 malloc 过程，先来看一下 malloc 的整体流程：(其中会涉及
很多子过程，子过程会在后面一一介绍)
```
je_malloc (jemalloc.c)
jemalloc 已经初始化过，malloc_slow 正常情况为 false
|
+--ialloc_body (jemalloc.c)
|  |
|  +--tsd_fetch (tsd.h)
|  |  获取 tsd ( thread specific data )
|  |
|  +--根据 size 获得 ind
|  |
|  +--ialloc (jemalloc.c)
|     |
|     +--tcache_get (tsd.h)
|     |  从 tsd 中获取 tcache (具体内容见下文)
|     |
|     +--iallocztm (jemalloc.c)
|        |
|        +--arena_malloc (arena.h)
|           malloc 的主体函数
|           |
|           +-[?] tcache!=NULL
|           |  |
|           |  Y--+--size<=SMALL_MAXCLASS
|           |     |  tcache_alloc_small (tcache.h)
|           |     |
|           |     +--size<=tcache_maxclass
|           |        tcache_alloc_large (tcache.h)
|           |
|           +--tcache为NULL 或者 size > tcache_maxclass
|              arena_malloc_hard (arena.c)
|              |
|              +--arena_choose (jemalloc_internal.h)
|              |  获取该线程所属的 arena
|              |
|              +-[?] size <= SMALL_MAXCLASS
|                 |
|                 Y--arena_malloc_small (arena.c)
|                 |  从 arena 中分配 small bin (具体内容见下文)
|                 |
|                [?] size <= large_maxclass
|                 |
|                 Y--arena_malloc_large (arena.c)
|                 |  从 arena 中分配 large (具体内容见下文)
|                 |
|                 N--huge_malloc (huge.c)
|                    分配 huge (具体内容见下文)
|   
+--ialloc_post_check (jemalloc.c)
   做相关检查及统计数据更新
```
简单来说，malloc 流程就是根据 size 大小使用不同方式分配(下面流程是默认开启 tcache 的工作过程)：
* small bin ： 从 tcache 分配，如果 tcache 没有，则 tcache 从 arena 获取元素填充 tcache，
然后再从 tcache 获取
* large <= tcache_maxclass ： 优先从 tcache 分配，如果 tcache 没有，则从 arena 分配
* large > tcache_maxclass ： 从 arena 分配
* huge ： 使用 huge_malloc 分配

这里需要说明的是，jemalloc 的 tcache 是可以关闭的，将 tcache 关闭的话，那么 tcache 一直为 NULL，
则 small、large、huge 分别走 arena_malloc_small、arena_malloc_large、huge_malloc 接口。
(下文内容大部分都是针对默认开启 tcache 的情况讲述的)

上述流程中，将 arena_malloc 的执行流程也一起写进去了，下面很多过程会用到 arena_malloc，
可以参照上述内容。

上述过程中 tcache_get 大部分情况下过程很简单，但是如果 tcache 还没初始化，那么过程
就会很复杂，具体过程如下：
```
tcache_get (tcache.h)
|
+--tsd_tcache_get (tsd.h)
|  从 tsd 中获取 tcache (该函数是使用 宏 生成的)
|
+-[?] tcache==NULL 及 tsd 状态为 nominal
   |
   Y--+--tcache_get_hard (tcache.c)
      |  |
      |  +--arena_choose (jemalloc_internal.h)
      |  |  选择 application arena
      |  |  |
      |  |  +--arena_choose_impl (jemalloc_internal.h)
      |  |     |
      |  |     +--tsd_arena_get
      |  |     |
      |  |     +-[?] arena==NULL
      |  |        |
      |  |        Y--arena_choose_hard (jemalloc.c)
      |  |           为该线程选取 arena (具体内容见下文)
      |  |
      |  +--tcache_create (tsd.c)
      |     为 tcache 分配空间并初始化
      |     (调用 ipallocztm 时，传入参数 arena=arena[0], is_metadata=true，
      |      所以，在 arena 0 上为 tcache 分配空间)
      |     |
      |     +--计算 tcache 的大小，包括 tcache_bin 和 bin 的 stack elements
      |     |
      |     +--ipallocztm (jemalloc_internal.h)
      |     |  为 tcache 分配空间
      |     |  
      |     +--tcache_arena_associate
      |     |  将 tcache 放入 arena->tcache_ql
      |     |
      |     +--ticker_init
      |     |  初始化 ticker
      |     |
      |     +--初始化 tcache->tbins[i].avail，指向各 bin 的 stack
      |
      +--tsd_tcache_set
         将 tcache 设置到 tsd 中
```
tcache 创建过程中使用了 ipallocztm 为 tcache 分配空间，ipallocztm 可以提供有指定对齐
需求的内存分配，而 iallocztm 则提供没有特殊对齐需求的内存分配，下面给出 ipallocztm 的
执行流程：
```
ipallocztm (jemalloc_internal.h)
提供有指定对齐需求的内存分配
|
+--arena_palloc (arena.c)
|  提供有指定对齐需求的内存分配，比如大于页对齐的内存分配
|  (然而并不严格满足对齐需求，如 small 时，如果对齐需求小于 page，则不考虑对齐需求)
|  |
|  +-[?] usize <= SMALL_MAXCLASS 且 对齐要求不大于 PAGE
|     |
|     Y--arena_malloc (arena.h)
|     |  (具体内容见上文)
|     |
|    [?] usize <= large_maxclass & alignment <= PAGE
|     |
|     Y--+--arena_malloc (arena.h)
|     |  |  (具体内容见上文)
|     |  |
|     |  +--如果设置了 config_cache_oblivious，正则一下地址
|     |
|    [?] usize <= large_maxclass
|     |
|     Y--arena_palloc_large
|     |  为 large size 但是 alignment 大于 page 对齐的请求分配内存
|     |  思路是在头部添加空隙，使得对齐满足要求，然后用新的尺寸申请空间
|     |  最后再回收多余的头部、尾部
|     |  (具体实现见代码)
|     |
|    [?] alignment <= chunksize
|     |
|     Y--huge_malloc
|     |  (具体内容见下文)
|     |
|     N--huge_palloc
|        (具体内容见下文)
|
+--arena_metadata_allocated_add (arena.h)
   更新统计参数
```

下面来看从 tcache 中分配 small 的过程，先看一个简易的流程图:
![allocate small from tcache](pictures/tcache-small-alloc.png)
从上图只简要列出了从 tcache 分配 small 的重要步骤，下面做一些解释：
* 步骤一：从 tcache 中获取该 bin，如果 tcache 中有，申请成功，不执行下面的步骤；如果
tcache 中没有，那么执行步骤二
* 步骤二：从 arena 的 bin 中获得元素填充 tcache，如果 arena 中有足够的元素填充，
那么填充之后返回步骤一进行分配；如果 arena 的 bin 中没有足够的元素，那么触发步骤三
* 步骤三：从 arena 的 runs_avail 中找到满足的 run，分配给 arena 的 bin，如果
runs_avail 能找到，那么返回步骤二(找到比该small run大的也可以，jemalloc中会做切分)；
如果找不到，那么触发 步骤四、步骤五
* 步骤四、五：申请一个新的 chunk，并将新 chunk 切成合适的 run 放入 runs_avail 中，
返回步骤三

上述流程很好地体现了 jemalloc 的分层设计，首先从 tcache 中取，其次从 arena 中取，
最后从操作系统申请新的块，这样的分层设计配合逐层缓存，使得大部分时候分配很迅速、高效。

现在来看看上述流程的详细解释：
```
tcache_alloc_small (tcache.h)
从 tcache 中分配 small bin
|
+--tcache_alloc_easy (tcache.h)
|  如果 tbin->avail 中有元素，则分配成功
|  同时 更新 tbin->low_water
|
+--上一步失败
|  tcache_alloc_small_hard (tcache.c)
|  |
|  +--arena_tcache_fill_small (arena.c)
|  |  从 arena 中获取内存填充 tcache
|  |  |
|  |  +--根据 tbin->lg_fill_div, tbin->ncached_max 计算需要填充的数量
|  |  |
|  |  +--重复调用 arena_run_reg_alloc,arena_bin_malloc_hard填充 tbin
|  |  |  arena_run_reg_alloc 从 runcur 获取 region (具体内容见下文)
|  |  |  arena_bin_malloc_hard寻找可用的 run 来替补 runcur (具体内容见下文)
|  |  |
|  |  +--arena_decay_tick (arena.h)
|  |     更新 ticker，并可能触发 arena_purge 内存清理
|  |
|  +--tcache_alloc_easy (tcache.h)
|
+--tcache_event (tcache.h)
   更新 ticker，并可能出发 tcache_event_hard 对 bin 进行回收
   (具体内容见下文)
```
上述流程中，arena_run_reg_alloc 的过程很简洁，就是根据 bitmap 来获得 region，
如果当前 run 中没有可用的 region，那么使用 arena_bin_malloc_hard 从 bin->runs
中选出一个run 来分配，如果 bin->runs 中也没有可用的，那么会触发 arena_run_alloc_small，
下面给出 arena_bin_malloc_hard 的流程：
```
arena_bin_malloc_hard (arena.c)
获取 run 填充 runcur，然后再分配
|
+--run = arena_bin_nonfull_run_get (arena.c)
|  |
|  +--arena_bin_nonfull_run_tryget (arena.c)
|  |  从 bin->runs 中尝试获得一个 run
|  |
|  +-[?] 上一步 tryget 失败
|  |  |
|  |  Y--arena_run_alloc_small (arena.c)
|  |     从 arena 中分配 run 给该 bin (具体内容见下文)
|  |
|  +--如果上一步 arena_run_alloc_small 也失败了
|     再尝试一次 arena_bin_nonfull_run_tryget
|     (因为中间有换锁，可能有其他线程填充了runs)
|
+--如果其他线程填充了 runcur
|  |
|  +--arena_run_reg_alloc 从 runcur 分配 reg
|  |
|  +--如果 run 是满的，用 arena_dalloc_bin_run 回收
|     否则调用 arena_bin_lower_run 将 run、runcur
|           中地址低的变成新的 runcur，另一个放回 runs
|
+--runcur=run
   arena_run_reg_alloc 从 runcur 分配 reg
```
上述过程会触发 arena_run_alloc_small，下面给出该过程的流程：
```
arena_run_alloc_small (arena.c)
从 arena 中分配合适的 run 给某个 bin
|
+--arena_run_alloc_small_helper (arena.c)
|  |
|  +--arena_run_first_best_fit (arena.c)
|  |  |
|  |  +--size->run_quantize_ceil->size2index
|  |  |  先将 size 转换成 真实 run 应该有的大小
|  |  |  再转换成 index，从而映射成 runs_avail 下标
|  |  |
|  |  +--使用 arena_run_heap_first 在 arena->runs_avail 中找到合适的run
|  |
|  +--arena_run_split_small (arena.c)
|     |
|     +--获得一些参数
|     |
|     +--如果该 run 内存被 decommitted，调用 chunk_commit_default 将内存 commit
|     |
|     +--arena_run_split_remove (arena.c)
|     |  切分出需要的 run，将剩余的空间组成新的 run 再放回 runs_avail
|     |  |
|     |  +--arena_avail_remove
|     |  |  从 runs_avail 中移除该 run
|     |  |  此处使用 run_quantize_floor 去调整
|     |  |  因为实际 run 的尺寸会大于其所在 ind的尺寸
|     |  |
|     |  +--如果是dirty，则用 arena_run_dirty_remove从
|     |  |      runs_dirty 中删去该 run 的 map_misc
|     |  |  因为 runs_dirty 是一个双向环形链表
|     |  |  删除的时候将该run自己的map_misc的指针
|     |  |      进行修改就可以完成在链表中删除自己
|     |  |
|     |  +--arena_avail_insert
|     |     将多余的页面返回到 arena->runs_avail
|     |     多余的页面使用 run_quantize_floor 确定 ind
|     |
|     +--初始化 run 的 mapbits
|
+--上一步失败，调用 arena_chunk_alloc (arena.c)
|  (上一步失败，说明没有可用的 run，需要申请新的 chunk)
|
+-[?] chunk 分配成功
   |  chunk 分配成功初始化的时候，会自动有一个maxrun
   |
   Y--arena_run_split_small (见上面的流程)
   |
   N--其他线程可能给该 arena 分配了 chunk
      再试一次arena_run_alloc_small_helper
```
上述过程中会从 runs_avail 中寻找可用的run，从该run中切出需要的部分，剩下的放回 runs_avail，
如果 runs_avail 中也没有可用的，那么会触发 chunk 的分配。

对于 chunk，需要指出的是在 jemalloc 中，chunk 不仅仅是
chunksize 大小的 chunk，大于 chunksize 的 huge 也认为是 chunk。根据我的观察，大部分
arena_chunk_* 形式的函数是操作 chunksize 的 chunk，即用来切分成 run 的 arena chunk，
而 chunk_* 形式的函数大部分是不加区分的操作 chunk 和 huge，即用来操作大内存。
所以，之后文中也将用来切分成 run 的 chunk 称为 arena chunk，而 chunk 则用来不加区分的
形容 chunk 和 huge。

下面看看 chunk 的分配：
```
arena_chunk_alloc (arena.c)
分配 arena chunk
|
+-[?] arena->spare != NULL
|  |  spare 中记录着上次释放的 chunk
|  |
|  Y--arena_chunk_init_spare (arena.c)
|  |  将 arena->spare 作为新 chunk，并将 spare 置为 NULL
|  |
|  N--arena_chunk_init_hard (arena.c)
|     |
|     +--arena_chunk_alloc_internal (arena.c)
|     |  |
|     |  +--chunk_alloc_cache
|     |  |  使用 chunk_recycle 从 chunks_szad_cache/chunks_ad_cache 中分配 chunk
|     |  |  (chunk_recycle 的具体内容见下文)
|     |  |
|     |  +-[?] 上一步分配成功
|     |     |
|     |     Y--+--arena_chunk_register (arena.c)
|     |     |  |  |
|     |     |  |  +--extent_node_init 初始化 chunk->node
|     |     |  |  |
|     |     |  |  +--chunk_register
|     |     |  |     将 chunk 在 rtree 中登记
|     |     |  |     rtree--radix tree--基数树
|     |     |  |
|     |     |  +--注册失败，调用chunk_dalloc_cache
|     |     |     (chunk_dalloc_cache 具体内容在 free 部分)
|     |     |
|     |     N--arena_chunk_alloc_internal_hard (arena.c)
|     |        |
|     |        +--chunk_alloc_wrapper (chunk.c)
|     |        |  实际分配 chunk 空间
|     |        |
|     |        +--如果 chunk 的地址未 commit(false)，则尝试 commit 其地址
|     |        |  如果 commit 失败，则调用 chunk_dalloc_wrapper 释放 chunk，并返回
|     |        |  (chunk_dalloc_wrapper 具体内容见下文)
|     |        |  (如果操作系统是 overcommit 的，上一步chunk_alloc_wrapper
|     |        |   会置 commit 为 true)
|     |        |
|     |        +-[?] arena_chunk_register 成功
|     |           |
|     |           N--chunk_dalloc_wrapper (chunk.c)
|     |              释放 chunk (具体内容见下文)
|     |
|     +--调用 arena_mapbits_unallocated_set
|        初始化 chunk 的 mapbits
|
+--ql_tail_insert
|  将该 chunk 插入到 arena->achunks
|
+--arena_avail_insert
   将该 chunk 的 maxrun 插入 runs_avail
```
上述流程还是比较清晰的，不过这里需要做一些说明：
* 结合前面的数据结构中的说明，chunks_szad/ad_cache 中是 dirty chunks，即有物理地址映射
的 chunks； chunks_szad/ad_retained 中是 clean chunks，即只有地址空间，没有物理地址
* chunks_szad/ad_* 是通过 extent node 链接的，对于 arena chunk，node 在其内部，对于
huge chunk，node 在 chunk 外面
* 如果操作系统是 overcommit 的，那么上述关于 commit 的操作都可以忽略。如果操作系统没有
设置 overcommit，那么在需要的时候会调用 commit 操作来恢复物理内存映射

现在看看在 tcache 中分配较小的几类 large 的情形：
![allocate large from tcache](pictures/tcache-large-alloc.png)
大致过程：
* 步骤一：从 tcache 中申请 large，如果tcache 中有，那么返回，如果没有，那么执行步骤二
* 步骤二及步骤二可能触发的步骤三、四其实就是下一种情况——从 arena 中分配 large，所以具体内容见
下一种情形

上述是简易的流程，下面看看详细的代码流程：
```
tcache_alloc_large (tcache.h)
从 tcache 中分配 large
|
+--tcache_alloc_easy
|  如果 tbin->avail 中有元素，则分配成功
|  同时 更新 tbin->low_water
|
+--上一步失败
|  arena_malloc_large (arena.c)
|  (具体内容见下文)
|
+--tcache_event (tcache.h)
```

上述过程中的 arena_malloc_large 和从 arena 中分配 large 是一个函数，下面给出简易流程图：
![allocate large from arena](pictures/arena-large-alloc.png)
* 步骤一：从 arena 的 runs_avail 中寻找可用的 run (找到比该run大的也可以，jemalloc 会做切分)，
如果找到，那么返回，如果找不到，那么执行步骤三、四
* 步骤二、三：向操作系统申请新的 chunk，并将 chunk 切成合适的 run，返回步骤二

下面给出在 arena 中分配 large 的详细执行流程：
```
arena_malloc_large (arena.c)
在 arena 中分配 large run
|
+--如果设置了 cache_oblivious,对地址进行随机化
|
+--arena_run_alloc_large (arena.c)
|  这里分配 large run 时，会添加上 large_pad，为 cache_oblivious 功能提供空间
|  |
|  +--arena_run_alloc_large_helper (arena.c)
|  |  |
|  |  +--arena_run_first_best_fit (arena.c)
|  |  |  (具体过程见上文)
|  |  |
|  |  +--arena_run_split_large (arena.c)
|  |     |
|  |     +--arena_run_split_large_helper (arena.c)
|  |        |
|  |        +--如果该 run 内存被 decommitted，调用 chunk_commit_default 将内存 commit
|  |        |
|  |        +--arena_run_split_remove
|  |        |  (具体过程见上文)
|  |        |
|  |        +--对分配的 run 初始化并设置一些标记
|  |           如果需要对 large run 清零，则使用 arena_run_zero 清零内存
|  |
|  +--上一步分配失败，说明没有可用run
|  |  arena_chunk_alloc 分配 chunk
|  |  (具体内容见上文)
|  |
|  +-[?] chunk 分配成功
|     |
|     Y--arena_run_split_large (arena.c)
|     |  (具体内容见上文)
|     |
|     N--arena_run_alloc_large_helper
|        上述换锁时，可能有其他线程添加了run，再试一次
|
+--更新统计参数
|
+--arena_decay_tick
   更新 ticker，并可能触发 arena_purge 内存清理
```

最后一种情况是 huge 的分配：
![allocate huge](pictures/huge-alloc.png)
其主要流程是：
* 步骤一：向 arena 申请分配 huge
* 步骤二：arena 在本地 chunks_szad/ad_cached 缓存中寻找合适的空间，如果找到就返回，如果
找不到，那么执行步骤三
* 步骤三：arena 在本地 chunks_szad/ad_retained 中寻找合适的地址，如果找到则返回，如果找不到，
那么执行步骤四
* 步骤四：从 操作系统中通过 map 申请空间

下面看一下详细的流程：
```
huge_malloc (huge.c)
|
+--huge_palloc (huge.c)
   |
   +--ipallocztm (jemalloc_internal.h)
   |  为 chunk 的 extent node 分配空间
   |  (这里似乎是在 thread 自己的 arena 上分配的 huge 的 extent node)
   |
   +--arena_chunk_alloc_huge (arena.c)
   |  |
   |  +--chunk_alloc_cache (chunk.c)
   |  |  (具体内容见下文)
   |  |
   |  +--上一步失败
   |     arena_chunk_alloc_huge_hard (arena.c)
   |     |
   |     +--chunk_alloc_wrapper
   |        (具体内容见下文)
   |
   +--上一步分配失败，调用 idalloctm 释放 extent node
   |  idalloctm 会调用 arena_dalloc 来释放空间
   |  (idalloctm 具体内容见 free 部分)
   |
   +--huge_node_set (huge.c)
   |  huge_node_set 会调用 chunk_register 在 基数树中注册
   |  如果该步失败，则释放 node、huge chunk
   |
   +--调用 ql_tail_insert 将 node 插入 arena->huge
   |
   +--arena_decay_tick
      更新 ticker，并可能触发 arena_purge 内存清理
```

对于 tcache 关闭的情况，还有一个过程 arena_malloc_small，从arena 中分配 small：
```
arena_malloc_small (arena.c)
|
+--根据 ind 选出 bin=arena->bins[ind]
|
+-[?] (run=bin->runcur)!=NULL & run->nfree>0
|  |
|  Y--arena_run_reg_alloc (arena.c)
|  |  根据 run 的 bitmap 找出第一个可用的 region
|  |  根据 ind、offset 等信息算出 region 地址
|  |
|  N--arena_bin_malloc_hard (arena.c)
|     (具体内容见上文)
|
+--更新统计数据
|
+--根据 junk 等参数配置本次分配
|
+--arena_decay_tick (arena.h)
   更新 ticker，并可能触发 arena_purge 内存清理

```

下面给出上述内容中用到的一些子过程。

上述chunk分配中使用到了 chunk_recycle，其作用是从 chunks_szad/ad_* 中回收 chunk：
```
chunk_recycle (chunk.c)
在 chunks_szad,chunks_ad 中回收 chunk
|
+-[?] new_addr != NULL，说明根据地址分配
|  |
|  Y--+--extent_node_init (extent.h)
|  |  |  使用 addr,size 初始化node
|  |  |
|  |  +--extent_tree_ad_search
|  |     使用地址在 chunks_ad 中查找
|  |
|  N--chunk_first_best_fit
|     使用 extent_tree_szad_nsearch 在 chunk_szad 中查找
|
+--没找到node，或者找到的node太小
|  返回 NULL
|
+--根据对齐要求，得到多余的头部、尾部
|  (leadsize, trailsize)
|
+--extent_tree_szad_remove, extent_tree_ad_remove
|  从chunks_szad, chunks_ad 删除node
|
+--arena_chunk_cache_maybe_remove
|  如果 chunk 是dirty，则从arena->chunks_cache, arena->runs_dirty 中删除
|
+--如果头部有多余，那么
|  extent_tree_szad_insert 插入 chunks_szad
|  extent_tree_ad_insert 插入 chunks_ad
|  arena_chunk_cache_maybe_insert 根据是否 dirty/cache 插入 chunks_cache, runs_dirty
|
+--如果尾部有多余，那么
|  |
|  +--chunk_hooks->split
|  |  实际调用 chunk_split_default
|  |  地址空间就是一个数值，切分没有实际操作
|  |  chunk_split_default 返回 false,表示成功
|  |
|  +--如果 node 为 NULL, 调用 arena_node_alloc 为 chunk node 分配空间
|  |  arena_node_alloc 使用 base_alloc 为 node 分配空间
|  |  分配失败，则调用 chunk_record，这里调用 chunk_record 是为了再次尝试分配空间，
|  |  并将 多余空间 放回树中，然后返回
|  |
|  +--node 不为 NULL，将尾部空间放回 chunks_sazd,chunks_ad
|  |  根据 dirty/cache 决定是否放回 runs_dirty,chunks_cache
|  |
|  +--如果 chunk 内存空间未 commit，则调用 chunk_commit_default
|     如果 commit 失败，调用 chunk_record 将 chunk 再放回 chunks_szad/ad 树中
|
+--如果有必要，arena_node_dalloc 将 node 放回 arena->node_cache
   node 是通过 base_alloc 分配的，如果不使用了，需要返回 node_cache 缓存，便于下次使用
```
上述过程涉及到较多的数据结构的信息，需要熟悉数据结构部分的内容，如果有疑问，可以到
结合数据结构部分再看看。

下面说明 chunk 的实际分配函数和实际释放函数：
```
chunk_alloc_wrapper
chunk 的实际分配函数
|
+--chunk_alloc_retained
|  |
|  +--chunk_recycle
|     在 chunks_szad_retained,chunks_ad_retained 中回收 chunk
|
+-[?] 上一步失败
   |
   +--chunk_alloc_default_impl
      |
      +--chunk_alloc_core
         根据策略使用 chunk_alloc_dss 或者
         chunk_alloc_mmap 分配 chunk
         (默认使用 mmap)
         |
         +--chunk_alloc_dss
         |  使用 sbrk 申请地址空间
         |  (详细过程见代码及注释)
         |
         +--chunk_alloc_mmap
            使用 mmap 申请地址空间
            (详细过程见代码及注释)


chunk_dalloc_wrapper
chunk 的实际回收函数
|
+--chunk_dalloc_default_impl
|  如果addr不在dss中，使用
|  chunk_dalloc_mmap/pages_unmap 释放空间
|
+--上述释放成功，返回
|
+--chunk_decommit_default
|  调用pages_decommit/pages_commit_impl
|  来 decommit 地址，如果 os_overcommit!=0,
|  则不 decommit，否则使用mmap(PROT_NONE)来
|  decommit 地址空间
|  (默认os_overcommit不为0，所以什么都不做,decommit失败)
|
+--如果decommit失败，调用chunk_purge_default
|  使用 madvise 来 释放地址空间
|
+--chunk_record
   将 地址空间 放到 chunks_szad/ad_retained
   树中，可供之后的chunk申请使用。
   (retained 树中是有地址空间，但是没有实际
    物理内存的，而cached树中是有物理内存映射
    的，所以申请chunk时，cached树优先级更高)
```

上面和 malloc 有关的主要函数都解释过了，现在还有一些调用链上的其他功能和函数，下面
来看一下，主要是 ticker、内存回收 等相关函数。

先来看一下 ticker 相关的：
```
arena_decay_tick
更新 ticker 值，并可能出发内存回收
|
+--arena_decay_ticks (arena.h)
   |
   +--decay_ticker_get
   |  从 tsd 中拿到属于对应该 arena 的 ticker
   |  (详细过程见下文)
   |
   +-[?] ticker_ticks
      ticker 到了，返回 true，否则 ticker 减去某个值
      |
      Y--arena_purge
         调用 arena_purge 清理内存(all=false)
         (具体内容见下文)
```

上述过程中需要从 tsd 中获取 ticker，其过程如下：
```
decay_ticker_get (jemalloc_internal.h)
在线程 tsd 中获取指定 arena 的 ticker (如果没有，就新建一下)
每个线程都为所有 arena 保存 ticker，但是一般一个线程只用两个 arena
|
+--arena_tdata_get (jemalloc_internal.h)
   |
   +--arenas_tdata = tsd_arenas_tdata_get
   |  获取 tsd 中的 arenas 的 ticker 数组
   |
   +-[?] arenas_tdata == NULL
   |  数组是否为空，为空则需要初始化
   |  |
   |  Y--arena_tdata_get_hard
   |
   +-[?] ind >= tsd_narenas_tdata_get
   |  要获得的 ticker 超出数组的长度
   |  |
   |  Y--arena_tdata_get_hard
   |
   +--arena_tdata_get_hard


arena_tdata_get_hard
|
+--如果 ticker 数组太小，就新建数组，并将原数组复制到新数组
   如果原来没有 ticker 数组，就新建数组并初始化
   (具体实现见源码)
   ticker 数组空间的分配/释放使用 a0malloc/a0dalloc
   a0malloc--a0ialloc--iallocztm--arena_malloc 在 arena 0 上分配
   a0dalloc--a0idalloc--idalloctm--arena_dalloc 完成释放

```

ticker 事件会触发内存回收操作，下面给出了 ticker 触发 arena_purge 函数的内容：
```
arena_purge (arena.c)
回收内存
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
   |  |  如果 dirty run 的 chunk 是 arena->spare，则先将 spare 用 arena_chunk_alloc 分配
   |  |  暂存过程中根据 ratio、decay 的条件，选择结束的时机
   |  |  (具体实现见代码)
   |  |
   |  +--arena_purge_stashed
   |  |  如果是 chunk，暂不清理，因为可能是 arena chunk，链接信息在其内部，
   |  |  清理会使 purge_chunks_sentinel 链表断裂
   |  |  如果是 run，则清理：通过 chunk_decommit_default 或者 chunk_purge_wrapper
   |  |  释放物理地址映射、设置 mapbit 标记
   |  |  (具体实现见代码)
   |  |
   |  +--arena_unstash_purged
   |     对上一步未清理的 chunk 进行实际的清理 (chunk_dalloc_wrapper)
   |     对上一步的 run，调用 arena_run_dalloc 执行 run 的回收
   |     (具体实现见代码) (arena_run_dalloc 的具体内容见 free 部分)
   |
   N--arena_maybe_purge
      |
      +-[?] opt_purge == purge_mode_ratio
         |
         Y--arena_maybe_purge_ratio
         |  根据 lg_dirty_mult 计算需要清理的页面数
         |  根据计算结果决定调用 arena_purge_to_limit 清理页面的数量
         |
         N--arena_maybe_purge_decay
            根据 时钟、decay 参数 计算需要清理的页面数(具体计算过程没有细看)
            根据计算结果决定调用 arena_purge_to_limit 清理页面的数量

```
上述过程中：
* arena_purge_stashed 中对于 run 调用 chunk_decommit_default 或者 chunk_purge_wrapper
来释放 run 的物理内存，chunk_decommit_default 和 chunk_purge_wrapper 是用来对 chunk
内的部分内存释放其物理页面的，很多时候都是用在 run 上的，而对于 chunk，一般使用
chunk_dalloc_wrapper
* 内存回收，对于 chunk，意味着释放物理内存，将地址空间放到 chunks_szad/ad_retained 中，
对于 run，意味着释放物理内存，将 run 从 runs_avail、runs_dirty 中删除，放回 chunk 中

这里给出一张 内存清理的大致流程图：
![arena purge](pictures/arena-purge.png)

最后，对 tcache 的回收做一些说明：
```
tcache_event
tcache 内存回收
|
+-[?] ticker_tick
   |
   +--tcache_event_hard
      对某一个 tbin 进行回收
      |
      +--获取本次回收对象 tcache->next_gc_bin
      |
      +-[?] binind < NBINS
      |  |
      |  Y--tcache_bin_flush_small
      |  |  将部分内存放回 arena
      |  |  (具体内容见 free 部分)
      |  |
      |  N--tcache_bin_flush_large
      |     将部分内存放回 arena
      |     (具体内容见 free 部分)
      |
      +--根据 low_water 动态调整填充度lg_fill_div
      |
      +--设置下一次回收的ind

```


### 源码解析
* arane_choose_hard
arena_choose_hard 为线程选取 arena，在 4.2.1 中 每个线程有两个 arena，一个是 application arena，用来分配
应用数据，一个是internal arena，用来分配部分管理数据，不过目前 jemalloc 4.2.1 的实现还不
完善，根据我的理解，基本上 application arena 和 internal arena 是一个 arena，而且 internal
arena 的使用频率不高。

arena_choose_hard 在选取 arena 时，会同时完成 application arena 和 internal arena 的选取，
按照 负载为零 > 未初始化 > 负载最轻 的优先级选取，如果选了未初始化的 arena, 则调用 arena_init_locked
先初始化 (初始化流程见 初始化部分)，选择完成后，使用 arena_bind 绑定 tsd、arena，根据 internal 参数返回 结果
下面是详细代码：
```c
/* Slow path, called only by arena_choose(). */
/*
 * commented by yuanmu.lb
 * choose both application arena and internal metadata arena for tsd.
 * use 'internal' option to decide which to return ? app or internal?
 */
arena_t *
arena_choose_hard(tsd_t *tsd, bool internal)
{
	arena_t *ret JEMALLOC_CC_SILENCE_INIT(NULL);

	if (narenas_auto > 1) {
		unsigned i, j, choose[2], first_null;

		/*
		 * Determine binding for both non-internal and internal
		 * allocation.
		 *
		 *   choose[0]: For application allocation.
		 *   choose[1]: For internal metadata allocation.
		 */

		for (j = 0; j < 2; j++)
			choose[j] = 0;

		first_null = narenas_auto;
		malloc_mutex_lock(tsd_tsdn(tsd), &arenas_lock);
		assert(arena_get(tsd_tsdn(tsd), 0, false) != NULL);
		for (i = 1; i < narenas_auto; i++) {
			if (arena_get(tsd_tsdn(tsd), i, false) != NULL) {
				/*
				 * Choose the first arena that has the lowest
				 * number of threads assigned to it.
				 */
				/*
				 * commented by yuanmu.lb
				 * choose the arenas for application and internal
				 */
				for (j = 0; j < 2; j++) {
					if (arena_nthreads_get(arena_get(
					    tsd_tsdn(tsd), i, false), !!j) <
					    arena_nthreads_get(arena_get(
					    tsd_tsdn(tsd), choose[j], false),
					    !!j))
						choose[j] = i;
				}
			} else if (first_null == narenas_auto) {
				/*
				 * Record the index of the first uninitialized
				 * arena, in case all extant arenas are in use.
				 *
				 * NB: It is possible for there to be
				 * discontinuities in terms of initialized
				 * versus uninitialized arenas, due to the
				 * "thread.arena" mallctl.
				 */
				first_null = i;
			}
		}

		for (j = 0; j < 2; j++) {
			/*
			 * commented by yuanmu.lb
			 * if threads of arena[choose[j]] is 0, choose it
			 * if no null arena(first_null==narenas_auto), choose choose[j]
			 *     (arena[choose[j]] has lowest threads number)
			 */
			if (arena_nthreads_get(arena_get(tsd_tsdn(tsd),
			    choose[j], false), !!j) == 0 || first_null ==
			    narenas_auto) {
				/*
				 * Use an unloaded arena, or the least loaded
				 * arena if all arenas are already initialized.
				 */
				/*
				 * commented by yuanmu.lb
				 * use 'internal' to determine which to return
				 */
				if (!!j == internal) {
					ret = arena_get(tsd_tsdn(tsd),
					    choose[j], false);
				}
			} else {
				arena_t *arena;

				/* Initialize a new arena. */
				choose[j] = first_null;
				arena = arena_init_locked(tsd_tsdn(tsd),
				    choose[j]);
				if (arena == NULL) {
					malloc_mutex_unlock(tsd_tsdn(tsd),
					    &arenas_lock);
					return (NULL);
				}
				if (!!j == internal)
					ret = arena;
			}
			arena_bind(tsd, choose[j], !!j);
		}
		malloc_mutex_unlock(tsd_tsdn(tsd), &arenas_lock);
	} else {
		ret = arena_get(tsd_tsdn(tsd), 0, false);
		arena_bind(tsd, 0, false);
		arena_bind(tsd, 0, true);
	}

	return (ret);
}
```
上述过程有点长，可以分层三个部分，对应三个 for 循环。第一个 `for (j=0; j<2; j++)`
是初始化，将 choose[0] 和 choose[1] 初始化，这里使用了一个执行两次的 for 循环来
完成工作，后续的 `for (j=0; j<2; j++)` 都是在对 choose[0]、choose[1] 操作，即
对 application arena、internal arena 操作。

第二个 for 循环 `for (i = 1; i < narenas_auto; i++)`,作用是遍历所有 arenas，包括
已经初始化的和没有初始化的，如果此次访问的 arena 已经初始化了，那么使用
`for (j = 0; j<2; j++)` 将 该 arena 与 choose[j] 中指定的 arena 比较，将 负载轻的
赋值给 choose[j]，如果该 arena 还没有初始化，并且 first_null 中记录的是 narenas_auto,
那么将 first_null 赋值为 i，用来标记第一个 没有初始化的 arena。

第三个 for 循环 `for (j=0; j<2; j++)`是根据第二个 for 循环获得的内容来决定为该线程
选取哪个 arena。如果 choose[j] 中指定的 arena 的负载 为 0，或者 first_null 为
narenas_auto (first_null 为 narenas_auto 说明 所有 arenas 都已经初始化了，那么 choose
中记录的 arena 就是负载最轻的，就是最终选择的)，那么 choose[j] 中记录的就是最终结果，
而下面的代码：
```c
if (!!j == internal) {
	ret = arena_get(tsd_tsdn(tsd),
	choose[j], false);
}
```
这是用来决定返回值，internal 中记录着是返回 internal arena 还是 application arena，
而 !!j 则可以得出当前的 choose[j] 是internal 还是 application，从而决定 ret 的值。

如果不满足 choose[j] 负载为0或者 first_null 为 narenas_auto，说明当前还有没有初始化的
arena，那么选取未初始化的 arena，并将之初始化。

以上就是 arena_choose_hard 的过程，主要就是要直到 arena 选取的优先级，这样就容易看明白了。


* arena_run_first_best_fit 及 arena_avail_insert
arena_run_first_best_fit 是从 arena->runs_avail 中满足该尺寸的 run，其中涉及到 size
到 run index 的转换，代码如下：
```c
/*
 * Do first-best-fit run selection, i.e. select the lowest run that best fits.
 * Run sizes are indexed, so not all candidate runs are necessarily exactly the
 * same size.
 */
static arena_run_t *
arena_run_first_best_fit(arena_t *arena, size_t size)
{
	szind_t ind, i;

	ind = size2index(run_quantize_ceil(size));
	for (i = ind; i < runs_avail_nclasses + runs_avail_bias; i++) {
		arena_chunk_map_misc_t *miscelm = arena_run_heap_first(
		    arena_runs_avail_get(arena, i));
		if (miscelm != NULL)
			return (&miscelm->run);
	}

	return (NULL);
}
```
其中，第一步确定 ind，第二步通過 for 循环找到可用的run。第二步好理解，第一步
`ind = size2index(run_quantize_ceil(size))` 这里解释一下，`run_quantize_ceil(size)`
是将该size向上对齐到一个真实的 run 的请求的大小，然后通过 `size2index`转换成一个
index。而具体 runs_avail[ind] 中存放的内容，需要结合 arena_avail_insert 来说明，下面
是 arena_avail_insert 的代码：
```c
static void
arena_avail_insert(arena_t *arena, arena_chunk_t *chunk, size_t pageind,
    size_t npages)
{
	szind_t ind = size2index(run_quantize_floor(arena_miscelm_size_get(
	    arena_miscelm_get_const(chunk, pageind))));
	assert(npages == (arena_mapbits_unallocated_size_get(chunk, pageind) >>
	    LG_PAGE));
	arena_run_heap_insert(arena_runs_avail_get(arena, ind),
	    arena_miscelm_get_mutable(chunk, pageind));
}
```
这段代码的关键是 `ind = size2index(run_quantize_floor(...))`，首先将 size 通过
`run_quantize_floor` 向下对齐到一个真实的 run 的请求大小，然后通过 size2index
转换成 index，大多数时候每一个真实的 run 和一个 index 是对应的，大多数时候，
一个真实的run请求，尤其对于 large run来说，是size2index[i]+4K，那么
arena_avail_insert 意味着大多数时候 runs_avail[index] 中存放着大于等于该index
对应的 真实run (最小size对应的向上对齐的真实的 run) 的可用空间的信息，
比如 index=60 时，其size范围是 (896K, 1024K]，那么 runs_avail[60] 中存放着
大于等于 1000K(896K+4K),小于1028K(1024K+4K) 的空间信息，可以满足 896K 以下的 run
空间分配。(这里896K不包含large_pad,前面加上的4K 为 large_pad)

现在再来看 arena_run_first_best_fit 中定位 runs_avail 的 index 的过程，
`run_quantize_ceil(size)`向上对齐到真实的run，由上述insert过程可知，size2index(...)
映射出的  ind 对应的 runs_avail[ind] 可以满足该真实 run 的分配，则可以满足该size
的分配，比如现在size=1000000，其属于 (896K,1024K]，那么对应的真实 run为
1028K，那么算出的 ind 为 61，那么 runs_avail[61] 中的run为 [1024K+4K, 1280K+4K]，可以
满足本次分配。

到此，上述 arena_run_first_best_fit 和 arena_avail_insert 的过程是对应的。

然而，在 large run 的分配中，调用链上有多次 size 更新，似乎更新后的size有些偏大了。
1. arena_malloc_large : usize=index2size(binind), size = usize+large_pad  
2. arena_run_alloc_large_helper : size = s2u(size)
3. arena_run_first_best_fit : ind = size2index(run_quantize_ceil(size))

上述过程中，经过第三步似乎已经可以找到满足条件的 ind，而第一步、第二步的操作又将
size扩大了一个级别，这样找到的 ind 似乎偏大了，比如 size = 1000000，经过第一步、
第二步，size更新为 1280K，再经过第三步，ind为 62，可以满足 1280K(不包含large_pad)
的分配，而 1000000 只需要 1024K 以上的就可以。所以，这样的计算似乎偏大了。
不知道是不是我理解错了？

经过 GDB 跟踪，large_maxclass 为 1835008，index 为 63，而通过上述三个步骤，最后
的 ind 为 65，而新 chunk 分出来的 maxrun 被放到了 ind 为 64 的 runs_avail 中，
这里也可以看出index的计算似乎有些不匹配，然而，代码中对于 index 为 63 的size范围，
由于在 runs_avail 中永远找不到，所以在 arena_run_alloc_large 中，每次申请完 chunk，
立即执行一次 split，这次的split可以满足所有的large尺寸，对于 index 为 63 的尺寸
也可以满足，而且，index 为 63 的size应该就是在这里分配的：
```c
	chunk = arena_chunk_alloc(tsdn, arena);
	if (chunk != NULL) {
		run = &arena_miscelm_get_mutable(chunk, map_bias)->run;
		if (arena_run_split_large(arena, run, size, zero))
			run = NULL;
		return (run);
	}
```

* chunk_recycle, chunk_record
chunk_recycle、chunk_record 可以看作是两个相反的过程，这里的chunk既包括 arena chunk,
也包括 huge，chunk_recycle 是从 chunks_szad/ad_* 树中获得 chunk 来使用，而
chunk_record 是将当前chunk释放到 chunks_szad/ad_* 树中暂存。chunks_szad/ad_* 是用
红黑树管理的释放的 chunk。(这里的释放是指被用户释放，但是jemalloc可能还没有释放，
jemalloc 将这些 chunk 暂存起来)

这里 chunk_recycle 中获取的 chunk 可能较大，需要切分，将多余的 空间 再放回红黑树中，
而 chunk_record 在回收 chunk 时，可能回收的chunk可以和红黑树中其他chunk合并，所以
有时候还需要做合并操作，合并的时候还需要结合 标志 来判定是否可以合并。

还需要说明一点，chunks 树中有 arena chunk，还有huge，其中 arena chunk 在使用的时候，
头部是有一个 node 空间的，但是 在 chunk_record 中将 arena chunk 放入 chunk 树中时，
并没有使用 arena chunk 头部的 node，而是新申请了一个 node 结构，将该 node 结构的信息
指向该 arena chunk，然后将该新申请的 node 放入 树中，这样 arena chunk  和 huge 的管理
方法就统一了，都是使用一个 外部申请的 node 来管理的。

这两个过程比较复杂，代码也比较长，这里详细的代码分析先略去，之后有时间再补上。

* arena_purge_to_limit
jemalloc 在释放内存的时候并没有将内存立即释放掉，而是使用数据结构将这些脏内存
缓存起来，在某些时机调用 arena_purge 来清理内存，将脏内存清理到一定数量内。
而 arena_purge_to_limit 这是 arena_purge 的实际执行者。

而 arena_purge_to_limit 的内容，上述 内存清理的 流程中已经说明的比较清楚了，所以
这里就不做解释。下面针对 arena_stash_dirty、arena_purge_stashed、
arena_unstash_purged 补充一些说明。

关于 arena_stash_dirty，我们结合上述内存清理的那张图，图中看出，arena->runs_dirty 是
用来管理 dirty runs，arena->chunks_cache 是用来管理 dirty chunks，需要注意的是
arena->runs_dirty 中也将 dirty chunks 链接进去了，就是说 arena->runs_dirty 中既有
dirty runs，也有 dirty chunks，而且 arena->chunks_cache 中的 chunks 顺序和
arena->dirty_runs 中chunks 的顺序是一样的，就是说 arena->chunks_cache 是
arena->dirty_runs 的子序列。还有，这里的 chunk 既包括 arena chunk，又包括 huge，
不过需要说明的是，这里的 arena chunk 的node不是存在 chunk 内部的，而是新申请
了一个 node，该node 中存储了 arena chunk 的信息，而这个 node 才是 链接在 chunks_cache 
中的结构，关于新申请 node 的过程可以阅读 chunk_record 的部分。

```c
static size_t
arena_stash_dirty(tsdn_t *tsdn, arena_t *arena, chunk_hooks_t *chunk_hooks,
    size_t ndirty_limit, arena_runs_dirty_link_t *purge_runs_sentinel,
    extent_node_t *purge_chunks_sentinel)
{
	arena_runs_dirty_link_t *rdelm, *rdelm_next;
	extent_node_t *chunkselm;
	size_t nstashed = 0;

	/* Stash runs/chunks according to ndirty_limit. */
	for (rdelm = qr_next(&arena->runs_dirty, rd_link),
	    chunkselm = qr_next(&arena->chunks_cache, cc_link);
	    rdelm != &arena->runs_dirty; rdelm = rdelm_next) {
		size_t npages;
		rdelm_next = qr_next(rdelm, rd_link);

		/*
		 * commented by yuanmu.lb
		 * see about line 410 in arena.h
		 * chunks_cache links the dirty chunks
		 * runs_dirty links the dirty runs
		 * and a chunk is a kind of run, the maxrun
		 * and chunks_cache is in the same order with runs_dirty
		 * so, when rdelm==&chunkselm->rd, means to stash chunk
		 * otherwise, to stash run
		 */
		if (rdelm == &chunkselm->rd) {
			extent_node_t *chunkselm_next;
			bool zero;
			UNUSED void *chunk;

			npages = extent_node_size_get(chunkselm) >> LG_PAGE;
			/*
			 * commented by yuanmu.lb
			 * for decay mode, nstashed pages is ok for the limit
			 * add npages will break the limit, so break the block
			 *     and just return the nstashed number
			 */
			if (opt_purge == purge_mode_decay && arena->ndirty -
			    (nstashed + npages) < ndirty_limit)
				break;

			chunkselm_next = qr_next(chunkselm, cc_link);
			/*
			 * Allocate.  chunkselm remains valid due to the
			 * dalloc_node=false argument to chunk_alloc_cache().
			 */
			zero = false;
			/*
			 * commented by yuanmu.lb
			 * remove the chunk from chunks_szad_cached and chunks_ad_cached
			 * and stashing it into purge_runs_sentinel and purge_chunks_sentinel
			 */
			chunk = chunk_alloc_cache(tsdn, arena, chunk_hooks,
			    extent_node_addr_get(chunkselm),
			    extent_node_size_get(chunkselm), chunksize, &zero,
			    false);
			assert(chunk == extent_node_addr_get(chunkselm));
			assert(zero == extent_node_zeroed_get(chunkselm));
			extent_node_dirty_insert(chunkselm, purge_runs_sentinel,
			    purge_chunks_sentinel);
			assert(npages == (extent_node_size_get(chunkselm) >>
			    LG_PAGE));
			chunkselm = chunkselm_next;
		} else {
			arena_chunk_t *chunk =
			    (arena_chunk_t *)CHUNK_ADDR2BASE(rdelm);
			arena_chunk_map_misc_t *miscelm =
			    arena_rd_to_miscelm(rdelm);
			size_t pageind = arena_miscelm_to_pageind(miscelm);
			arena_run_t *run = &miscelm->run;
			size_t run_size =
			    arena_mapbits_unallocated_size_get(chunk, pageind);

			npages = run_size >> LG_PAGE;
			if (opt_purge == purge_mode_decay && arena->ndirty -
			    (nstashed + npages) < ndirty_limit)
				break;

			assert(pageind + npages <= chunk_npages);
			assert(arena_mapbits_dirty_get(chunk, pageind) ==
			    arena_mapbits_dirty_get(chunk, pageind+npages-1));

			/*
			 * If purging the spare chunk's run, make it available
			 * prior to allocation.
			 */
			if (chunk == arena->spare)
				arena_chunk_alloc(tsdn, arena);

			/* Temporarily allocate the free dirty run. */
			arena_run_split_large(arena, run, run_size, false);
			/* Stash. */
			if (false)
				qr_new(rdelm, rd_link); /* Redundant. */
			else {
				assert(qr_next(rdelm, rd_link) == rdelm);
				assert(qr_prev(rdelm, rd_link) == rdelm);
			}
			qr_meld(purge_runs_sentinel, rdelm, rd_link);
		}

		nstashed += npages;
		if (opt_purge == purge_mode_ratio && arena->ndirty - nstashed <=
		    ndirty_limit)
			break;
	}

	return (nstashed);
}
```
上述过程的主体是一个 for 循环，for 循环是用来遍历 arena->runs_dirty,在遍历的过程中，
首先通过 `if (rdelm == &chunkselm->rd)` 判断该元素是否是 chunk，前面说了，
arena->chunks_cache 是 arena->runs_dirty 的子序列，如果相等，说明该元素是 chunk，
更新 npages，如果达到清理数量的要求，那么退出循环，接着 将 chunkselm_next 更新为
下一个 chunk，然后将 该chunk 从 chunk 红黑树中分配出来，并放入 purge_runs_sentinel
中。如果该元素不是 chunk，那么先获取该run 的相关信息，更新npages并判断是否达到清理要求，
如果该 run 是 spare 的run，那么需要将  spare 的 chunk 分配出来，spare中记录着上次
释放被回收的dirty chunk，如果需要清理其run，那么该 arena chunk 就不全是dirty的了，
接着将 run 分配出来，最后不管是run还是 chunk，都插入 purge_runs_sentinel 中。这样
整个过程就完成了从 runs_dirty、chunks_cache 中将一定数量的 run、chunk 拿出来，准备
后续的清理。

清理的第二步是 arena_purge_stashed，这一部分是遍历 purge_runs_sentinel，而
purge_chunks_sentinel 是 其 子序列，所以依然采用上一步的方法遍历以及判断是否是
chunk，如果是chunk，那么不做实质的操作，只是更新 chunkselm，因为chunk可能是
arena chunk，在arena chunk 的头部记录着很多 run 的信息，如果现在就执行清理，
那么 run 的连接信息就会丢失。如果是 run，那么优先使用 decommit
释放物理内存，其次使用 chunk_purge_wrapper/madvise 释放物理内存，然后更新
run 的mapbits。这一步只是释放物理地址空间，虚拟地址空间还在。（如果通过decommit，
虚拟地址空间是不能访问的，如果是 madvise，虚拟地址空间还是可以访问的，并且访问
时通过缺页中断重新映射上物理页面）

第三步 arena_unstash_purged，执行剩下的清理操作，包括清理 chunk及更新 run 的管理
信息。首先依然是 遍历 purge_runs_sentinel,如果是 chunk，那么使用 
chunk_dalloc_wrapper/munmap 释放物理内存及虚拟地址空间，如果是 run，那么调用
arena_run_dalloc 通过调用该接口更新 run 的管理信息。

以上就是 内存清理的过程，过程比较复杂，需要好好阅读代码。

