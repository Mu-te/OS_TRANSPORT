#ifndef OS_TRANSPORT_THREAD_POOL_H
#define OS_TRANSPORT_THREAD_POOL_H

#include <stdint.h>
#include <stdlib.h>

// 线程类型枚举（你后续补充具体作用）
typedef enum {
    THREAD_TYPE_ASYNC_POLL = 0,  // 异步轮询线程
    THREAD_TYPE_NOTIFIER = 1,    // 通知线程
    THREAD_TYPE_WORKER = 2,      // 工作线程
    THREAD_TYPE_MAX = 3          // 线程类型总数
} ThreadType;

// 任务回调函数类型（任务的具体逻辑由外部传入）
typedef void (*ThreadPoolTaskCallback)(void* arg);

// 任务结构体（每个任务绑定类型+回调+参数）
typedef struct {
    ThreadType type;                  // 任务归属的线程类型
    ThreadPoolTaskCallback callback;  // 任务回调函数
    void* arg;                        // 回调函数参数（外部传入，需自行管理内存）
    uint8_t is_processed;             // 任务是否已处理（内部标记）
} ThreadPoolTask;

// 线程池句柄（对外隐藏内部结构，仅暴露指针）
typedef struct _ThreadPool* ThreadPoolHandle;

/**
 * @brief 初始化线程池
 * @param thread_nums 每种线程的数量（数组，顺序：ASYNC_POLL、NOTIFIER、WORKER）
 * @param queue_cap 每种线程任务队列的容量（队列满阈值）
 * @return 线程池句柄（非NULL=成功，NULL=失败）
 */
ThreadPoolHandle thread_pool_init(uint32_t thread_nums[THREAD_TYPE_MAX], uint32_t queue_cap);

/**
 * @brief 提交任务到指定类型的线程队列
 * @param handle 线程池句柄
 * @param type 任务归属的线程类型
 * @param callback 任务回调函数
 * @param arg 回调函数参数（外部需保证生命周期，或在回调中释放）
 * @return 0=成功，-1=失败（仅线程池已销毁时失败，队列满时会缓存任务，不返回失败）
 */
int thread_pool_submit_task(ThreadPoolHandle handle, ThreadType type,
                            ThreadPoolTaskCallback callback, void* arg);

/**
 * @brief 销毁线程池（会处理完所有队列中的任务后退出）
 * @param handle 线程池句柄
 */
void thread_pool_destroy(ThreadPoolHandle handle);

#endif // OS_TRANSPORT_THREAD_POOL_H