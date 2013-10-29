tcp-port-forward
================
  用来转发TCP数据，参数去除ARGV[0]之后，每4个参数为一组，必须成组出现，依次含义为：
  本地监听地址（loopback/lan/any） 本地监听端口 远程地址 远程端口
  
  
  程序在第一、二个参数指定的地址监听TCP请求，连接后想第三、四个参数指定的地址发起连接，成功后对该2个连接的数据进行相互转发。
  
  
  程序在centos 6.2 final clang++ 3.3 with BOOST 1.54.0使用如下选项编译通过
  clang++ main.cpp -std=c++11 -stdlib=libc++  -I/usr/local/include -I/usr/local/include/c++/v1/include -L/usr/local/lib -lboost_system

  enjoy !
tcp port forward
