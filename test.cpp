#include "MYSQL/SqlConnectionPool.h"

int main(int argc, char ** argv)
{
    printf("进行数据库连接的测试！\n");
    connection_pool* connPool = connection_pool::GetInstance();        //获得单例模式的数据库连接池
    connPool->init("localhost", "root", "shaoguixin1+", "AA", 3306, 8);      //进行数据库连接池的初始化

    delete connPool;
    return 0;
}
