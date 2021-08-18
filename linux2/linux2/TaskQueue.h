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

    //�������
    void addTask(Task<T> task);
    void addTask(callback f, void* arg);
    //ȡ��һ������
    Task<T> takeTask();
    //��ȡ��ǰ����ĸ���
    inline size_t taskNumber() {
        return m_taskQ.size();
    }
    
private:
    pthread_mutex_t m_mutex;
    std::queue<Task<T><T>>m_taskQ;
};
