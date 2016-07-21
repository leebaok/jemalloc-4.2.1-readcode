## free 流程
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
free 的主体流程很清晰，下面看看每个子过程的流程。
               
下面是将 small bin 释放到 tcache 的流程：
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
|     |  |  (arena_dalloc_bin_junked_locked 具体内容见下文)
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
上述循环中的代码有点长，但是意思并不复杂，仔细看，并没有什么难点。

下面是将 run 释放到 tcache 的过程，和 `tcache_dalloc_small` 十分相似：
```
tcache_dalloc_large (tcache.h)
|
+-[?] tbin 满了
|  |
|  +--tcache_bin_flush_large (tcache.c)
|     |
|     +--循环：每次回收同一个 arena 中的 bin，直到达到回收数量
|     |  |
|     |  +--找到属于某个 arena 的 bin，调用 arena_dalloc_bin_junked_locked 回收
|     |  |  (arena_dalloc_large_junked_locked 具体内容见下文)
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


```
arena_dalloc_small
|
+--arena_dalloc_bin
|  |
|  +--arena_dalloc_bin_locked_impl
|     |
|     +--arena_run_reg_dalloc
|     |  |
|     |  +--bitmap_unset 修改 run 的 bitmap
|     |  |
|     |  +--更新 run->nfree
|     |
|     +-[?] run->nfree == bin_info->nregs (该run所有的region都释放了)
|     |  |
|     |  Y--+--arena_dissociate_bin_run
|     |  |  |  |
|     |  |  |  +-[?] run == bin->runcur
|     |  |  |     |
|     |  |  |     Y--将 bin->runcur 置为 NULL
|     |  |  |     |
|     |  |  |     N--如果该 bin 的 run 的容量大于一个 region，那么
|     |  |  |        调用 arena_run_heap_remove 将 run 从 bin->runs 中移除
|     |  |  |        (如果bin的run的容量就是一个region，那么不需要移除,
|     |  |  |         因为 bin->runs 中是 non-full non-empty runs，该run
|     |  |  |         在本次释放前是empty，所以不在 runs 中)
|     |  |  |
|     |  |  +--arena_dalloc_bin_run
|     |  |     |
|     |  |     +--arena_run_dalloc
|     |  |        |
|     |  |        +--获取相关参数
|     |  |        |
|     |  |        +--设置 run 在 chunk 中的 bitmaps
|     |  |        |
|     |  |        +--arena_run_coalesce (arena.c)
|     |  |        |  通过 mapbits 标志，尝试将该 run 和前后的 run 合并
|     |  |        |  (具体实现见代码)
|     |  |        |
|     |  |        +--arena_avail_insert
|     |  |        |  将该run插入 runs_avail
|     |  |        |
|     |  |        +--如果该 run 是 dirty，调用 arena_run_dirty_insert 插入 runs_dirty
|     |  |        |
|     |  |        +-[?] 合并后的 run 是一个 chunk
|     |  |        |  |
|     |  |        |  +--arena_chunk_dalloc
|     |  |        |     |
|     |  |        |     +--arena_avail_remove 将 run 从 arena->runs_avail 中移除
|     |  |        |     |
|     |  |        |     +--ql_remove 将 chunk 从 arena->achunks 中移除
|     |  |        |     |
|     |  |        |     +--将该 chunk 设置为 arena->spare
|     |  |        |     |
|     |  |        |     +--arena_spare_discard
|     |  |        |        将 旧的 spare 释放掉
|     |  |        |        |
|     |  |        |        +--如果旧的 spare 为 dirty，则用 arena_run_dirty_remove 
|     |  |        |        |  runs_dirty 中移除
|     |  |        |        |
|     |  |        |        +--arena_chunk_discard
|     |  |        |           |
|     |  |        |           +--chunk_deregister 将chunk从基数树从移除
|     |  |        |           |
|     |  |        |           +--如果chunk标志为 decommit，调用 chunk_decommit_default
|     |  |        |           |  将地址空间 decommit 掉 (默认 os_overcommit不为0，什么都不做)
|     |  |        |           |
|     |  |        |           +--chunk_dalloc_cache
|     |  |        |              |    
|     |  |        |              +--chunk_record 将 chunk 记录到 chunks_szad/ad_cached 树中
|     |  |        |              |  过程中会尝试与该chunk地址相邻的chunk合并
|     |  |        |              |
|     |  |        |              +--arena_maybe_purge
|     |  |        |        
|     |  |        +-[?] dirty
|     |  |           |
|     |  |           +--arena_maybe_purge
|     |  |
|     |  N-[?] run->nfree == 1 & run != bin->runcur
|     |     |
|     |     Y--arena_bin_lower_run
|     |        nfree=1，说明之前该 run 为空，不在 runs 中
|     |        将 run，runcur 中地址低的作为 runcur，地址高的使用  
|     |        arena_bin_runs_insert 放入 bin->runs
|     |
|     +--更新统计信息
|
+--arena_decay_tick

```



```
arena_dalloc_bin_junked_locked
|
+--arena_dalloc_bin_locked_impl
   (工作流程见上述内容)

```



```
arena_dalloc_large
|
+--arena_dalloc_large_locked_impl
   |
   +--更新统计参数
   |
   +--arena_run_dalloc
      (工作流程见上述内容)

```


```             
arena_dalloc_large_junked_locked
|
+--arena_dalloc_large_locked_impl
   (工作流程见上述内容)

```



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
|     (工作流程见上述内容)
|
+--idalloctm
|  将 huge node 的空间释放
|  |
|  +--arena_dalloc 
|     (工作流程见上述内容)
|
+--arena_decay_tick
               
```
