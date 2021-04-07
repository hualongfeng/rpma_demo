Demo for RPMA with pmem
===

The demo implements two parts of the messaging process:
- The server starts a listening endpoint and waits for an incoming connection.
When a new connection request appears it is accepted. The client sends to
the server its need value, such as size, pmem path. When the server receives the message
from the client, malloc or pmem_map_file size memory and send its descriptor back to the client.
Then the server waits for disconnection.
- The client connects to the server and sends to it its value the information it needs.
When the client gets the decriptor from the server. Then the client start to write data to server, when to end, disconnects.

## Usage

```bash
[user@server]$ ./server $server_address $port
```

```bash
[user@client]$ ./client $server_address $port
```

## Dependence

### [pmdk](https://pmem.io/pmdk/manpages/linux/master/libpmem/pmem_flush.3)

#### install [ndctl](https://github.com/pmem/ndctl) 

```bash
$ git clone https://github.com/pmem/ndctl.git
$ git checkout v71.1
$ ./autogen.sh
$ ./configure CFLAGS='-g -O2' --prefix=/usr --sysconfdir=/etc --libdir=/usr/lib
#  执行上面语句时可能缺失一些依赖包
# sudo apt install -y asciidoctor libkmod-dev libfastjson-dev libjson-c-dev
$ make
$ make check
$ sudo make install
$ ndctl -v #检验安装是否正常
```

