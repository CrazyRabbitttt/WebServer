#include "SqlConnectionPoll.h"

using namespace std;


connection_pool::connection_pool() {
    this -> CurConn = 0;
    this -> FreeConn = 0;
}


void connection_pool::init(string url, string User, string Passwd, string DBName, int Port, unsigned int MaxConn)
{
    this -> url = url;
    this -> Port = Port;
    this -> User = User;
    this -> PassWord = Passwd;
    this -> DataBaseName = DBName;

    lock.lock();        //上锁

    for (int i = 0; i < MaxConn; i++) {
        MYSQL *conn = NULL;
        conn = mysql_init(conn);            //初始化sql， Max个SQL连接

        if (conn == NULL) {
            cout << "Error:" << mysql_error(conn);
            exit(1);
        }

        conn = mysql_real_connect(conn, url.c_str(), User.c_str(), Passwd.c_str(), DBName.c_str(), Port, NULL, 0);

        if (conn == NULL) {
            cout << "Error:" << mysql_error(conn);
            exit(1);
        }

        /* 更新连接池*/
        connList.push_back(conn);           //将得到的连接加入到List中供使用
        ++FreeConn;

    }

    reserve = sem(FreeConn);                //使用目前空闲资源数目初始化信号量

    this ->MaxConn = FreeConn;

    lock.unlock();      //解锁

}



//有请求的时候从数据库连接池中返回一个可用的连接，更新空闲与使用变量
MYSQL *connection_pool::GetConnection() {
    MYSQL *conn = NULL;

    if (connList.size() == 0) {
        return NULL;
    }

    reserve.wait();                     //等待信号量的可用资源,为零就阻塞

    lock.lock();

    conn = connList.front();            //从空闲链表中拿到可用资源
    connList.pop_front();

    --FreeConn;                         //更新资源数目
    ++CurConn;

    lock.unlock();
    
    return conn;

}


bool connection_pool::ReleaseConnection(MYSQL *conn) {
    if (conn == NULL) return false;

    lock.lock();

    connList.push_back(conn);
    ++FreeConn;
    --CurConn;

    lock.unlock();

    reserve.post();                     //增加信号量（连接池增加了），唤醒阻塞的线程
    return true;
}

/* 销毁掉连接池 */
void connection_pool::DestoryPool() {
    lock.lock();
    if (connList.size()) {
        auto it = connList.begin();
        //使用迭代器遍历容器
        for (it; it != connList.end(); it++) {
            MYSQL *curconn = *it;
            mysql_close(curconn);
        }
        CurConn = 0;            //设置可用数目
        FreeConn = 0;
        connList.clear();       //清空链表
        lock.unlock();
    }

    lock.unlock();
}


int connection_pool::GetFreeCoon() {
    return this->FreeConn;
}

/* 析构函数自动调用Destory函数 */
connection_pool::~connection_pool() {
    DestoryPool();      
}

/* 利用局部静态变量来获取唯一的连接池对象
    使用静态类变量的话并发情况下肯能会生成多个对象*/
connection_pool * connection_pool::GetInstance() {
    static connection_pool connPool;
    return &connPool;
}

/* 使用RAII进行一个连接的管理*/
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connpool) {
    *SQL = connpool->GetConnection();

    connRAII = *SQL;            //数据库连接
    poolRAII = connpool;        //连接池
}

connectionRAII::~connectionRAII() {
    poolRAII -> ReleaseConnection(connRAII);
}