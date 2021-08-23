#include "threadpool.h"
#include <pthread.h>

const int NUMBER = 2;

//任务结构体
typedef struct Task {
    //函数地址
    void (*function)(void* arg);
    //参数地址
    void* arg;
}Task;

//线程池结构体
struct ThreadPool {
    //任务队列
    Task* taskQ;

    //容量
    int queueCapacity;
    //当前任务个数
    int queueSize;
    //队头
    int queueFront;
    //队尾
    int queueRear;

    //管理者线程id
    pthread_t managerID;
    //工作的线程id, 数组
    pthread_t* threadIDs;
    
    //最小线程数
    int minNum;
    //最大线程数
    int maxNum;
    //忙碌的线程数量
    int busyNum;
    //存活的线程数量
    int liveNum;
    //要杀死的线程数量
    int exitNum;

    //锁整个线程池
    pthread_mutex_t mutexPool;
    //锁住busyNum变量
    pthread_mutex_t  mutexBusy;
    //判断任务队列是不是满了
    pthread_cond_t notFull;
    //判断任务队列是不是空了
    pthread_cond_t notEmpty;

    //销毁线程池标识位, 销毁=1
    int shutdown;
};

/*
* function: 创建线程池并初始化
* param:
* 1.min: 最小线程数
* 2.max: 最大线程数
* 3.queue: Size任务队列容量
*/
ThreadPool* threadPoolCreate(int min, int max, int queueSize) {

    //创建一块堆内存, malloc返回void*类型, 强制类型转换为ThreadPool* 类型
    ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));

    do {
        if (pool == NULL) {
            printf("ERROR: malloc threadpool fail.\n");
            break;
        }

        //根据任务队列的最大值, 为工作线程数组分配内存
        pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
        if (pool->threadIDs == NULL) {
            printf("ERROR: malloc threadIDs fail.\n");
            break;
        }
        //创建成功, 初始化, 未被占用=0
        memset(pool->threadIDs, 0, sizeof(pthread_t) * max);

        //初始化基本参数
        pool->minNum = min;
        pool->maxNum = max;
        pool->busyNum = 0;
        pool->liveNum = min;
        pool->exitNum = 0;

       
        if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
            pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
            pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
            pthread_cond_init(&pool->notFull, NULL) != 0
            ) {
            printf("ERROR: mutex or cond init fail.\n");
            break;
        }

        //初始化任务队列基本参数
        pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
        pool->queueCapacity = queueSize;
        pool->queueSize = 0;
        pool->queueFront = 0;
        pool->queueRear = 0;

        //初始化shutdown
        pool->shutdown = 0;

        //创建线程, 直到打到参数min(最少线程数), 管理者线程地址, 线程属性, 任务函数, 传递参数
        pthread_create(&pool->managerID, NULL, manager, pool);
        for (int i = 0; i < min; i++) {
            pthread_create(&pool->threadIDs[i], NULL, worker, pool);
        }

        //成功返回pool
        return pool;
    } while (0);
    

    //释放资源
    if (pool && pool->threadIDs) free(pool->threadIDs);
    if (pool && pool->taskQ) free(pool->taskQ);
    if (pool) free(pool);

    return NULL;
}

int threadPoolDestroy(ThreadPool* pool) {
    if (pool == NULL) {
        return -1;
    }

    //关闭线程池
    pool->shutdown = 1;
    
    //阻塞回收管理者线程
    pthread_join(pool->managerID, NULL);
    //唤醒阻塞的消费者线程
    for (int i = 0; i < pool->liveNum; i++) {
        pthread_cond_signal(&pool->notEmpty);
    }

    //释放内存
    if (pool->taskQ) {
        free(pool->taskQ);
    }

    if (pool->threadIDs) {
        free(pool->threadIDs);
    }
    
    pthread_mutex_destroy(&pool->mutexPool);
    pthread_mutex_destroy(&pool->mutexBusy);
    pthread_cond_destroy(&pool->notEmpty);
    pthread_cond_destroy(&pool->notFull);

    free(pool);
    pool = NULL;

    return 0;
}

void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg) {
    pthread_mutex_lock(&pool->mutexPool);

    //如果工作队列已经满了, 并且线程池未关闭, 阻塞生产者线程.
    while (pool->queueSize == pool->queueCapacity && !pool->shutdown) {
        pthread_cond_wait(&pool->notFull, &pool->mutexPool);
    }
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutexPool);
        return;
    }

    //添加任务
    pool->taskQ[pool->queueRear].function = func;
    pool->taskQ[pool->queueRear].arg = arg;
    pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
    pool->queueSize++;

    pthread_cond_signal(&pool->notEmpty);

    pthread_mutex_unlock(&pool->mutexPool);
}

