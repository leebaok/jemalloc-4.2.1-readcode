## free 流程
```
je_free (jemalloc.c)
|
+--ifree
   |
   +--更新统计数据
   |
   +--iqalloc
      |
	  +--idalloctm
	     |
         +--arena_dalloc
            |
            +--获取释放地址所在的chunk的地址
            |
            +-[?] chunk 地址不等于 释放地址
               |  说明释放空间是 small 或者 large，不是huge
               |
               Y--+--获取释放地址在chunk中的pageind,mapbits
               |  |
               |  +-[?] 根据 mapbits 判断是否是 small 
               |     |
               |     Y-[?] tcache != NULL
               |     |  |
               |     |  Y--tcache_dalloc_small
               |     |  |
               |     |  N--arena_dalloc_small
               |     |
               |     N-[?] tcache != NULL & size-large_pad <= tcache_maxclass
               |        |
               |        Y--tcache_dalloc_large
               |        |
               |        N--arena_dalloc_large
               |
			   N--huge_dalloc
```
               
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
|     |  |  (arena_dalloc_bin_junked_locked 过程在后面解释)
|     |  |
|     |  +--arena_decay_ticks
|     |     更新时钟计数，并可能触发 arena_purge
|     |
|     +--更新统计，整理 tbin->avail 数组
|
+--将要释放的 bin 放入 tbin->avail 中，并更新 ncached
|
+--tcache_event
```

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
|     |  |  (arena_dalloc_large_junked_locked 过程在后面解释)
|     |  |
|     |  +--arena_decay_ticks
|     |     更新时钟计数，并可能触发 arena_purge
|     |
|     +--更新统计，整理 tbin->avail 数组
|
+--将要释放的 bin 放入 tbin->avail 中，并更新 ncached
|
+--tcache_event
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
|     |  |  |         因为 bin->runs 中是 non-full runs)
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
|     |  |        |        +--如果旧的 spare 为 dirty，则用 arena_run_ditry_remove 
|     |  |        |        |  runs_ditry 中移除
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
|     |        将 run，runcur 中 region 少的作为 runcur，多的使用  
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
