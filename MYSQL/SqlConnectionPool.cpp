#include "SqlConnectionPool.h"

using namespace std;

connection_pool::connection_pool() {
    this->CurConn = 0;
    this->FreeConn = 0;
}

//单例模式通过静态变量获得
connection_pool * connection_pool::GetInstance() {
    static connection_pool connPool;
    return &connPool;
}

void connection_pool::init(string url, string User, string PassWord, string DbName, int Port, unsigned int MaxCoon1) {
    this->url   = url;
    this->Port  = Port;
    this->User  = User;
    this->PassWord = PassWord;
    this->DatabaseName = DbName; 
    
    int tmpConn = MaxCoon1;
    printf("进行了线程池的初始化\n");
    lock.lock();

    printf("The maxconn:%d, this:%d\n", tmpConn, this->MaxConn);
    for (int i = 0; i < tmpConn; i++) {
        //debug 
        printf("创建数据库连接：%d\n", i);
        MYSQL *conn = NULL;
        conn = mysql_init(conn);            

        if (conn == NULL) {
            cout << "Error:" << mysql_error(conn);
            exit(1);
        }
        conn = mysql_real_connect(conn, url.c_str(), User.c_str(), PassWord.c_str(), DbName.c_str(), Port, NULL, 0);
        
        
        if (conn == NULL) {
            cout << "Error: " << mysql_error(conn);
            exit(1);
        }
        connList.push_back(conn);   //将空闲连接添加到List中
        ++FreeConn;                 //空闲可使用的连接

    }

    reserve = sem(FreeConn);

    this->MaxConn = FreeConn;
    lock.unlock();
    //debug:
    printf("数据库连接池创建成功啊！\n");
}


//从数据库连接池中返回一个可用的连接，更新空闲连接的数目
MYSQL *connection_pool::GetConnection() {
    MYSQL *conn = NULL;

    if (connList.size() == 0) {
        return NULL;
    }

    reserve.wait();             //信号量进行等待可用

    lock.lock();

    conn = connList.front();
    connList.pop_front();

    --FreeConn;                 //可用的减少
    ++CurConn;                  //目前正在使用的加一

    lock.unlock();
    return conn;

}


bool connection_pool::ReleaseConnection(MYSQL* conn) {
    if (conn == NULL) return false;

    //销毁就要放到List中

    lock.lock();

    connList.push_back(conn);
    ++FreeConn;
    --CurConn;

    lock.unlock();

    reserve.post();                     //来可用的啦，没有的就会接收到信号量的通知
    return true;

}


//销毁数据库连接池子

void connection_pool::DestroyPool() {
    lock.lock();

    if (connList.size() > 0) {
        list<MYSQL*>::iterator it;
        for (it = connList.begin(); it != connList.end(); it++) {
            MYSQL *tmp = *it;
            mysql_close(tmp);               //关掉数据库连接
        }
        CurConn = 0;
        FreeConn= 0;
        connList.clear();

        lock.unlock();
        // return ;
    }
    lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn() {
    return this->FreeConn;
}

connection_pool::~connection_pool() {
    DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool * connPool) {
    *SQL = connPool->GetConnection();           //获得可用的连接

    conRAII  = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII);           //调用池子中的Release释放掉当前的连接
}

