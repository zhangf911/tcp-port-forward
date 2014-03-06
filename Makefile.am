CXX=clang++
CXXFLAGS=-std=c++11 -stdlib=libc++ -I. -I/mo_server_runtime/gcc481_and_clang/local/include -I/mo_server_runtime/gcc481_and_clang/local/include/c++/v1/include -I/home/baba/development/mo/include-c++11 -g -O0  -DENABLE_TRACE -DENABLE_DEBUG -DENABLE_INFO -DENABLE_WARNING -DENABLE_ERROR -DENABLE_FETAL -DBOOST_ALL_DYN_LINK
LDFLAGS=-L/mo_server_runtime/gcc481_and_clang/local/lib -lboost_log -lboost_log_setup -lboost_thread -lboost_system -L/usr/local/lib -lprotobuf -lcurl -ltcmalloc_minimal -lprotobuf -lluabind
LIBS=-llua -lc++ -lpthread
AUTOMAKE_OPTIONS= foreign 
bin_PROGRAMS= load_server
load_server_SOURCES= main.cpp configure.cpp configure.pb.cc
