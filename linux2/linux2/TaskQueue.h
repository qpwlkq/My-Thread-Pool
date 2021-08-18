#pragma once
#include <queue>
#include <pthread.h>


using callback = void (*)(void*);

template <typename T>
struct Task<T> {
    Task<T>() {
        function = nullptr;
        arg = nullptr;
    }
    Task<T>(callback func, void* arg) {
        this->arg = (T*)arg;
        function = func;
    }
    callback function;
    T* arg;
};

template <typename T>
class TaskQueue {
public:
    TaskQueue();
    ~TaskQueue();

    //添加任务
    void addTask(Task<T> task);
    void addTask(callback f, void* arg);
    //取出一个任务
    Task<T> takeTask();
    //获取当前任务的个数
    inline size_t taskNumber() {
        return m_taskQ.size();
    }
    
private:
    pthread_mutex_t m_mutex;
    std::queue<Task<T><T>>m_taskQ;
};
