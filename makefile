server: main.cpp ./ThreadPool/threadpool.h ./HTTP/http_conn.cpp ./HTTP/http_conn.h ./Locker/locker.h ./MYSQL/SqlConnectionPool.cpp ./MYSQL/SqlConnectionPool.h ./Timer/timer.cc ./Timer/timer.h
		g++ -o server main.cpp ./ThreadPool/threadpool.h ./HTTP/http_conn.cpp ./HTTP/http_conn.h ./Locker/locker.h ./MYSQL/SqlConnectionPool.cpp ./MYSQL/SqlConnectionPool.h ./Timer/timer.cc ./Timer/timer.h   -lpthread -lmysqlclient


clean:
		rm -r server