> 遇到的问题：
>
> ```bash
> $ ndctl -v
> ndctl: /usr/lib/x86_64-linux-gnu/libdaxctl.so.1: version `LIBDAXCTL_6' not found (required by ndctl)
> ndctl: /usr/lib/x86_64-linux-gnu/libdaxctl.so.1: version `LIBDAXCTL_8' not found (required by ndctl)
> ndctl: /usr/lib/x86_64-linux-gnu/libdaxctl.so.1: version `LIBDAXCTL_7' not found (required by ndctl)
> $ sudo find / -name "libdaxctl.so.1"
> /usr/lib/x86_64-linux-gnu/libdaxctl.so.1
> /usr/lib/libdaxctl.so.1
> /home/ssp/ndctl/daxctl/lib/.libs/libdaxctl.so.1
> $ ll /usr/lib/x86_64-linux-gnu/* | grep daxctl
> lrwxrwxrwx   1 root root       18 8月   2  2018 libdaxctl.so.1 -> libdaxctl.so.1.2.0
> -rw-r--r--   1 root root    18352 8月   2  2018 libdaxctl.so.1.2.0
> $ ll /usr/lib/libdaxctl.so.1
> lrwxrwxrwx 1 root root 18 4月   1 14:03 /usr/lib/libdaxctl.so.1 -> libdaxctl.so.1.5.0*
> $ sudo rm /usr/lib/x86_64-linux-gnu/libdaxctl.so.1 /usr/lib/x86_64-linux-gnu/libdaxctl.so.1.2.0
> $ ndctl -v
> 71.1
> ```

#### build pmdk

```bash
$ git clone https://github.com/pmem/pmdk.git
$ cd pmdk/
$ git checkout tags/1.10
$ make
$ sudo make install
$ sudo ldconfig
```

> 如果出现需要的文件和实际存在的目录不一致的问题，删除该目录，重新clone, checkout, make

#### check and enable pmem

```bash
$ sudo ndctl create-namespace --force --reconfig=namespace0.0 --mode=fsdax --map=dev --size=200G
  Error: create namespace: namespace0.0 failed to enable for zeroing, continuing

{
  "dev":"namespace0.0",
  "mode":"fsdax",
  "map":"dev",
  "size":"196.87 GiB (211.39 GB)",
  "uuid":"799aceb7-8f0a-4b2a-a9a5-06fcb1e891bc",
  "sector_size":512,
  "align":2097152,
  "blockdev":"pmem0"
}
# 这个命令仅当需要删除这个pmem设备时执行
$ sudo ndctl destroy-namespace namespace0.0 -f
destroyed 1 namespace
$ sudo mkfs -t xfs /dev/pmem0
meta-data=/dev/pmem0             isize=512    agcount=4, agsize=12902272 blks
         =                       sectsz=4096  attr=2, projid32bit=1
         =                       crc=1        finobt=1, sparse=0, rmapbt=0, reflink=0
data     =                       bsize=4096   blocks=51609088, imaxpct=25
         =                       sunit=0      swidth=0 blks
naming   =version 2              bsize=4096   ascii-ci=0 ftype=1
log      =internal log           bsize=4096   blocks=25199, version=2
         =                       sectsz=4096  sunit=1 blks, lazy-count=1
realtime =none                   extsz=4096   blocks=0, rtextents=0
$ sudo mount -o dax /dev/pmem0 /mnt
# $ truncate -s 4G temp  # in test directory
```

```c
// pmem_map_demo.c
#include <inttypes.h>
#include <stdio.h>
#include <libpmem.h>

int main() {
  char path[] = "/mnt/temp";
  remove(path);
  size_t mr_size = 0;
  int is_pmem = 0;
  void* ptr = pmem_map_file(path, 1000, PMEM_FILE_CREATE | PMEM_FILE_SPARSE, 0600, &mr_size, &is_pmem);
  printf("pmem? %d %p\n", is_pmem, ptr);
  printf("size = %ld\n", mr_size);
  uint64_t num= 0x1234567812345678;
  uint64_t *num_ptr = ptr;
  *num_ptr = num;
  printf("unmap: %d\n", pmem_unmap(ptr, mr_size));
  return 0;
}
```

对`pmem_map_demo.c`进行编译，使用`gcc pmem.c -lpmem`.

可查看test/pmem0这个文件，会发现该文件的开始位置已经被赋值。查看方式: `vim`  -> `:%!xxd` -> `:%!xxd`r - > `:q!`.

### [librpma](https://pmem.io/rpma/manpages/master/librpma.7.html)

#### DESCRIPTION

librpma is a C library to simplify accessing persistent memory (PMem) on remote hosts over Remote Direct Memory Access (RDMA).

The librpma library provides two possible schemes of operation: **Remote Memory Access** and **Messaging**. 

### rpma_dependence

[Download Page for libibverbs-dev_17.1-1_amd64.deb on AMD64 machines](https://packages.ubuntu.com/bionic/amd64/libibverbs-dev/download)

You should be able to use any of the listed mirrors by adding a line to your /etc/apt/sources.list like this:

```
deb http://cz.archive.ubuntu.com/ubuntu bionic main
```

```bash
$ sudo apt install -y libibverbs-dev
$ sudo apt install -y librdmacm-dev
$ sudo apt install -y libcmocka-dev
$ sudo apt install -y librdmacm-dev
$ sudo apt install -y librte-pmd-mlx4-17.11
$ sudo apt install -y libprotobuf-c-dev
$ sudo apt install -y txt2man
$ sudo apt install -y rdmacm-utils
$ sudo apt install -y rdma-core
# ulimit -a
```

### rpma_build
[Re: Cannot allocate memory error when using RDMA](https://www.mail-archive.com/search?l=dev@crail.apache.org&q=subject:"Re\%3A+Cannot+allocate+memory+error+when+using+RDMA"&o=newest)

> ```
> Can you check if you have "max locked memory" set to a high enough value or unlimited (use "ulimit -l"). You can ``set memlock in /etc/security/limits.conf e.g.:
> *            soft    memlock         unlimited
> *            hard    memlock         unlimited
> ```

```bash
$ git clone https://github.com/pmem/rpma.git
$ cd rpma
$ git checkout 0.9.0 # optional
$ mkdir build && cd build
$ cmake ..
$ make
```



## Build
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..  # Debug version, or Release version
make
```

