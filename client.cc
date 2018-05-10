#include "core/print.hh"
#include "core/reactor.hh"
#include "core/app-template.hh"
#include "core/future-util.hh"
#include "core/distributed.hh"
#include "core/semaphore.hh"
#include "core/future-util.hh"
#include <chrono>
#include <array>

using namespace seastar;

static size_t buf_size = 128; //128; //128;//1600;
static std::string str_txbuf(buf_size, 'X'); 

class tcp_echo_client {
private:
    unsigned _conn_per_core {0};
    unsigned _reqs_per_conn {0};
    std::vector<connected_socket> _sockets;
    semaphore _conn_connected {0};
    semaphore _conn_finished {0};
    timer<> _run_timer;
    bool _timer_done {false};
    uint64_t _total_reqs {0};
public:
    tcp_echo_client(unsigned total_conn, unsigned reqs_per_conn)
        : _conn_per_core(total_conn / smp::count)
        , _reqs_per_conn(reqs_per_conn)
        , _run_timer([this] { _timer_done = true; })
    {
    }

    class connection {
    public:
        connected_socket _fd;
        input_stream<char> _in;
        output_stream<char> _out;
        tcp_echo_client& _echo_client;
        uint64_t _nr_done{0};
    public:
        connection(connected_socket&& fd, tcp_echo_client& echo_client)
            : _fd(std::move(fd))
            , _in(_fd.input())
            , _out(_fd.output())
            , _echo_client(echo_client)
        {
        }

        ~connection() {
        }

        future<> do_launch_request() {
            net::fragment frag { const_cast<char*>(str_txbuf.c_str()), buf_size };
            net::packet pack(frag, deleter());
            return _out.write(std::move(pack)).then([this] {
                return _out.flush();
            }).then([this] {
                return _in.read_exactly(buf_size).then([this] (auto&& data) {
                    _nr_done++;
                    if (_echo_client.done(_nr_done)) {
                        return make_ready_future<>();
                    }
                    return this->do_launch_request();
                });
            });
        }
    };

    future<uint64_t> total_reqs() {
        print("Requests on cpu %2d: %ld\n", engine().cpu_id(), _total_reqs);
        return make_ready_future<uint64_t>(_total_reqs);
    }

    bool done(uint64_t nr_done) {
        return nr_done >= _reqs_per_conn;
    }

    future<> connect(ipv4_addr server_addr) {
        for (unsigned i = 0; i < _conn_per_core; i++) {
            engine().net().connect(make_ipv4_address(server_addr)).then([this] (connected_socket fd) {
                _sockets.push_back(std::move(fd));
                print("Established connection %6d on cpu %3d\n", _conn_connected.current(), engine().cpu_id());
                _conn_connected.signal();
            }).or_terminate();
        }

        return _conn_connected.wait(_conn_per_core);
    }

    future<> run() {
        print("Established all %6d tcp connections on cpu %3d\n", _conn_per_core, engine().cpu_id());

        for (auto&& fd : _sockets) {
            auto conn = make_lw_shared<connection>(std::move(fd), *this);

            // read
            keep_doing([conn, this] () {
                return conn->_in.read_exactly(buf_size).then([this] (auto&& data) {
                });
            }).then([conn] {
                conn->_out.close();
            });

            // write
            keep_doing([conn, this] () {
                net::fragment frag { const_cast<char*>(str_txbuf.c_str()), buf_size };
                net::packet pack(frag, deleter());
                return conn->_out.write(std::move(pack)).then([this, conn] {
                    conn->_out.flush();
                });
            }).then([conn] {
                conn->_out.close();
            });
            
        }

        return _conn_finished.wait(_conn_per_core);
    }

    future<> stop() {
        return make_ready_future();
    }
};

namespace bpo = boost::program_options;

int main(int ac, char** av) {
    distributed<tcp_echo_client> shard_echo_client;
    
    app_template app;
    app.add_options()
        ("server,s", bpo::value<std::string>()->default_value("127.0.0.1:10000"), "Server address")
        ("conn,c", bpo::value<unsigned>()->default_value(100), "total connections")
        ("reqs,r", bpo::value<unsigned>()->default_value(0), "reqs per connection");

    // run app
    return app.run(ac, av, [&] () -> future<int> {
        auto& config = app.configuration();
        auto server = config["server"].as<std::string>();
        auto reqs_per_conn = config["reqs"].as<unsigned>();
        auto total_conn= config["conn"].as<unsigned>();

        if (total_conn % smp::count != 0) {
            print("Error: conn needs to be n * cpu_nr\n");
            return make_ready_future<int>(-1);
        }

        return shard_echo_client.start(std::move(total_conn), std::move(reqs_per_conn)).then([&shard_echo_client, server] {
            return shard_echo_client.invoke_on_all(&tcp_echo_client::connect, ipv4_addr{server});
        }).then([&shard_echo_client] {
            return shard_echo_client.invoke_on_all(&tcp_echo_client::run);
        }).then([&shard_echo_client] {
            return shard_echo_client.map_reduce(adder<uint64_t>(), &tcp_echo_client::total_reqs);
        }).then([&shard_echo_client] (auto r) {
           return shard_echo_client.stop().then([] {
               return make_ready_future<int>(0);
           });
        });
    });
}
