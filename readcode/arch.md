## 总体架构
jemalloc 有五个重要的数据结构：arena, bin, chunk, run, tcache 。

这五个数据结构可以分为三类：
* 物理布局相关：chunk, run
* 中央管理相关：arena, bin
* 线程本地缓存：tcache

chunk：jemalloc 向操作系统获取内存是以 chunk 为单位的，默认chunk为2M，
相当于 jemalloc 每次从操作系统“批发”一个 chunk 的内存。

run：jemalloc 从操作系统获取一个chunk的内存后，会将 chunk 切分成 run 
进行管理。

arena：jemalloc 的中央管理器，其管理着 jemalloc 的 chunk，包括释放掉的 chunk
以及正在使用的 chunk，对于正在使用的 chunk，arena 会将其切分成 run 进行管理。

bin：jemalloc 中央管理器的子管理器，即 arena 的次级管理器，负责 small bin 
的分配，从 arena 申请 run，并将 run 划分成 region 进行实际的 small 分配。

tcache：从属于某个线程的缓存分配器，tache 按照策略从 arena/bin 中获取一定数量
的 small bin 及 部分 large 放在本地缓存，线程的大部分申请都是从 tcache 中获取，
大部分释放都是放回 tcache，tcache 也会按照某种策略将部分缓存放回 arena/bin。




