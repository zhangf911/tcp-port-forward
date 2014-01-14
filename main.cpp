//
//  main.cpp
//  port-forward
//
//  Created by wenxue on 13-10-29.
//  Copyright (c) 2013年 wenx. All rights reserved.
//

#include <iostream>

#include <boost/asio.hpp>

class message_block : public std::enable_shared_from_this<message_block> {

	std::shared_ptr<char> buffer_base_ptr_;
	const std::size_t buffer_length_;
	char *wr_ptr_;
	char *rd_ptr_;

private:
	message_block(std::size_t length)
    : buffer_base_ptr_(std::shared_ptr<char>(new char[length]))
    , buffer_length_(length) {
        assert(length);
        assert(&*buffer_base_ptr_);
        wr_ptr_ = rd_ptr_ = &*buffer_base_ptr_;
    }

	message_block(const message_block &b)
    : buffer_base_ptr_(b.buffer_base_ptr_)
    , buffer_length_(b.buffer_length_)
    , wr_ptr_(b.wr_ptr_)
    , rd_ptr_(b.rd_ptr_) {
    }

public:
	virtual ~message_block() {}

public:
	static std::shared_ptr<message_block> from_size(const uint32_t s) {
        return std::shared_ptr<message_block>(new message_block(s));
	}

public:
	std::size_t capacity() const { return (&*buffer_base_ptr_ + buffer_length_ - wr_ptr_); }
	std::size_t length() const { return (wr_ptr_ - rd_ptr_); }
	bool empty() const { return (wr_ptr_ == rd_ptr_); }

	const char* rd_ptr() const { return rd_ptr_; }
	char* wr_ptr() const { return wr_ptr_; }

public:
	void rd_ptr(const std::size_t n) {
		assert(rd_ptr_ + n >= &*buffer_base_ptr_ && rd_ptr_ + n <= wr_ptr_);
		rd_ptr_ += n;
	}

	void wr_ptr(const std::size_t n) {
		assert(wr_ptr_ + n >= rd_ptr_ && wr_ptr_ + n <= &*buffer_base_ptr_ + buffer_length_);
		wr_ptr_ += n;
	}


};


template<typename T, typename P>
class acceptor : public std::enable_shared_from_this<acceptor<T,P>> {
    boost::asio::io_service &io_service_;
	boost::asio::ip::tcp::acceptor acceptor_;
    P context_;
public:
    acceptor(boost::asio::io_service &io_service, const char *addr, const int port, P &context)
    : io_service_(io_service)
    , acceptor_(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::from_string(addr), port))
    , context_(context) {}
    virtual ~acceptor() {}

public:
    void startup_async_accept() {
        auto peer(std::make_shared<boost::asio::ip::tcp::socket>(io_service_));
		acceptor_.async_accept(*peer,
                               std::bind(&acceptor::async_accept_handler,
                                         std::enable_shared_from_this<acceptor<T,P>>::shared_from_this(),
                                         peer, std::placeholders::_1));
	}
	void async_accept_handler(std::shared_ptr<boost::asio::ip::tcp::socket>  peer, const boost::system::error_code &ec) {
        startup_async_accept();
		if (ec) {
            printf("acceptor::async_accept_handler %d\n", ec.value());
		} else {
            static boost::asio::ip::tcp::no_delay option(true);
            peer->set_option(option);
            auto obj(std::make_shared<T>(io_service_, context_));
			obj->startup(peer);
		}
    }
};


typedef std::shared_ptr<boost::asio::ip::tcp::socket> shared_ptr_peer;

template<typename T>
class forward_peer : public std::enable_shared_from_this<forward_peer<T>> {

    typedef forward_peer<T> THIS_T;

public:
    forward_peer(boost::asio::io_service& io_service, T &ep)
    : io_service_(io_service)
    , forward_endport_(ep)
    {

    }

    ~forward_peer() {}

public:
    void async_connect_handler(shared_ptr_peer incoming_peer,
                               shared_ptr_peer forward_peer,
                               const boost::system::error_code& ec) {
        if (ec) {
            printf("forward_peer::async_connect_handler can not connect to forward target\n");
        } else {


            auto xf = [this](shared_ptr_peer first, shared_ptr_peer second, std::shared_ptr<message_block> buf) {
                first->async_receive(boost::asio::buffer(buf->wr_ptr(), buf->capacity()),
                                     std::bind<int>(&forward_peer::async_receive_handler,
                                                    std::enable_shared_from_this<THIS_T>::shared_from_this(),
                                                    first,
                                                    second,
                                                    buf,
                                                    std::placeholders::_1,
                                                    std::placeholders::_2));
            };

            xf(incoming_peer, forward_peer, message_block::from_size(1024*1024));
            xf(forward_peer, incoming_peer, message_block::from_size(1024*1024));
        }
    }

public:
    int startup(shared_ptr_peer incoming_peer) {
        auto forward_peer(std::make_shared<boost::asio::ip::tcp::socket>(io_service_));
        printf("foward_peer::startup incoming peer %08X\n", &*incoming_peer);
        printf("foward_peer::startup forward peer %08X\n", &*forward_peer);
        forward_peer->async_connect(forward_endport_,
                                    std::bind(&forward_peer::async_connect_handler,
                                              std::enable_shared_from_this<THIS_T>::shared_from_this(),
                                              incoming_peer,
                                              forward_peer,
                                              std::placeholders::_1));
        return 0;
    }

