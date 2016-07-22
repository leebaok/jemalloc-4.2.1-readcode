## 遗留问题

* tsd state 的四个状态具体含义和作用

* tsd arenas_tdata_bypass 的作用

* ctl 是干嘛的

* 哪些 在 base 中分配，哪些在 arena 0 中分配，哪些在自己的 arena 中分配

* malloc_mutex_lock, witness 等关于锁的细节

* arena_purge 的 decay 模式具体如何工作

* arenas_tdata 是 一个 arena 一个，还是每个线程都有每个 arena 的 arena_tdata?
如果每个都有，那么arena 对于每个线程是单独计时？？

## 笔记
* 内存释放不是非要按照 chunk 为单位的
arena_purge_to_limit 中对run 的回收会使用 chunk_decommit_default/chunk_purge_wrapper 释放chunk中部分页面，
优先使用 decommit，不行的话，使用 chunk_purge，purge 最后会调用 madvise 来
释放页面，并且在linux平台上，使用 madvise 释放后，会将 run 置为 zero 
如果使用 decommit ，则会将 run 置为 decommit

chunk_purge 其实就是用来释放 run 的
chunk自己的释放其实是通过 chunk_dalloc_wrapper 实现的，该函数最终使用 munmap
来释放chunk，而根据 linux 文档，munmap 释放时，即使要释放的空间有部分已经释放，其会将没释放的继续释放，并正确执行完成。

那么，对于 有部分run释放的 chunk，释放时其dirty pages 怎么算？
其实，有部分 run 释放的 chunk，不会被记录在 dirty chunks中，
而是记录在 achunks 中 (所以，achunks 中记录的不全是正在使用的 chunk，而是被切成 run
 的chunk)
比如，chunk = run 1 of zeroed + run 2 of dirty + run 3 of zeroed，该chunk 就在 achunk
中，虽然该chunk没有被使用，但是内部 run 没有被合并
这样一个 chunk 会在 arena_purge 时， run 2 --> zeroed,然后和 run1、run3 合并，最后
整个chunk被dalloc

对于 arena_purge， X 如果在 runs_dirty 和 chunks_cache 中，那么 X 是dirty chunk，
使用 chunk_dalloc，如果 X 只在 runs_dirty 中，先使用 chunk_purge 将 run 物理页面
清洗掉，状态置为 decommit或者zero，然后和前后run合并，如果可以合并成 chunk，再将
chunk 释放。



