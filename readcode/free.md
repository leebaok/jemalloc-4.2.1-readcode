## free 

### 流程说明
这部分主要解释 free 的工作流程。

下面给出 free 的主体函数的流程：
```
je_free (jemalloc.c)
|
+--ifree (jemalloc.c)
   |
   +--更新统计数据
   |
   +--iqalloc (jemalloc_internal.h)
      |
	  +--idalloctm (jemalloc_internal.h)
	     |
         +--arena_dalloc (arena.h)
            |
            +--获取释放地址所在的chunk的地址
            |
            +-[?] chunk 地址不等于 释放地址
               |  说明释放空间是 small 或者 large，不是 huge
               |
               Y--+--获取释放地址在 chunk 中的 pageind,mapbits
               |  |
               |  +-[?] 根据 mapbits 判断是否是 small
               |     |
               |     Y-[?] tcache != NULL
               |     |  |
               |     |  Y--tcache_dalloc_small
               |     |  |  (具体内容见下文)
               |     |  |  
               |     |  N--arena_dalloc_small
               |     |     (具体内容见下文)
               |     |
               |     N-[?] tcache != NULL & size-large_pad <= tcache_maxclass
               |        |
               |        Y--tcache_dalloc_large
               |        |  (具体内容见下文)
               |        |
               |        N--arena_dalloc_large
               |           (具体内容见下文)
               |
			   N--huge_dalloc
                  (具体内容见下文)
```
free 的主体流程很清晰，基本就是 malloc 的逆过程，在默认 tcache 开启的情况下按照四种尺寸分别调用：
tcache_dalloc_small、tcache_dalloc_large、arena_dalloc_large、huge_dalloc 来释放内存。
如果 tcache 关闭，则使用 arena_dalloc_small、arena_dalloc_large、huge_dalloc 来释放内存。

下面来看 tcache 开启的情况下的四种情形：

首先，释放 small bin 时，重要的流程见下图：
![deallocate small to tcache](pictures/tcache-small-dalloc.png)
上图中：
* 步骤一：调用接口将 small 释放到 tcache 中，如果 tcache 中有空间释放，那么返回；如果
tcache 中对应 bin 满了，那么触发 步骤二
* 步骤二：对该 bin 执行 flush，将其部分 region 释放到 arena 的 bin 中，如果释放到
arena 的 bin 的run 中时，run 没满，那么返回步骤一；如果 arena 中 bin 的run满了，
那么触发步骤三
* 步骤三：将arena 的 bin 中的满的run释放回 arena 的 runs_avail 中，释放过程中，
会将 run 和前后可合并的 run 合并，如果合并后的 run 不是 整个chunk，那么放入 runs_avail，
返回 步骤二，如果合并后的 run 是 一个 chunk，那么触发步骤四
* 步骤四：释放chunk到 chunks_szad/ad_cached

