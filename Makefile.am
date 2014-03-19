CXX=clang++
CXXFLAGS=-std=c++11 -stdlib=libc++ -I. -I/mo_server_runtime/libcxx/include -I/home/baba/development/mo/include-c++11 -O3 -DENABLE_TRACE -DENABLE_DEBUG -DENABLE_INFO -DENABLE_WARNING -DENABLE_ERROR -DENABLE_FETAL
LDFLAGS=-L/home/baba/development/boost-trunk/stage/lib -lboost_system -lprotobuf -lcurl -ltcmalloc_minimal -lprotobuf -lluabind -llog4cplus
LIBS=-llua -lc++ -lpthread
AUTOMAKE_OPTIONS= foreign 
bin_PROGRAMS= load_server
load_server_SOURCES= main.cpp configure.cpp configure.pb.cc
