#ifndef OS_TRANSPORT_THREAD_POOL_H
#define OS_TRANSPORT_THREAD_POOL_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

// 线程类型枚举
typedef enum {
    THREAD_TYPE_ASYNC_POLL = 0,  // 异步轮询线程（1个）
    THREAD_TYPE_NOTIFIER = 1,    // 通知线程（1个）
    THREAD_TYPE_WORKER = 2,      // 工作线程（64个）
    THREAD_TYPE_MAX = 3
} ThreadType;

// 任务完成回调（asyncPoll通知外部调用者）
typedef void (*TaskCompleteCallback)(void* arg, bool success);

// 任务结构体（扩展：包含完成回调+外部参数）
typedef struct {
    ThreadType type;                  // 任务归属类型（最终由worker执行）
    void (*task_func)(void* arg);     // 任务执行函数
    void* task_arg;                   // 任务参数
    TaskCompleteCallback complete_cb; // 完成回调（asyncPoll调用）
    void* complete_arg;               // 回调参数
    bool is_completed;                // 任务是否完成
    uint64_t task_id;                 // 任务ID（唯一标识）
} ThreadPoolTask;

// 线程池句柄（对外隐藏）
typedef struct _ThreadPool* ThreadPoolHandle;

/**
 * @brief 初始化线程池（固定创建1asyncPoll+1notifier+64worker，仅初始化不运行）
 * @param queue_cap 任务队列容量（建议设为1024）
 * @return 线程池句柄/NULL
 */
ThreadPoolHandle thread_pool_init(uint32_t queue_cap);

/**
 * @brief 外部触发中断：通知asyncPoll线程开始处理任务
 * @param handle 线程池句柄
 * @param task 要执行的任务（最终由worker处理）
 * @return 0=成功，-1=失败
 */
int thread_pool_trigger_interrupt(ThreadPoolHandle handle, ThreadPoolTask* task);

/**
 * @brief 启动所有线程（初始化后调用，线程开始等待任务）
 * @param handle 线程池句柄
 * @return 0=成功，-1=失败
 */
int thread_pool_start(ThreadPoolHandle handle);

/**
 * @brief 销毁线程池（等待所有任务完成后退出）
 * @param handle 线程池句柄
 */
void thread_pool_destroy(ThreadPoolHandle handle);

#endif // OS_TRANSPORT_THREAD_POOL_H