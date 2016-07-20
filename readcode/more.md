## 遗留问题

* tsd state 的四个状态具体含义和作用

* tsd arenas_tdata_bypass 的作用

* ctl 是干嘛的

* 哪些 在 base 中分配，哪些在 arena 0 中分配，哪些在自己的 arena 中分配

* malloc_mutex_lock, witness 等关于锁的细节

* arena_purge 的 decay 模式具体如何工作

## 笔记
* 内存释放不是非要按照 chunk 为单位的
arena_purge_to_limit 中对run 的回收会使用 chunk_decommit_default/chunk_purge_wrapper 释放chunk中部分页面，
优先使用 decommit，不行的话，使用 chunk_purge，purge 最后会调用 madvise 来
释放页面，并且在linux平台上，使用 madvise 释放后，会将 run 置为 zero 
如果使用 decommit ，则会将 run 置为 decommit

chunk_purge 其实就是用来释放 run 的
chunk自己的释放其实是通过 chunk_dalloc_wrapper 实现的，该函数最终使用 munmap
来释放chunk，而根据 linux 文档，munmap 释放时，即使要释放的空间有部分已经释放，其会将没释放的继续释放，并正确执行完成。
