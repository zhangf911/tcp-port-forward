//
//  main.cpp
//  port-forward
//
//  Created by wenxue on 13-10-29.
//  Copyright (c) 2013年 wenx. All rights reserved.
//

#include <iostream>
#include <thread>
#include <array>

#include <boost/asio.hpp>

#include <message_block.hpp>
#include <helper.hpp>

#include "configure.h"

template<typename T, typename P>
class acceptor : public std::enable_shared_from_this<acceptor<T,P>> {
    std::function<boost::asio::io_service&()> get_io_service_;
	boost::asio::ip::tcp::acceptor acceptor_;
    P context_;

public:
    acceptor(std::function<boost::asio::io_service&()> get_io_service, const char *addr, const int port, P context)
    : get_io_service_(get_io_service)
    , acceptor_(get_io_service_(), boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::from_string(addr), port))
    , context_(context) {}
    virtual ~acceptor() {}

public:
    void startup_async_accept() {
		auto &io_service = get_io_service_();
        auto peer(std::make_shared<boost::asio::ip::tcp::socket>(io_service));
		acceptor_.async_accept(*peer,
                               std::bind(&acceptor::async_accept_handler,
                                         std::enable_shared_from_this<acceptor<T,P>>::shared_from_this(),
                                         peer,
										 std::ref(io_service),
										 std::placeholders::_1));
	}

	void async_accept_handler(std::shared_ptr<boost::asio::ip::tcp::socket> peer,
							  boost::asio::io_service &io_servcie,
							  const boost::system::error_code &ec) {
        startup_async_accept();
		if (ec) {
			print_error("acceptor::async_accept_handler failure %d\n", ec.value());
		} else {
            static boost::asio::ip::tcp::no_delay option(true);
            peer->set_option(option);
            auto obj(std::make_shared<T>(io_servcie, context_));
			obj->startup(peer);
		}
    }
};

typedef std::shared_ptr<boost::asio::ip::tcp::socket> shared_ptr_peer;

template<typename T>
class forward_peer : public std::enable_shared_from_this<forward_peer<T>> {

    typedef forward_peer<T> THIS_T;

public:
    forward_peer(boost::asio::io_service& io_service, T ep)
    : io_service_(io_service)
    , forward_endport_(ep) {}

    ~forward_peer() {}

public:
    void async_connect_handler(shared_ptr_peer incoming_peer,
                               shared_ptr_peer forward_peer,
                               const boost::system::error_code& ec) {
        if (ec) {
            print_error("forward_peer::async_connect_handler failure %d\n", ec.value());
			startup(incoming_peer);
        } else {
            auto xf = [this](shared_ptr_peer first, shared_ptr_peer second) {
				auto buf(message_block::from_size(1024*128));
                first->async_receive(boost::asio::buffer(buf->wr_ptr(), buf->capacity()),//buf->prepare(1024*16),
                                     std::bind<int>(&forward_peer::async_receive_handler,
                                                    std::enable_shared_from_this<THIS_T>::shared_from_this(),
                                                    first,
                                                    second,
                                                    buf,
                                                    std::placeholders::_1,
                                                    std::placeholders::_2));
            };
            xf(incoming_peer, forward_peer);
            xf(forward_peer, incoming_peer);
        }
    }

public:
    int startup(shared_ptr_peer &incoming_peer) {
        auto forward_peer(std::make_shared<boost::asio::ip::tcp::socket>(io_service_));
//		std::cout << "foward_peer::startup incoming peer " << &*incoming_peer << std::endl;
//		std::cout << "foward_peer::startup forward peer " << &*forward_peer << std::endl;
        forward_peer->async_connect(forward_endport_(),
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
			print_error("forward_peer::async_send_handler failure %d\n", ec.value());
        } else {
            buffer->rd_ptr(bytes_transferred);
            if (buffer->length()) {
                sender->async_send(boost::asio::buffer(buffer->data(), buffer->length()),
                                   std::bind<int>(&forward_peer::async_send_handler,
                                                  std::enable_shared_from_this<THIS_T>::shared_from_this(),
                                                  sender,
                                                  receiver,
                                                  buffer,
                                                  std::placeholders::_1,
                                                  std::placeholders::_2));
            } else {
				buffer->reset();
                receiver->async_receive(boost::asio::buffer(buffer->wr_ptr(), buffer->capacity()),
                                        std::bind<int>(&forward_peer::async_receive_handler,
                                                       std::enable_shared_from_this<THIS_T>::shared_from_this(),
                                                       receiver,
                                                       sender,
                                                       buffer,
                                                       std::placeholders::_1,
                                                       std::placeholders::_2));
            }
        }
        return 0;
    }

    void async_send(shared_ptr_peer sender, shared_ptr_peer receiver, std::shared_ptr<message_block> &buffer) {
        sender->async_send(boost::asio::buffer(buffer->data(), buffer->length()),
                           std::bind<int>(&forward_peer::async_send_handler,
                                          std::enable_shared_from_this<THIS_T>::shared_from_this(),
                                          sender,
                                          receiver,
                                          buffer,
                                          std::placeholders::_1,
                                          std::placeholders::_2));
    }

