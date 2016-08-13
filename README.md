# jemalloc 源码阅读

> by 远牧，2016年7月

jemalloc 是一个高性能的内存分配器，尤其是在多线程的情况下，其性能十分优异，目前已经被用
在多个大型项目中。jemalloc 源码在 https://github.com/jemalloc/jemalloc

本仓库是对 jemalloc-4.2.1 的源码加了注释，并对 jemalloc-4.2.1 的执行流程进行了梳理。

* [概述](readcode/intro.md)
* [总体架构](readcode/arch.md)
* [数据结构](readcode/datastruct.md)
* [初始化](readcode/init.md)
* [malloc](readcode/malloc.md)
* [free](readcode/free.md)
* [小结](readcode/summary.md)
* [附录](readcode/more.md)

