#include "os_transport_thread_pool_internal.h"
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

// 初始化单个任务队列
static int task_queue_init(TaskQueue* queue, uint32_t cap) {
    if (queue == NULL || cap == 0) {
        return -1;
    }

    queue->tasks = (ThreadPoolTask*)calloc(cap, sizeof(ThreadPoolTask));
    if (queue->tasks == NULL) {
        TRANSPORT_LOG("ERROR", "malloc task queue failed, cap=%d", cap);
        return -1;
    }

    queue->cap = cap;
    queue->size = 0;
    queue->head = 0;
    queue->tail = 0;

    // 初始化同步原语
    if (pthread_mutex_init(&queue->mutex, NULL) != 0 ||
        pthread_cond_init(&queue->cond_not_empty, NULL) != 0 ||
        pthread_cond_init(&queue->cond_not_full, NULL) != 0 ||
        pthread_cond_init(&queue->cond_task_done, NULL) != 0) {
        TRANSPORT_LOG("ERROR", "init mutex/cond failed, errno=%d", errno);
        free(queue->tasks);
        return -1;
    }

    return 0;
}

// 销毁任务队列
static void task_queue_destroy(TaskQueue* queue) {
    if (queue == NULL) return;

    pthread_mutex_lock(&queue->mutex);
    if (queue->tasks != NULL) {
        free(queue->tasks);
        queue->tasks = NULL;
    }
    pthread_mutex_unlock(&queue->mutex);

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond_not_empty);
    pthread_cond_destroy(&queue->cond_not_full);
    pthread_cond_destroy(&queue->cond_task_done);
    memset(queue, 0, sizeof(TaskQueue));
}

