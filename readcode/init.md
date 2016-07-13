## 初始化流程

1 jemalloc.c:jemalloc_constructor
  使用 gcc 的 constructor 特性将 jemalloc 初始化 在 main 之前执行

1-1 malloc_init 
  初始化 malloc 

1-1-1 malloc_init_hard

1-1-1-1 malloc_init_hard_needed
  如果自己是初始化执行者，返回
  如果自己不是初始化执行者，等待别人初始化完成
		
1-1-1-2 malloc_init_hard_a0_locked
  自己是初始化的执行者，执行初始化
  a0 指 arena 0，这里指该过程执行初始化直到 arena 0 初始化完成

1-1-1-2-1 malloc_conf_init
  设置 malloc 的参数，比如 junk 等

1-1-1-2-2 pages_boot

1-1-1-2-3 base_boot

1-1-1-2-4 chunk_boot

1-1-1-2-5 ctl_boot

1-1-1-2-6 arena_boot

1-1-1-2-7 tcache_boot

1-1-1-2-8 设置 arenas 数组

1-1-1-2-9 arena_init

1-1-1-3 malloc_tsd_boot0

1-1-1-4 malloc_init_hard_recursible

1-1-1-5 malloc_init_hard_finish

1-1-1-6 malloc_tsd_boot1

1-1-2 malloc_thread_init : do nothing


