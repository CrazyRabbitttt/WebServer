#### 使用Webbench进行服务器的压测

> - 压测工具：`webbench` 开启服务器之后从另一个终端开启webbench测试
> - 使用的格式`webbench -c number -t time http://127.0.0.1:9090/`,其中`time` 表示压测的时间，`number`表示压测的数量

**压测结果**

> **本机环境：** 1核2G
>
> **测试结果：** 经过测试，可在10s内进行5000并发的连接
>
> ```shell
> Benchmarking: GET http://127.0.0.1:9090/
> 5000 clients, running 10 sec.
> 
> Speed=193710 pages/min, 361558 bytes/sec.
> Requests: 32285 susceed, 0 failed.
> ```