上述过程和 malloc 的过程可以对应上，很好理解。下面看一看具体的流程：
```
tcache_dalloc_small (tcache.h)
|
+-[?] tbin 满了
|  |
|  +--tcache_bin_flush_small (tcache.c)
|     |
|     +--循环：每次回收同一个 arena 中的 bin，直到达到回收数量
|     |  |
|     |  +--找到属于某个 arena 的 bin，调用 arena_dalloc_bin_junked_locked 回收
|     |  |  arena_dalloc_bin_junked_locked 会调用 arena_dalloc_bin_locked_impl 回收
|     |  |  (arena_dalloc_bin_locked_impl 具体内容见下文)
|     |  |
|     |  +--arena_decay_ticks
|     |     更新时钟计数，并可能触发 arena_purge
|     |
|     +--更新统计，整理 tbin->avail 数组
|
+--将要释放的 bin 放入 tbin->avail 中，并更新 ncached
|
+--tcache_event
   更新 ticker，并可能出发 tcache_event_hard 对 bin 进行回收
```
其中 arena_dalloc_bin_locked_impl 的过程如下：
```
arena_dalloc_bin_locked_impl (arena.c)
|
+--arena_run_reg_dalloc
|  将 region 放回 run 中
|  |
|  +--bitmap_unset 修改 run 的 bitmap，更新 run->nfree
|
+-[?] run->nfree == bin_info->nregs
|  |  该run所有的 region 都释放了
|  |      
|  Y--+--arena_dissociate_bin_run (arena.c)
|  |  |  |
|  |  |  +-[?] run == bin->runcur
|  |  |     |
|  |  |     Y--将 bin->runcur 置为 NULL
|  |  |     |
|  |  |     N--如果该 bin 的 run 的容量大于一个 region，那么
|  |  |        调用 arena_run_heap_remove 将 run 从 bin->runs 中移除
|  |  |        (如果bin的run的容量就是一个region，那么不需要移除,
|  |  |         因为 bin->runs 中是 non-full non-empty runs，该run
|  |  |         在本次释放前是empty，所以不在 runs 中)
|  |  |
|  |  +--arena_dalloc_bin_run (arena.c)
|  |     |
|  |     +--arena_run_dalloc (arena.c)
|  |        释放该 run (具体内容见下文)
|  |  
|  N-[?] run->nfree == 1 & run != bin->runcur
|     |  现在 nfree=1，说明之前该 run 为空，不在 runs 中
|     |
|     Y--arena_bin_lower_run (arena.c)
|        将 run，runcur 中地址低的作为 runcur，地址高的使用  
|        arena_bin_runs_insert 放入 bin->runs
|
+--更新统计信息
```
上述过程还可能触发 run 的释放，下面看看 arena_run_dalloc 的过程：
```
arena_run_dalloc (arena.c)
|
+--获取相关参数，设置 run 在 chunk 中的 bitmaps
|
+--arena_run_coalesce (arena.c)
|  通过 mapbits 标志，尝试将该 run 和前后的 run 合并
|
+--arena_avail_insert (arena.c)
|  将该run(合并后的 run)插入 runs_avail
|
+--如果该 run 是 dirty，调用 arena_run_dirty_insert 插入 runs_dirty
|
+-[?] 合并后的 run 是一个 chunk
|  |
|  Y--arena_chunk_dalloc (arena.c)
|     将 chunk 从正在占用的 chunks 中删除
|     |
|     +--arena_avail_remove 将 run 从 arena->runs_avail 中移除
|     |  ql_remove 将 chunk 从 arena->achunks 中移除
|     |  将该 chunk 设置为 arena->spare
|     |
|     +--arena_spare_discard (arena.c)
|        将 旧的 spare 释放掉
|        |
|        +--如果旧的 spare 为 dirty，则用 arena_run_dirty_remove 从 runs_dirty 中移除
|        |
|        +--arena_chunk_discard (arena.c)
|           |
|           +--chunk_deregister 将chunk从基数树从移除
|           |
|           +--根据 chunk 的 maxrun 的 mapbits 的 committed 标志，
|           |  尝试 decommit chunk 的 header 的物理地址
|           |  (这一步真的会执行到，当 chunk 的 maxrun 被 decommit 的时候会执行)
|           |  (说明 spare 的 maxrun 可能会 decommit，而 run 在用的时候会被commit)
|           |
|           +--chunk_dalloc_cache
|              |    
|              +--chunk_record 将 chunk 记录到 chunks_szad/ad_cached 树中
|              |  过程中会尝试与该chunk地址相邻的chunk合并
|              |  并尝试将 chunk 放到 runs_dirty,chunks_cache 中
|              |
|              +--arena_maybe_purge
|                 触发一次清理内存   
|        
+-[?] dirty
   |
   Y--arena_maybe_purge
      触发一次清理内存   
```
上述流程有些复杂，run 的释放可能会触发 chunk 的回收，chunk 的回收又涉及到 spare 指针
的维护，以及最后还会触发 内存清理，需要好好阅读代码。