// 入队任务（阻塞直到队列非满）
static int task_queue_push(TaskQueue* queue, const ThreadPoolTask* task) {
    if (queue == NULL || task == NULL) return -1;

    pthread_mutex_lock(&queue->mutex);

    // 队列满则阻塞
    while (queue->size >= queue->cap && !queue->tasks[0].is_completed) {
        pthread_cond_wait(&queue->cond_not_full, &queue->mutex);
    }

    // 拷贝任务到队列
    memcpy(&queue->tasks[queue->tail], task, sizeof(ThreadPoolTask));
    queue->tail = (queue->tail + 1) % queue->cap;
    queue->size++;

    // 通知队列非空
    pthread_cond_signal(&queue->cond_not_empty);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

// 出队任务（阻塞直到队列非空）
static int task_queue_pop(TaskQueue* queue, ThreadPoolTask* task) {
    if (queue == NULL || task == NULL) return -1;

    pthread_mutex_lock(&queue->mutex);

    // 队列空则阻塞
    while (queue->size == 0 && !queue->tasks[0].is_completed) {
        pthread_cond_wait(&queue->cond_not_empty, &queue->mutex);
    }

    // 空队列/销毁中，返回失败
    if (queue->size == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    // 取出任务
    memcpy(task, &queue->tasks[queue->head], sizeof(ThreadPoolTask));
    queue->head = (queue->head + 1) % queue->cap;
    queue->size--;

    // 通知队列非满
    pthread_cond_signal(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

// 任务完成通知（worker→asyncPoll→外部）
void notify_task_complete(ThreadPoolHandle handle, ThreadPoolTask* task, bool success) {
    if (handle == NULL || task == NULL) return;

    pthread_mutex_lock(&handle->global_mutex);
    task->is_completed = true;
    pthread_mutex_unlock(&handle->global_mutex);

    // 1. 通知asyncPoll任务完成
    pthread_cond_signal(&handle->worker_queue.cond_task_done);

    // 2. asyncPoll回调外部（这里直接执行，也可由asyncPoll统一处理）
    if (task->complete_cb != NULL) {
        task->complete_cb(task->complete_arg, success);
    }

    // 3. 释放任务资源（外部参数需自行管理，这里标记完成）
    TRANSPORT_LOG("INFO", "task %lu completed, success=%d", task->task_id, success);
}

// ===================== 线程业务逻辑 =====================
// 1. asyncPoll线程：接收外部中断→入队notifier→监测worker完成→通知外部
static void* async_poll_thread_func(void* arg) {
    ThreadPoolHandle handle = (ThreadPoolHandle)arg;
    ThreadPoolTask task;

    TRANSPORT_LOG("INFO", "asyncPoll thread initialized, waiting for interrupt...");

    // 步骤1：等待线程启动信号（初始化后才运行）
    pthread_mutex_lock(&handle->global_mutex);
    while (!handle->is_running && !handle->is_destroying) {
        pthread_cond_wait(&handle->cond_thread_start, &handle->global_mutex);
    }
    pthread_mutex_unlock(&handle->global_mutex);

    if (handle->is_destroying) {
        pthread_exit(NULL);
        return NULL;
    }

    // 步骤2：循环监测外部中断
    while (!handle->is_destroying) {
        // 等待外部中断信号
        pthread_mutex_lock(&handle->global_mutex);
        pthread_cond_wait(&handle->cond_interrupt, &handle->global_mutex);
        pthread_mutex_unlock(&handle->global_mutex);

        if (handle->is_destroying) break;

        // 步骤3：从asyncPoll队列取出任务
        if (task_queue_pop(&handle->async_poll_queue, &task) != 0) {
            TRANSPORT_LOG("WARN", "asyncPoll queue is empty");
            continue;
        }

        // 步骤4：将任务转发到notifier队列
        task.type = THREAD_TYPE_NOTIFIER;
        if (task_queue_push(&handle->notifier_queue, &task) != 0) {
            TRANSPORT_LOG("ERROR", "push task to notifier queue failed, task=%lu", task.task_id);
            notify_task_complete(handle, &task, false);
            continue;
        }
        TRANSPORT_LOG("INFO", "asyncPoll push task %lu to notifier queue", task.task_id);

        // 步骤5：等待worker完成任务
        pthread_mutex_lock(&handle->worker_queue.mutex);
        while (!task.is_completed && !handle->is_destroying) {
            pthread_cond_wait(&handle->worker_queue.cond_task_done, &handle->worker_queue.mutex);
        }
        pthread_mutex_unlock(&handle->worker_queue.mutex);

        // 步骤6：通知外部调用者（已在notify_task_complete中执行）
        TRANSPORT_LOG("INFO", "asyncPoll notify external: task %lu done", task.task_id);
    }

    TRANSPORT_LOG("INFO", "asyncPoll thread exit");
    pthread_exit(NULL);
    return NULL;
}

// 2. notifier线程：接收asyncPoll任务→转发worker→通知worker启动
static void* notifier_thread_func(void* arg) {
    ThreadPoolHandle handle = (ThreadPoolHandle)arg;
    ThreadPoolTask task;

    TRANSPORT_LOG("INFO", "notifier thread initialized, waiting for task...");

    // 等待线程启动信号
    pthread_mutex_lock(&handle->global_mutex);
    while (!handle->is_running && !handle->is_destroying) {
        pthread_cond_wait(&handle->cond_thread_start, &handle->global_mutex);
    }
    pthread_mutex_unlock(&handle->global_mutex);

    if (handle->is_destroying) {
        pthread_exit(NULL);
        return NULL;
    }

    // 循环转发任务到worker
    while (!handle->is_destroying) {
        // 从notifier队列取任务
        if (task_queue_pop(&handle->notifier_queue, &task) != 0) {
            usleep(1000); // 空轮询避免CPU占用
            continue;
        }

        // 转发到worker队列
        task.type = THREAD_TYPE_WORKER;
        if (task_queue_push(&handle->worker_queue, &task) != 0) {
            TRANSPORT_LOG("ERROR", "push task to worker queue failed, task=%lu", task.task_id);
            notify_task_complete(handle, &task, false);
            continue;
        }
        TRANSPORT_LOG("INFO", "notifier push task %lu to worker queue", task.task_id);
    }

    TRANSPORT_LOG("INFO", "notifier thread exit");
    pthread_exit(NULL);
    return NULL;
}

// 3. worker线程：执行任务→释放资源→发送完成信号
static void* worker_thread_func(void* arg) {
    ThreadPoolHandle handle = (ThreadPoolHandle)arg;
    ThreadPoolTask task;
    int thread_id = *(int*)arg;
    free(arg); // 释放线程ID参数

    TRANSPORT_LOG("INFO", "worker thread %d initialized, waiting for task...", thread_id);

    // 等待线程启动信号
    pthread_mutex_lock(&handle->global_mutex);
    while (!handle->is_running && !handle->is_destroying) {
        pthread_cond_wait(&handle->cond_thread_start, &handle->global_mutex);
    }
    pthread_mutex_unlock(&handle->global_mutex);

    if (handle->is_destroying) {
        pthread_exit(NULL);
        return NULL;
    }

    // 循环执行任务
    while (!handle->is_destroying) {
        // 从worker队列取任务（阻塞）
        if (task_queue_pop(&handle->worker_queue, &task) != 0) {
            usleep(1000);
            continue;
        }

        if (handle->is_destroying) break;

        // 执行任务
        TRANSPORT_LOG("INFO", "worker %d start processing task %lu", thread_id, task.task_id);
        bool success = true;
        if (task.task_func != NULL) {
            try { // 简易异常捕获（避免worker崩溃）
                task.task_func(task.task_arg);
            } catch (...) {
                success = false;
                TRANSPORT_LOG("ERROR", "worker %d task %lu execute failed", thread_id, task.task_id);
            }
        } else {
            success = false;
            TRANSPORT_LOG("ERROR", "worker %d task %lu has no func", thread_id, task.task_id);
        }

        // 释放资源+发送完成信号
        notify_task_complete(handle, &task, success);
    }

    TRANSPORT_LOG("INFO", "worker thread %d exit", thread_id);
    pthread_exit(NULL);
    return NULL;
}

// ===================== 对外接口实现 =====================
// 初始化线程池：1asyncPoll + 1notifier + 64worker（仅初始化，不运行）
ThreadPoolHandle thread_pool_init(uint32_t queue_cap) {
    // 1. 分配线程池内存
    ThreadPoolHandle handle = (ThreadPoolHandle)calloc(1, sizeof(struct _ThreadPool));
    if (handle == NULL) {
        TRANSPORT_LOG("ERROR", "malloc thread pool failed");
        return NULL;
    }

    // 2. 初始化全局同步原语
    if (pthread_mutex_init(&handle->global_mutex, NULL) != 0 ||
        pthread_cond_init(&handle->cond_thread_start, NULL) != 0 ||
        pthread_cond_init(&handle->cond_interrupt, NULL) != 0) {
        TRANSPORT_LOG("ERROR", "init global cond/mutex failed");
        free(handle);
        return NULL;
    }

    // 3. 初始化任务队列
    if (task_queue_init(&handle->async_poll_queue, queue_cap) != 0 ||
        task_queue_init(&handle->notifier_queue, queue_cap) != 0 ||
        task_queue_init(&handle->worker_queue, queue_cap) != 0) {
        TRANSPORT_LOG("ERROR", "init task queues failed");
        thread_pool_destroy(handle);
        return NULL;
    }

    // 4. 配置线程数量：固定1/1/64
    handle->thread_nums[THREAD_TYPE_ASYNC_POLL] = 1;
    handle->thread_nums[THREAD_TYPE_NOTIFIER] = 1;
    handle->thread_nums[THREAD_TYPE_WORKER] = 64;
    handle->is_initialized = true;
    handle->is_running = false;
    handle->is_destroying = false;
    handle->next_task_id = 1;

    // 5. 创建线程（初始化但不运行）
    // 5.1 创建asyncPoll线程
    if (pthread_create(&handle->threads[THREAD_TYPE_ASYNC_POLL][0], NULL,
                       async_poll_thread_func, handle) != 0) {
        TRANSPORT_LOG("ERROR", "create asyncPoll thread failed");
        thread_pool_destroy(handle);
        return NULL;
    }

    // 5.2 创建notifier线程
    if (pthread_create(&handle->threads[THREAD_TYPE_NOTIFIER][0], NULL,
                       notifier_thread_func, handle) != 0) {
        TRANSPORT_LOG("ERROR", "create notifier thread failed");
        thread_pool_destroy(handle);
        return NULL;
    }

    // 5.3 创建64个worker线程
    for (int i = 0; i < 64; i++) {
        int* thread_id = (int*)malloc(sizeof(int));
        *thread_id = i;
        if (pthread_create(&handle->threads[THREAD_TYPE_WORKER][i], NULL,
                           worker_thread_func, thread_id) != 0) {
            TRANSPORT_LOG("ERROR", "create worker thread %d failed", i);
            free(thread_id);
            thread_pool_destroy(handle);
            return NULL;
        }
    }

    TRANSPORT_LOG("INFO", "thread pool init success: 1asyncPoll + 1notifier + 64worker");
    return handle;
}

// 外部触发中断：通知asyncPoll线程处理任务
int thread_pool_trigger_interrupt(ThreadPoolHandle handle, ThreadPoolTask* task) {
    if (handle == NULL || task == NULL || !handle->is_initialized) {
        TRANSPORT_LOG("ERROR", "invalid param for trigger interrupt");
        return -1;
    }

    // 1. 分配唯一任务ID
    pthread_mutex_lock(&handle->global_mutex);
    task->task_id = handle->next_task_id++;
    task->is_completed = false;
    pthread_mutex_unlock(&handle->global_mutex);

    // 2. 将任务入队asyncPoll队列
    if (task_queue_push(&handle->async_poll_queue, task) != 0) {
        TRANSPORT_LOG("ERROR", "push task %lu to asyncPoll queue failed", task->task_id);
        return -1;
    }

    // 3. 发送中断信号给asyncPoll线程
    pthread_cond_signal(&handle->cond_interrupt);
    TRANSPORT_LOG("INFO", "trigger interrupt success, task %lu", task->task_id);

    return 0;
}

// 启动所有线程（初始化后调用，线程开始工作）
int thread_pool_start(ThreadPoolHandle handle) {
    if (handle == NULL || !handle->is_initialized || handle->is_running) {
        TRANSPORT_LOG("ERROR", "invalid thread pool state for start");
        return -1;
    }

    // 发送启动信号，唤醒所有阻塞的线程
    pthread_mutex_lock(&handle->global_mutex);
    handle->is_running = true;
    pthread_cond_broadcast(&handle->cond_thread_start);
    pthread_mutex_unlock(&handle->global_mutex);

    TRANSPORT_LOG("INFO", "thread pool start success, all threads are running");
    return 0;
}

// 销毁线程池：等待所有任务完成后退出
void thread_pool_destroy(ThreadPoolHandle handle) {
    if (handle == NULL) return;

    TRANSPORT_LOG("INFO", "thread pool destroying...");

    // 1. 标记销毁状态
    pthread_mutex_lock(&handle->global_mutex);
    handle->is_destroying = true;
    handle->is_running = false;
    // 唤醒所有阻塞的线程
    pthread_cond_broadcast(&handle->cond_thread_start);
    pthread_cond_broadcast(&handle->cond_interrupt);
    pthread_mutex_unlock(&handle->global_mutex);

    // 2. 唤醒所有队列的阻塞线程
    pthread_cond_broadcast(&handle->async_poll_queue.cond_not_empty);
    pthread_cond_broadcast(&handle->notifier_queue.cond_not_empty);
    pthread_cond_broadcast(&handle->worker_queue.cond_not_empty);

    // 3. 等待所有线程退出
    // 3.1 asyncPoll线程
    pthread_join(handle->threads[THREAD_TYPE_ASYNC_POLL][0], NULL);
    // 3.2 notifier线程
    pthread_join(handle->threads[THREAD_TYPE_NOTIFIER][0], NULL);
    // 3.3 worker线程（64个）
    for (int i = 0; i < 64; i++) {
        pthread_join(handle->threads[THREAD_TYPE_WORKER][i], NULL);
    }

    // 4. 销毁任务队列
    task_queue_destroy(&handle->async_poll_queue);
    task_queue_destroy(&handle->notifier_queue);
    task_queue_destroy(&handle->worker_queue);

    // 5. 销毁全局同步原语
    pthread_mutex_destroy(&handle->global_mutex);
    pthread_cond_destroy(&handle->cond_thread_start);
    pthread_cond_destroy(&handle->cond_interrupt);

    // 6. 释放内存
    free(handle);
    TRANSPORT_LOG("INFO", "thread pool destroy success");
}