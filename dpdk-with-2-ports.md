在 DPDK 中，可以通过合理配置多队列和多核，独立管理两个具有不同 IP 地址的端口的收发操作。这种情况下，两个端口的数据收发完全独立，可以同时进行。以下是实现这种功能的步骤和关键点：

---

### 1. **环境准备**
确保两个端口已绑定到 DPDK 的用户态驱动程序（如 `vfio-pci` 或 `uio_pci_generic`）。

使用 `dpdk-devbind.py` 检查和绑定设备：
```bash
dpdk-devbind.py --status
dpdk-devbind.py -b vfio-pci <device1_pci_id>
dpdk-devbind.py -b vfio-pci <device2_pci_id>
```

---

### 2. **DPDK 初始化**
在程序中初始化 EAL（Environment Abstraction Layer），并获取两个网卡端口：
```c
int ret = rte_eal_init(argc, argv);
if (ret < 0)
    rte_exit(EXIT_FAILURE, "Failed to initialize EAL\n");

// 获取可用端口数
uint16_t nb_ports = rte_eth_dev_count_avail();
if (nb_ports < 2)
    rte_exit(EXIT_FAILURE, "At least 2 ports are required\n");
```

---

### 3. **配置每个端口**
为两个端口分别配置 RX 和 TX 队列，以下是一个典型的配置代码：
```c
struct rte_eth_conf port_conf = {
    .rxmode = {
        .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
    },
};

// 配置端口 0
uint16_t port0 = 0;
uint16_t port1 = 1;

if (rte_eth_dev_configure(port0, 1, 1, &port_conf) < 0)
    rte_exit(EXIT_FAILURE, "Failed to configure port 0\n");
if (rte_eth_dev_configure(port1, 1, 1, &port_conf) < 0)
    rte_exit(EXIT_FAILURE, "Failed to configure port 1\n");

// 分配 RX 和 TX 内存池
struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", 8192,
                                                        256, 0,
                                                        RTE_MBUF_DEFAULT_BUF_SIZE,
                                                        rte_socket_id());

if (!mbuf_pool)
    rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

if (rte_eth_rx_queue_setup(port0, 0, 128, rte_eth_dev_socket_id(port0), NULL, mbuf_pool) < 0)
    rte_exit(EXIT_FAILURE, "Failed to setup RX queue for port 0\n");
if (rte_eth_tx_queue_setup(port0, 0, 128, rte_eth_dev_socket_id(port0), NULL) < 0)
    rte_exit(EXIT_FAILURE, "Failed to setup TX queue for port 0\n");

if (rte_eth_rx_queue_setup(port1, 0, 128, rte_eth_dev_socket_id(port1), NULL, mbuf_pool) < 0)
    rte_exit(EXIT_FAILURE, "Failed to setup RX queue for port 1\n");
if (rte_eth_tx_queue_setup(port1, 0, 128, rte_eth_dev_socket_id(port1), NULL) < 0)
    rte_exit(EXIT_FAILURE, "Failed to setup TX queue for port 1\n");

// 启动端口
if (rte_eth_dev_start(port0) < 0)
    rte_exit(EXIT_FAILURE, "Failed to start port 0\n");
if (rte_eth_dev_start(port1) < 0)
    rte_exit(EXIT_FAILURE, "Failed to start port 1\n");
```

---

### 4. **创建独立的收发逻辑**
为每个端口创建独立的收发线程，使用 DPDK 的 `rte_lcore` 或 POSIX 线程来实现。

#### 示例：每个端口独立的收发逻辑
```c
int lcore_port0_main(void *arg) {
    uint16_t port = *(uint16_t *)arg;
    struct rte_mbuf *bufs[32];
    while (1) {
        // 接收数据包
        uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, 32);
        if (nb_rx > 0) {
            // 对接收的数据进行处理
            for (int i = 0; i < nb_rx; i++) {
                rte_pktmbuf_free(bufs[i]); // 示例中直接释放
            }
        }

        // 发送数据包（如有需要）
        // rte_eth_tx_burst(port, 0, tx_bufs, tx_count);
    }
}

int lcore_port1_main(void *arg) {
    uint16_t port = *(uint16_t *)arg;
    struct rte_mbuf *bufs[32];
    while (1) {
        uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, 32);
        if (nb_rx > 0) {
            for (int i = 0; i < nb_rx; i++) {
                rte_pktmbuf_free(bufs[i]);
            }
        }
    }
}
```

#### 分配线程到核
使用 `rte_eal_remote_launch` 将逻辑绑定到指定核心：
```c
uint16_t port0 = 0, port1 = 1;
rte_eal_remote_launch(lcore_port0_main, &port0, 1); // 核 1 处理端口 0
rte_eal_remote_launch(lcore_port1_main, &port1, 2); // 核 2 处理端口 1
rte_eal_mp_wait_lcore(); // 等待所有核心完成
```

---

### 5. **运行和调试**
确保两个端口的 IP 地址在 DPDK 程序外通过网卡配置工具（如 `ifconfig` 或 `ip`) 设置正确。如果需要程序内部控制 IP，可以结合 `rte_lpm` 等库自行实现。

---

### 6. **注意事项**
- 确保网卡多队列和 RSS（接收侧缩放）配置正确，如果使用多核收发，可以扩展到每端口多队列模式。
- 注意 CPU 核的绑定，避免多个线程共享同一个核心。
- 使用 `rte_eth_stats_get` 检查端口的收发统计信息以进行调试。
- 如果要区分数据类型，可以结合包头解析逻辑（如解析以太网或 IP 头）。

通过这些步骤，即可实现两个端口的独立收发操作，并充分利用 DPDK 的高性能网络处理能力。