    int async_receive_handler(shared_ptr_peer receiver,
                              shared_ptr_peer sender,
                              std::shared_ptr<message_block> buffer,//std::shared_ptr<boost::asio::streambuf> buffer,
                              boost::system::error_code ec,
                              size_t bytes_transferred) {
        if (ec) {
			print_error("forward_peer::async_receive_handler failure %d\n", ec.value());
        } else {
			buffer->wr_ptr(bytes_transferred);
#if 0
            {
                char fn[1024];
                memset(fn, 0, sizeof(fn));
                sprintf(fn, "%x.bin", &*receiver);

                FILE *fp = fopen(fn, "ab");
                assert(fp);
                auto rc = fwrite(buffer->data().begin(), 1, buffer->size(), fp);
                assert(rc == buffer->size());
                fflush(fp);
                fclose(fp);
            }
#endif
            async_send(sender, receiver, buffer);
        }
        return 0;
    }

private:
    boost::asio::io_service &io_service_;
    T forward_endport_;
};

int main(int argc, const char * argv[])
{
    if (argc < 2) {
        printf("USAGE:%s configure.lua\n", argv[0]);
        return 0;
    }


	if (global::instance().update_configure_from_lua(argv[1])) {
        fprintf(stderr, "::main can not load configure from lua.\n");
        return 1;
    }

    if (global::instance().configure().run_as_daemon()) {
        helper::init_daemon(1,"/tmp",
                            global::instance().configure().has_server_crash_run() ? global::instance().configure().server_crash_run().data() : nullptr);
    }



    loggee::init("loader", 0,
                 1024*1024*100,
                 1);

	print_info("::main startup ...\n");


    std::array<boost::asio::io_service, 8> io_services;
    for (auto &x: io_services) {
        new boost::asio::io_service::work(x);
    }

    auto xf = [](std::array<boost::asio::io_service, 8> &ios) ->boost::asio::io_service& {
        static std::size_t index = 0;
        return ios[++index % ios.size()];
    };

	typedef std::tuple<boost::asio::ip::tcp::endpoint,std::atomic<uint64_t>*> element;
	typedef std::vector<element> endpoints;

	auto lambda_ep_provider = [](std::shared_ptr<endpoints> eps, std::shared_ptr<std::atomic<uint64_t>> counter) ->boost::asio::ip::tcp::endpoint& {
		static uint32_t pos = 0;
		auto &ep = (*eps)[++pos % eps->size()];

		++(*counter);
		++(*std::get<1>(ep));
		return std::ref(std::get<0>(ep));
	};

	for (auto i = 0; i < global::instance().configure().maps_size(); ++i) {
		auto provider = std::make_shared<endpoints>();

		for (auto j = 0; j < global::instance().configure().maps(i).remotes_size(); ++j) {
			auto addr = global::instance().configure().maps(i).remotes(j).addr();
			auto port = global::instance().configure().maps(i).remotes(j).port();

			element e = std::make_tuple(boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::from_string(addr), port), new std::atomic<uint64_t>(0));
			provider->push_back(e);

		}

		typedef std::function<boost::asio::ip::tcp::endpoint&()> endpoint_provider;

		typedef acceptor<forward_peer<endpoint_provider>,endpoint_provider> ACCEPTOR;

		if (!provider->empty()) {

			auto pc = std::make_shared<std::atomic<uint64_t>>(0);
			auto acceptor = std::make_shared<ACCEPTOR>(std::bind(xf, std::ref(io_services)),
													   global::instance().configure().maps(i).local().addr().c_str(),
													   global::instance().configure().maps(i).local().port(),
													   std::bind(lambda_ep_provider, provider, pc));
			acceptor->startup_async_accept();

			struct __helper {
				static void on_timer(std::shared_ptr<boost::asio::deadline_timer> timer,
									 const ::addr_map &am,
									 std::shared_ptr<endpoints> peps,
									 std::shared_ptr<std::atomic<uint64_t>> pc,
									 const boost::system::error_code& ec) {
					if (ec) {
						print_error("::main::__helper::on_timer %d\n", ec.value());
					} else {
						std::string s;
						char buf[1024];
						memset(buf, 0, sizeof(buf));

						sprintf(buf, "%s %u total %llu ", am.local().addr().c_str(), am.local().port(), pc->load());

						s += buf;

						for (auto &x: *peps) {
							memset(buf, 0, sizeof(buf));
							sprintf(buf, "%s %u %llu ", std::get<0>(x).address().to_string().c_str(),
									std::get<0>(x).port(),
									std::get<1>(x)->load());
							s += buf;
						}

						print_info("%s\n", s.c_str());


						timer->expires_from_now(boost::posix_time::seconds(10));
						timer->async_wait(std::bind(__helper::on_timer, timer, std::ref(am), peps, pc, std::placeholders::_1));

					}


				}
			};


			auto timer(std::make_shared<boost::asio::deadline_timer>(xf(io_services)));
            timer->expires_from_now(boost::posix_time::seconds(10));
			timer->async_wait(std::bind(__helper::on_timer, timer, std::ref(global::instance().configure().maps(i)), provider, pc, std::placeholders::_1));
		}
	}


    print_info("startup asio threads pool ...\n");
    for (auto i = 0; i < io_services.size() - 1; ++i) {
        static auto f = static_cast<std::size_t(boost::asio::io_service::*)()>(&boost::asio::io_service::run);
        boost::asio::io_service& io = io_services[i];
        std::thread(std::bind(f, &io)).detach();
    }
    io_services.back().run();
    return 0;
}

