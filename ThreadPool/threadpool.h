#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "Locker/locker.h"
#include "../MYSQL/SqlConnectionPool.h"


template<typename T>
class threadpool
{
public:
    //初始化线程池，进行数据库的连接                线程池中线程数目        请求队列中最多允许的数目
    threadpool(connection_pool *connPool,int thread_number = 8, int max_request = 10000);
    ~threadpool();
    /* 向请求队列中插入任务请求 */
    bool append(T *request);


private:
    /* 工作线程运行的函数，从工作队列中取出来任务进行处理，互斥 & 抢夺 任务 */
    /*  pthread 需要传递的是静态的函数，我们声明为static */
    static void * worker(void *args);
    void run();

private:
    int m_thread_number;        //线程池中的线程数目
    int m_max_requests;         //请求队列中允许的最大请求数目
    pthread_t *m_threads;       //线程池数组，大小就是...nbumber
    std::list<T*> m_workqueue; //用于传递任务的请求队列
    locker m_queuelocker;       //维持互斥访问请求队列
    sem m_queuestat;            //请求队列的状态，是否有任务需要进行处理

    bool m_stop;                //是否结束线程
    connection_pool *m_connPool;//数据库连接池

};


template<typename T>
threadpool<T>::threadpool(connection_pool *connPool,int thread_number, int max_request)
    :m_thread_number(thread_number), m_max_requests(max_request), m_stop(false), m_threads(NULL), m_connPool(connPool)
    {
        /* 如果说线程数或者是请求数小于0 就不抛出异常*/
        if ((thread_number <= 0) || (max_requests <= 0)) {
            throw std::exception();
        } 

        /* 创建线程池.... */
        m_threads = new pthread_t[m_thread_number];
        if (!m_threads) {
            throw std::exception();
        }

        for (int i = 0; i < thread_number; i++) {
            /* 创建子线程，执行worker,传过去的是线程池对象，调用对象的run()函数*/
            if (pthread_create(m_threads + i, NULL, worker, this) != 0) {   
                delete[] m_thread_s;            /* 失败就销毁*/
                throw std::exception();
            }

            /* 线程分离，不需要单独对线程进行回收*/
            if (pthread_detach(m_threads[i])) {
                delete[] m_threads;
                throw std::exception();
            }
        }
        
    }
    


template<typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}


template<typename T>
bool threadpool<T>::append(T *request)
{
    m_queuelocker.lock();               //上锁，

    /* 请求队列的大小过大，*/
    if (m_workqueue.size() > m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);     //添加任务

    m_queuelocker.unlock();             //解锁

    /*信号量进行提醒。。。。。。。。有任务来啦*/
    m_queuestat.post();                 //P,信号量➕1
    return true;
} 



template<typename T>
void * threadpool<T>::worker(void *arg) {
    //函数调用传入的参数是  this， 也就是线程池对象被传进来了
    //返回类型是void *, 传进来的参数(线程池对象)
    threadpool* pool = (threadpool*)arg;    //强转为pool
    pool->run();
    return pool;
}


/* run()：工作线程从请求队列中取出任务进行处理*/


template<typename T>
void threadpool<T>::run() {
    while (!m_stop) {
        m_queuestat.wait();             //等待信号量，看看是否有可用的

        m_queuelocker.lock();           //执行到这里就是唤醒了，上互斥锁
        if (m_workqueue.empty()) {      //到你了但是没有任务可做，可能被抢夺走了
            m_queuelocker.unlock();
            continue;   
        }

        /*从请求队列中拿任务出来*/

        T *request = m_workqueue.front();
        m_workqueue.pop_front();

        m_queuelocker.unlock();         //解锁

        if (!request) continue;         //空，继续等

        /*连接数据库，从连接池中取出来一个数据库连接*/

        connectionRAII mysqlcon(&request->mysql, m_connPool);

        /*Process进行处理*/
        request -> process();           //就是每个对象都自己有处理的函数，process

        /*处理完将数据库放回连接池*/

    }
}



#endif