下图是将 large 释放回 tcache 的过程：
![deallocate large to tcache](pictures/tcache-large-dalloc.png)
上图不难理解，这里就不解释了，下面是具体流程：
```
tcache_dalloc_large (tcache.h)
|
+-[?] tbin 满了
|  |
|  +--tcache_bin_flush_large (tcache.c)
|     |
|     +--循环：每次回收同一个 arena 中的 bin，直到达到回收数量
|     |  |
|     |  +--找到属于某个 arena 的 bin，调用 arena_dalloc_large_junked_locked 回收
|     |  |  arena_dalloc_large_junked_locked 会调用 arena_dalloc_large_locked_impl 回收
|     |  |  (arena_dalloc_large_locked_impl 具体内容见下文)
|     |  |
|     |  +--arena_decay_ticks
|     |     更新时钟计数，并可能触发 arena_purge
|     |
|     +--更新统计，整理 tbin->avail 数组
|
+--将要释放的 bin 放入 tbin->avail 中，并更新 ncached
|
+--tcache_event
   更新 ticker，并可能出发 tcache_event_hard 对 bin 进行回收
```
上述过程中 arena_dalloc_large_locked_impl 的过程如下：
```
arena_dalloc_large_locked_impl
|
+--更新统计参数
|
+--arena_run_dalloc
   (具体内容见上文)
```
其中涉及 run 的释放，具体内容见上文。

下面是 arena large 的释放：
![deallocate large to arena](pictures/arena-large-dalloc.png)
具体流程如下：
```
arena_dalloc_large
|
+--arena_dalloc_large_locked_impl
```
其中主要工作通过 arena_dalloc_large_locked_impl 完成，而该过程的内容可以参见上文。

再来看 huge 的释放：
![deallocate huge](pictures/huge-dalloc.png)
具体流程如下：
```
huge_dalloc
|
+--huge_node_unset
|  |
|  +--chunk_deregister 将 huge node 从基数树中注销
|
+--ql_remove 将 huge node 从 arena->huge 中删除
|
+--arena_chunk_dalloc_huge
|  |
|  +--更新统计数据
|  |
|  +--chunk_dalloc_cache
|     (具体内容见上文)
|
+--idalloctm
|  将 huge node 的空间释放
|  |
|  +--arena_dalloc
|     (具体内容见上文)
|
+--arena_decay_tick
   更新 ticker，并可能触发 arena_purge 内存清理

```

最后来看看当 tcache 关闭时，将 small 释放到 arena 的过程：
```
arena_dalloc_small (arena.c)
|
+--arena_dalloc_bin (arena.c)
|  |
|  +--arena_dalloc_bin_locked_impl (arena.c)
|     (具体内容见上文)
|
+--arena_decay_tick
   更新 ticker，并可能触发 arena_purge 内存清理
```

### 源码说明
* arena_dalloc
```c
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
```
上述过程中有一些很巧妙的地方，通过简单的计算就可以区分出该释放的内存是 huge、large、
还是 small。首先，第一个 if 就是用来判断时候是 huge：
```c
	chunk = (arena_chunk_t *)CHUNK_ADDR2BASE(ptr);
	if (likely(chunk != ptr)) {
		... }
```
因为 chunk 是 2M 对齐的(在我的32位系统上)，large、small 都是在 chunk 内部，并且不在
chunk 的头部，所以 large、small 的地址一定不是 2M 对齐的，而 huge 比 chunk 大，并且
都是 2M 对齐的，所以先 CHUNK_ADDR2BASE 对 ptr 按照 2M 对齐一下，然后判断 对齐后的
大小是否和 ptr 相等，如果相等说明是 huge，不等说明是 large或者small，这样就区分出了
huge 和 非huge。

下一步是区分 large、small:
```c
		pageind = ((uintptr_t)ptr - (uintptr_t)chunk) >> LG_PAGE;
		mapbits = arena_mapbits_get(chunk, pageind);
		assert(arena_mapbits_allocated_get(chunk, pageind) != 0);
		if (likely((mapbits & CHUNK_MAP_LARGE) == 0)) {
			... }
```
首先获得 该 ptr 对应的页的 mapbits，根据前面数据结构部分的解释，页面在chunk中页面区
的偏移和 mapbits 在 chunk 中 mapbits 区的偏移是一一对应的，那么上述代码第一行得出
该ptr 的页面偏移，然后 通过 `arena_mapbits_get`得到 maptbits，而该函数的真正执行
过程就是一个简单的映射：
```c
	&chunk->map_bits[pageind-map_bias]
```
而得到 mapbits 之后，根据 mapbits 中的标记位就可以知道该 ptr 是否是 large，计算
方法就是上述代码中的 取与，十分方便。

