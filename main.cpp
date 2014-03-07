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


typedef std::list<std::shared_ptr<message_block>> buffers_list;
typedef std::tuple<std::shared_ptr<boost::asio::ip::tcp::socket>, buffers_list> forwared_peer_context;

template<typename T>
class forward_peer : public std::enable_shared_from_this<forward_peer<T>> {

    typedef forward_peer<T> THIS_T;

public:
    forward_peer(boost::asio::io_service& io_service, T ep)
    : io_service_(io_service)
    , forward_endport_(ep) {}

    ~forward_peer() {}

private:
	void async_receive_startup(std::shared_ptr<forwared_peer_context> receiver,
							   std::shared_ptr<forwared_peer_context> sender) {
		auto buffer(message_block::from_size(1024*32));
		std::get<0>(*receiver)->async_receive(boost::asio::buffer(buffer->wr_ptr(), buffer->capacity()),
											  std::bind<int>(&forward_peer::async_receive_handler,
															 std::enable_shared_from_this<THIS_T>::shared_from_this(),
															 receiver,
															 sender,
															 buffer,
															 std::placeholders::_1,
															 std::placeholders::_2));
	};

public:
    void async_connect_handler(std::shared_ptr<forwared_peer_context> receiver,
                               std::shared_ptr<forwared_peer_context> sender,
                               const boost::system::error_code& ec) {
        if (ec) {
            print_error("forward_peer::async_connect_handler failure %d, retry next element.\n", ec.value());
			startup(std::get<0>(*receiver));
        } else {
			async_receive_startup(receiver, sender);
			async_receive_startup(sender, receiver);
        }
    }

public:
    int startup(std::shared_ptr<boost::asio::ip::tcp::socket> &incoming_peer) {
        auto forward_peer(std::make_shared<boost::asio::ip::tcp::socket>(io_service_));
        forward_peer->async_connect(forward_endport_(),
                                    std::bind(&forward_peer::async_connect_handler,
                                              std::enable_shared_from_this<THIS_T>::shared_from_this(),
                                              std::make_shared<forwared_peer_context>(std::make_tuple(incoming_peer, buffers_list())),
                                              std::make_shared<forwared_peer_context>(std::make_tuple(forward_peer, buffers_list())),
                                              std::placeholders::_1));
        return 0;
    }

    int async_send_handler(std::shared_ptr<forwared_peer_context> receiver,
						   std::shared_ptr<forwared_peer_context> sender,
                           boost::system::error_code ec,
                           size_t bytes_transferred) {
        if (ec) {
			print_error("forward_peer::async_send_handler failure %d\n", ec.value());
			shutdown(receiver);
        } else {
			auto &buffer = std::get<1>(*sender).front();
            buffer->rd_ptr(bytes_transferred);

			if (buffer->empty()) {
				std::get<1>(*sender).pop_front();
			}

			if (!std::get<1>(*sender).empty()) {
				auto &buf = std::get<1>(*sender).front();
				std::get<0>(*sender)->async_send(boost::asio::buffer(buf->data(), buf->length()),
												 std::bind(&forward_peer::async_send_handler,
														   std::enable_shared_from_this<THIS_T>::shared_from_this(),
														   receiver,
														   sender,
														   std::placeholders::_1,
														   std::placeholders::_2));
			}
        }
        return 0;
    }

	void shutdown(std::shared_ptr<forwared_peer_context> &peer) {
		try {
			boost::system::error_code ec;
			auto s = std::get<0>(*peer);
			if (s) {
				s->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
			}
		} catch (std::exception e) {
			print_error("forward_peer::shutdown %s", e.what());
		}
	}

    int async_receive_handler(std::shared_ptr<forwared_peer_context> receiver,
                              std::shared_ptr<forwared_peer_context> sender,
                              std::shared_ptr<message_block> buffer,
                              boost::system::error_code ec,
                              size_t bytes_transferred) {
        if (ec || bytes_transferred == 0) {
			print_error("forward_peer::async_receive_handler failure %d\n", ec.value());
			shutdown(sender);
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

			async_receive_startup(receiver, sender);

			auto &buffers = std::get<1>(*sender);
			if (buffers.empty()) {
				auto &buffer = buffers.front();
				std::get<0>(*sender)->async_send(boost::asio::buffer(buffer->data(), buffer->length()),
												 std::bind(&forward_peer::async_send_handler,
														   std::enable_shared_from_this<THIS_T>::shared_from_this(),
														   receiver,
														   sender,
														   std::placeholders::_1,
														   std::placeholders::_2));
			} else {
				buffers.push_back(buffer);
			}
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

	typedef std::tuple<boost::asio::ip::tcp::endpoint,std::shared_ptr<std::atomic<uint64_t>>> element;
	typedef std::vector<element> endpoints;

	auto lambda_ep_provider = [](std::shared_ptr<endpoints> eps, std::shared_ptr<std::atomic<uint64_t>> total_dispatched) ->boost::asio::ip::tcp::endpoint& {
		static uint32_t pos = 0;
		auto &ep = (*eps)[++pos % eps->size()];

		++(*total_dispatched);
		++(*std::get<1>(ep));
		return std::ref(std::get<0>(ep));
	};

	for (auto i = 0; i < global::instance().configure().maps_size(); ++i) {
		auto provider = std::make_shared<endpoints>();

		for (auto j = 0; j < global::instance().configure().maps(i).remotes_size(); ++j) {
			auto addr = global::instance().configure().maps(i).remotes(j).addr();
			auto port = global::instance().configure().maps(i).remotes(j).port();

			element e = std::make_tuple(boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::from_string(addr), port), std::make_shared<std::atomic<uint64_t>>(0));
			provider->push_back(e);

		}

		typedef std::function<boost::asio::ip::tcp::endpoint&()> endpoint_provider;

		typedef acceptor<forward_peer<endpoint_provider>,endpoint_provider> ACCEPTOR;

		if (!provider->empty()) {

			auto total_dispatched = std::make_shared<std::atomic<uint64_t>>(0);
			auto acceptor = std::make_shared<ACCEPTOR>(std::bind(xf, std::ref(io_services)),
													   global::instance().configure().maps(i).local().addr().c_str(),
													   global::instance().configure().maps(i).local().port(),
													   std::bind(lambda_ep_provider, provider, total_dispatched));
			acceptor->startup_async_accept();

			struct __helper {
				static void on_timer(std::shared_ptr<boost::asio::deadline_timer> timer,
									 const ::addr_map &am,
									 std::shared_ptr<endpoints> peps,
									 std::shared_ptr<std::atomic<uint64_t>> total_dispatched,
									 const boost::system::error_code& ec) {
					if (ec) {
						print_error("::main::__helper::on_timer %d\n", ec.value());
					} else {
						std::string s;
						char buf[1024];
						memset(buf, 0, sizeof(buf));

						sprintf(buf, "%s %u total %lu ", am.local().addr().c_str(), am.local().port(), total_dispatched->load());

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
						timer->async_wait(std::bind(__helper::on_timer,
													timer,
													std::ref(am),
													peps,
													total_dispatched,
													std::placeholders::_1));

					}


				}
			};


			auto timer(std::make_shared<boost::asio::deadline_timer>(xf(io_services)));
            timer->expires_from_now(boost::posix_time::seconds(10));
			timer->async_wait(std::bind(__helper::on_timer,
										timer,
										std::ref(global::instance().configure().maps(i)),
										provider,
										total_dispatched,
										std::placeholders::_1));
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

