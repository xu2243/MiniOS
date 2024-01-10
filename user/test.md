如果在用户测试程序中不调用exit，那么多次执行用户程序后因为pcb未释放会导致fork失败。

- orange/hello.bin | orange/says.bin
这个命令，按理说会先执行orange/hello.bin，它会往stdout输出"Hello,world!"，建立了管道的话在此时yield()后，orange/says.bin理应能读取并输出"pipe says: Hello world!"到终端。需要注意，不能让says.bin先执行，因为minios没有时钟调度，只能靠yield来调度，这就导致gets()是一个绝对的阻塞函数。