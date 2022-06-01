#include "timer.h"

//下面进行时间轮的实现

time_wheel::time_wheel() : cur_slot(0) 
{
    //初始化时间轮
    for (int i = 0; i < N; i++) {
        slots[i] = NULL;
    }   
}

time_wheel::~time_wheel() {
    //遍历所有的槽位，销毁掉所有的计时器
    for (int i = 0; i < N; i++) {
        tw_timer * tmp = slots[i];      //头节点
        while (tmp) {                   
            slots[i] = tmp->next;       
            delete tmp;
            tmp = slots[i];
        }
    }
}

//进行定时器的添加
tw_timer*
time_wheel::add_timer(int timeout) {
    if (timeout < 0) {
        return NULL;
    }
    int ticks = 0;

    //根据timeout的时间进行确定tick值
    if (timeout < SI) ticks = 1;
    else ticks = timeout / SI;

    //计算多少圈之后被触发
    int rotation = ticks / N;

    //计算新添加的定时器应该被插入到哪一个槽中
    int ts = (cur_slot + (ticks % N))  % N;

    //创建定时器，初始化圈数目 & 槽位置
    tw_timer * timer = new tw_timer(rotation, ts);

    //下面将定时器插入到槽当中去
    if(!slots[ts]) {
        printf("The slot is null, now add a new timer to it.Rotation:%d, ts:%d, cur_slot:%d\n", rotation, ts, cur_slot);
        slots[ts] = timer;
    }else {     //否则将定时器进行插入，链表是不需要进行排序的，直接插入到头部就行
        timer->next = slots[ts];
        slots[ts]->prev = timer;
        slots[ts] = timer;
    }
    return timer;
}

void
time_wheel::del_timer(tw_timer * timer) {
    //进行定时器的删除
    if (!timer) return;

    int ts = timer->time_slot;

    if (timer == slots[ts]) {       //如果是链表的头部的话
        slots[ts] = slots[ts] -> next;
        if (slots[ts]) {            //配置一下新的链表头节点的数据
            slots[ts] -> prev = NULL;   
        }
        delete timer;
    }else {
        timer->prev->next = timer->next;
        if (timer->next) {          //如果timer有下一个节点的话
            timer->next->prev = timer->prev;
        }
        delete timer;
    }
}


void
time_wheel::tick() {                //time滴答函数
    //获得时间轮上面当前槽的头节点
    tw_timer * timer = slots[cur_slot];
    printf("current slot is %d\n", cur_slot);
    while (timer) {       //下面需要进行对于当前链表的遍历，进行是否是到达timeout的判断
        //如果说rotation > 0那么说明这一轮还不到它
        if (timer->rotation > 0) {
            timer->rotation--;
            timer = timer->next;        //进行链表上下一个节点的判断
        }else {
            timer->cb_func(timer->user_data);       //进行回调函数，将客户端的数据传输过去
            if (timer == slots[cur_slot]) {               //如果说是头指针的话
                slots[cur_slot] = slots[cur_slot]->next;
                delete timer;
                if (slots[cur_slot]) {
                    slots[cur_slot]->prev = NULL;
                }
                timer = slots[cur_slot];
            }else {
                timer->prev->next = timer->next;
                if (timer->next) {
                    timer->next->prev = timer->prev;
                }
                tw_timer* tmp = timer->next;
                delete timer;
                timer = tmp;
            }
        }
    }
    cur_slot = (cur_slot + 1) % N;              //时间轮的指针指向下一个槽位
}