关于 large 下面获取 size、index 等过程就很简单了，不做解释。

而 对于 small，该 ptr 是某一个 region 的 address，还需要根据 ptr 得到 run 的起始
地址，和 region 在 run 中的位置，下面再做一些解释。

关于 根据 ptr 找到 run 的过程在 arena_dalloc_bin_locked_impl(arena.c) 中，相关代码
如下：
```c
	pageind = ((uintptr_t)ptr - (uintptr_t)chunk) >> LG_PAGE;
	rpages_ind = pageind - arena_mapbits_small_runind_get(chunk, pageind);
	run = &arena_miscelm_get_mutable(chunk, rpages_ind)->run;
```
关键是 第二行 根据 pageind 得到 rpages_ind，即 run 的起始页面，下面是 
arena_mapbits_small_runind_get 的代码：
```c
JEMALLOC_ALWAYS_INLINE size_t
arena_mapbits_small_runind_get(const arena_chunk_t *chunk, size_t pageind)
{
	size_t mapbits;

	mapbits = arena_mapbits_get(chunk, pageind);
	assert((mapbits & (CHUNK_MAP_LARGE|CHUNK_MAP_ALLOCATED)) ==
	    CHUNK_MAP_ALLOCATED);
	return (mapbits >> CHUNK_MAP_RUNIND_SHIFT);
}
```
首先得到 mapbits，结合前面数据结构部分关于 small run 的 mapbits 的解释：
```
pppppppp pppppppp pppnnnnn nnn----A
```
其中，`p...p` 是 该页相对于 该run 起始页的偏移，所以上述代码中通过 移位 就可以
得到前面的该页相对该run起始页的偏移，所以 
`rpages_ind = pageind - arena_mapbits_small_runind_get(chunk, pageind)` 就可以得到
该run 的起始页面了，最后通过 
`run = &arena_miscelm_get_mutable(chunk, rpages_ind)->run` 可以得到该run 的map_misc，
即该 run 的管理数据，而这个过程和得到 mapbits 的过程类似，由 页面偏移映射出 
map_misc 在 chunk 的 map_misc 区域的偏移，从而得到 map_misc，执行内容如下：
```c
	return ((arena_chunk_map_misc_t *)((uintptr_t)chunk +
	    (uintptr_t)map_misc_offset) + pageind-map_bias);
```

现在看看 由 ptr 得出 region 在 small run 中的 index 的过程，这一过程可以在 
`arena_run_reg_dalloc(arena.c)`中找到，下面是相关代码：
```c
	size_t pageind = ((uintptr_t)ptr - (uintptr_t)chunk) >> LG_PAGE;
	size_t mapbits = arena_mapbits_get(chunk, pageind);
	szind_t binind = arena_ptr_small_binind_get(ptr, mapbits);
	arena_bin_info_t *bin_info = &arena_bin_info[binind];
	size_t regind = arena_run_regind(run, bin_info, ptr);
```
首先是 根据 mapbits 得出 binind，从而得出 bin_info，然后调用 `arena_run_regind`得出
regind(region index) ，思路就是算出 ptr 相对于 run 起始地址的偏移，然后获取 
一个 region 占用的空间，相除就可以得到 regind，不过实际计算中，jemalloc 为了计算
更快，避免使用除法，所以使用了很多二进制位操作来实现，具体可以参见代码。

* arena_run_coalesce

arena_run_coalesce 是释放run时用来尝试合并该run与前后run 的函数，其功能很明确，不过
实现中需要细致地分析、维护 mapbits 的标记，是否可以合并需要判断 dirty、allocated、
decommitted 三个标记是否一致，三个标记都一致才可以合并。需要注意的是，这里并不
判断 unzeroed 位，在使用的时候，如果需要使用 zeroed 的空间，那么在分配的过程中
会根据要求去初始化一些内存，而初始化时并不是分配的整个run都初始化，而是根据 
unzeroed 标记来判断哪些需要初始化，将这些需要初始化的进行初始化。