    int async_send_handler(shared_ptr_peer sender,
                           shared_ptr_peer receiver,
                           std::shared_ptr<message_block> buffer,
                           boost::system::error_code ec,
                           size_t bytes_transferred) {
        if (ec) {
            printf("foward_peer::async_send_handler\n");
        } else {
            printf("foward_peer::async_send_handler %08X %u bytes sent\n", &*sender, bytes_transferred);
            buffer->rd_ptr(bytes_transferred);
            if (buffer->empty()) {
                auto xf = [this](shared_ptr_peer receiver, shared_ptr_peer sender, std::shared_ptr<message_block> buf) {
                    receiver->async_receive(boost::asio::buffer(buf->wr_ptr(), buf->capacity()),
                                         std::bind<int>(&forward_peer::async_receive_handler,
                                                        std::enable_shared_from_this<THIS_T>::shared_from_this(),
                                                        receiver,
                                                        sender,
                                                        buf,
                                                        std::placeholders::_1,
                                                        std::placeholders::_2));
                };

                xf(receiver, sender, message_block::from_size(1024*1024));
            } else {
                sender->async_send(boost::asio::buffer(buffer->rd_ptr(), buffer->length()),
                                   std::bind<int>(&forward_peer::async_send_handler,
                                                  std::enable_shared_from_this<THIS_T>::shared_from_this(),
                                                  sender,
                                                  receiver,
                                                  buffer,
                                                  std::placeholders::_1,
                                                  std::placeholders::_2));
            }



        }
        return 0;
    }

    int async_receive_handler(shared_ptr_peer receiver,
                              shared_ptr_peer sender,
                              std::shared_ptr<message_block> buffer,
                              boost::system::error_code ec,
                              size_t bytes_transferred) {
        if (ec) {
            printf("foward_peer::async_receive_handler %d\n", ec.value());
        } else {
            printf("foward_peer::async_receive_handler %08X %u bytes received\n", &*receiver, bytes_transferred);
            buffer->wr_ptr(bytes_transferred);
            {
                char fn[1024];
                memset(fn, 0, sizeof(fn));
                sprintf(fn, "%x.bin", &*receiver);

                FILE *fp = fopen(fn, "ab");
                assert(fp);
                auto rc = fwrite(buffer->rd_ptr(), 1, buffer->length(), fp);
                assert(rc == buffer->length());
                fflush(fp);
                fclose(fp);
            }
            sender->async_send(boost::asio::buffer(buffer->rd_ptr(), buffer->length()),
                               std::bind<int>(&forward_peer::async_send_handler,
                                              std::enable_shared_from_this<THIS_T>::shared_from_this(),
                                              sender,
                                              receiver,
                                              buffer,
                                              std::placeholders::_1,
                                              std::placeholders::_2));

        }
        return 0;
    }

private:
    boost::asio::io_service &io_service_;
    T &forward_endport_;

};

int main(int argc, const char * argv[])
{
    if (argc < 5 || (argc - 1) % 4) {
        printf("USAGE:%s incoming-addr incoming-port forward-addr forward-port\n", argv[0]);
        return 0;
    }
    boost::asio::io_service io_service;

    auto index = 1;

    while (index < argc) {
        const auto incoming_addr = argv[index++];

        const auto incoming_port = atoi(argv[index++]);
        assert(incoming_port);

        const auto forward_addr = argv[index++];

        const auto forward_port = atoi(argv[index++]);
        assert(forward_port);



        boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address_v4::from_string(forward_addr), forward_port);

        auto x = std::make_shared<acceptor<forward_peer<boost::asio::ip::tcp::endpoint>,boost::asio::ip::tcp::endpoint>>(io_service,
                                                                                                                         incoming_addr,
                                                                                                                         incoming_port,
                                                                                                                         ep);
        x->startup_async_accept();
        
    }
    
    printf("startup running ...\n");
    io_service.run();
    return 0;
}