int threadPoolBusyNum(ThreadPool* pool) {
    pthread_mutex_lock(&pool->mutexBusy);
    int result = pool->busyNum;
    pthread_mutex_unlock(&pool->mutexBusy);
    return result;
}

int threadPoolLiveNum(ThreadPool* pool) {
    pthread_mutex_lock(&pool->mutexPool);
    int result = pool->liveNum;
    pthread_mutex_unlock(&pool->mutexPool);
    return result;
}

void* worker(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    while (1) {
        pthread_mutex_lock(&pool->mutexPool);
        //判断当前任务队列是否为空
        while (pool->queueSize == 0 && !pool->shutdown) {
            //阻塞工作线程
            pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);
            
            //判断是不是要要销毁线程
            if (pool->exitNum > 0) {
                pool->exitNum--;
                if (pool->liveNum > pool->minNum) {
                    pool->liveNum--;
                    pthread_mutex_unlock(&pool->mutexPool);
                    threadExit(pool);
                }
            }
        }
        //判断线程池是否被关闭了 shutdown变量=1
        if (pool->shutdown) {
            //解锁
            pthread_mutex_unlock(&pool->mutexPool);
            //当前线程退出
            threadExit(pool);
        }
        
        //从任务队列中取出一个任务
        Task task;
        task.function = pool->taskQ[pool->queueFront].function;
        task.arg = pool->taskQ[pool->queueFront].arg;
        //移动头节点
        pool->queueFront = (pool->queueFront + 1) % (pool->queueCapacity);
        pool->queueSize--;
        
        //唤醒生产者, 生产者也会唤醒消费者, 互相影响的过程
        pthread_cond_signal(&pool->notFull);
        pthread_mutex_unlock(&pool->mutexPool);

        //两面包夹芝士-上
        printf("Thread %ld start working...\n", pthread_self()); 
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum++;
        pthread_mutex_unlock(&pool->mutexBusy);
        
        //锁住busyNum, 然后工作, 结束后解锁busyNum
        task.function(task.arg); //(*task.function)(task.arg);
        free(task.arg);
        task.arg = NULL;

        //两面包夹芝士-下
        printf("Thread %ld end working...\n", pthread_self());
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum--;
        pthread_mutex_unlock(&pool->mutexBusy);
    }
    return NULL;
}

void* manager(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    while (!pool->shutdown) {
        //每隔3s检测一次
        sleep(3);

        // 取出线程池中任务的数量和当前线程的数量
        pthread_mutex_lock(&pool->mutexPool);
        int queueSize = pool->queueSize;
        int liveNum = pool->liveNum;
        pthread_mutex_unlock(&pool->mutexPool);

        //取出忙的线程的数量, 对于常用的参数可以专门一把锁锁住
        pthread_mutex_lock(&pool->mutexBusy);
        int busyNum = pool->busyNum;
        pthread_mutex_unlock(&pool->mutexBusy);

        //添加线程
        //添加条件: 任务的个数 > 存活的线程个数 && 存活的线程个数 < 最大的线程个数
        if (queueSize > liveNum && liveNum < pool->maxNum) {
            pthread_mutex_lock(&pool->mutexPool);
            int counter = 0;
            //NUMBER是每次添加的上限, 遍历整个工作线程id数组, 寻找那个位子能用.
            for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; i++) {
                if (pool->threadIDs[i] == 0) {
                    pthread_create(&pool->threadIDs[i], NULL, worker, pool);
                    counter++;
                    pool->liveNum++;
                }
            }
            pthread_mutex_unlock(&pool->mutexPool);
        }

        //销毁线程
        //销毁条件: 忙碌线程 * 2 < 存活的线程 && 存活的线程 > 最小线程数
        if (busyNum * 2 < liveNum && liveNum > pool->minNum) {
            pthread_mutex_lock(&pool->mutexPool);
            pool->exitNum = NUMBER;
            pthread_mutex_unlock(&pool->mutexPool);
            //让工作的线程自杀
            for (int i = 0; i < NUMBER; i++) {
                pthread_cond_signal(&pool->notEmpty);
            }
        }

    }
    return NULL;
}

void threadExit(ThreadPool* pool) {
    pthread_t tid = pthread_self();
    for (int i = 0; i < pool->maxNum; i++) {
        if (pool->threadIDs[i] = tid) {
            pool->threadIDs[i] = 0;
            printf("threadExit() called, %ld exiting.\n", tid);
            break;
        }
    }
    pthread_exit(NULL);
}

