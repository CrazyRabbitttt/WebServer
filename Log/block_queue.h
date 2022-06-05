#ifndef _BLOCK_QUEUE_H_
#define _BLOCK_QUEUE_H_

/*阻塞队列，position = (position + 1) % size*/

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../Locker/locker.h"
using namespace std;

template<class T>
class block_queue
{
public:
    block_queue(int max_size = 1000) {
        if (max_size <= 0) exit(-1);

        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    ~block_queue() {
        m_mutex.lock();
        if (m_array != NULL) delete[] m_array;
        m_mutex.unlock();
    }

    //阻塞队列进行清除
    void clear() {
        m_mutex.lock();
        m_size = 0;
        m_front = m_back = -1;
        m_mutex.unlock();
    }

    //判断队列是否是满了
    bool full() {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    //判断队列是否是空的
    bool empty() {
        m_mutex.lock();
        if (m_size == 0) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

        //返回队尾元素
    bool back(T &value) {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    int size() {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_size;

        m_mutex.unlock();
        return tmp;
    }

    int max_size() {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_max_size;

        m_mutex.unlock();
        return tmp;
    }

    //cond，生产者进行生产元素，等待唤醒
    bool push(const T& item) {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_cond.broadcast();             //如果满了就进行广播，唤醒等待的消费者进行消费
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;

        m_size ++;

        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    //pop,如果队列中没有元素的话，等待条件变量
    bool pop(T& item) {
        m_mutex.lock();
        //没有元素才等待条件变量
        while(m_size <= 0) {
            //如果抢到了互斥锁，return 0，否则就是没有抢占到互斥锁
            if(!m_cond.wait(m_mutex.get())) {
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }


private:
    locker m_mutex;
    cond m_cond;

    T* m_array;      //循环的数组，模拟队列
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};


#endif