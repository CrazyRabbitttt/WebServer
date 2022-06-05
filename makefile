server: main.cpp ./ThreadPool/threadpool.h ./HTTP/http_conn.cpp ./HTTP/http_conn.h ./Locker/locker.h ./Log/block_queue.h ./Log/log.h ./Log/log.cc  ./MYSQL/SqlConnectionPool.cpp ./MYSQL/SqlConnectionPool.h ./Timer/timer.cc ./Timer/timer.h
		g++ -o server main.cpp ./ThreadPool/threadpool.h ./HTTP/http_conn.cpp ./HTTP/http_conn.h ./Locker/locker.h ./MYSQL/SqlConnectionPool.cpp ./MYSQL/SqlConnectionPool.h ./Timer/timer.cc ./Timer/timer.h ./Log/log.cc ./Log/log.h  -lpthread -lmysqlclient


clean:
		rm -r server


