#ifndef LST_TIMER
#define LST_TIMER

//时间轮定时器，每个槽上的定时器都是没有顺序的
//插入O(1),删除O(1),执行O(N)但是由于时间轮将定时器散裂开，完全不是O(n)

#include <time.h>
#include <netinet/in.h>
#include <stdio.h>

#define BUFFER_SIZE 64

class tw_timer;

//客户端的数据，回调函数进行执行
struct client_data
{
    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    tw_timer *timer;        //定时器
};

class tw_timer
{
public:
    tw_timer(int rot, int ts)
    :next(NULL), prev(NULL), rotation(rot), time_slot(ts)
    {}

public:
    int rotation;                       //时间轮多少圈之后才进行执行
    int time_slot;                      //定时器属于时间轮上的那一个槽位
    void (*cb_func)(client_data *);     //回调函数，
    client_data * user_data;            //客户端的连接，用于回调函数

    tw_timer * next;
    tw_timer * prev;    
};


class time_wheel
{

public:
    time_wheel();
    ~time_wheel();
    tw_timer* add_timer(int timeout);       //创建定时器到时间轮中
    void del_timer(tw_timer* timer);        //将定时器进行删除
    void tick();                            //进行时间的滴答，处理事件


private:
    /* 时间轮上面的槽的数目 */
    static const int N = 60;

    /* 时间轮进行滴答的时间的间隔 */
    static const int SI = 1;    

    /* 时间轮的槽数组，本质是一个链表的数组， 其中链表是无序的*/
    tw_timer* slots[N];

    /* 时间轮当前位于哪一个槽位 */
    int cur_slot;       
};


#endif