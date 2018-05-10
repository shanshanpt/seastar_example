# seastar_example

### 1. Please clone seastar from : [seastar](https://github.com/scylladb/seastar)

### 2. build seastar:

```
My OS is redhat.

1. Use docker :

Please see details : https://github.com/scylladb/seastar/blob/master/doc/building-docker.md

2. Not use docker:

You may should install some libs as follws:
boost (>= 1.63)
libaio-devel
hwloc-devel
numactl-devel
libpciaccess-devel
cryptopp-devel
libxml2-devel
xfsprogs-devel
gnutls-devel
lksctp-tools-devel
lz4-devel
protobuf-devel
protobuf-compiler
libunwind-devel
systemtap-sdt-devel
libtool
cmake
ninja-build
ragel
binutils
python34

start build:

1) git clone https://github.com/scylladb/seastar.git

2) cd seastar

3) git submodule update --init --recursive

4) sudo ./install-dependencies.sh

5)
./configure.py --compiler=g++  --enable-dpdk  --mode=release
or
./configure.py --compiler=g++  --enable-dpdk  --mode=debug

6) ninja -j10

7) build server:
g++ -std=c++11  -I /home/me/workspace/seastar -L /usr/local/lib64/boost/  `pkg-config --cflags --libs /home/me/workspace/seastar/build/release/seastar.pc` -o server server.cc

8) build client:
g++ -std=c++11  -I /home/me/workspace/seastar -L /usr/local/lib64/boost/  `pkg-config --cflags --libs /home/me/workspace/seastar/build/release/seastar.pc` -o client client.cc

```



### 3.run server

```
Use posix satck:
./server --port=13001 --smp=1 --cpuset=5

Use native stack and dpdk:
[dhcp ip]
./server --port=13001 --smp=1 --cpuset=5 --dpdk-pmd --network-stack=native
[static ip]
./server --port=13001 --smp=1 --cpuset=5 --dpdk-pmd --network-stack=native --dhcp=0 --host-ipv4-addr=11.139.181.166 --gw-ipv4-addr=11.139.181.183 --netmask-ipv4-addr=255.255.255.192
```

### 4.run client

```
./client --smp=1 --cpuset=5 --server=your_ip:13001 --conn=1 --reqs=999999999
```
