# NS3-simple

# prompt
NS3实现一个4个端口的交换机节点。交换机在接收到一个数据包的时候要给它添加tag，tag中携带的内容是一个数值，记录了该数据包从哪个端口接收到的。数据包从端口出队并发送时，要从8个队列进行选择，按照队列1-8的权重分别是8、7、6、5、4、3、2、1，来进行数据包的出队列。server上的队列需要是普通的fifo队列。

# GPT

在交换机节点上实现8个加权队列（Weighted Round Robin，WRR），并在服务器节点上使用普通的FIFO队列。以下是详细的实现步骤：

创建交换机节点，并在收到数据包时给它添加记录接收端口的tag。

实现加权轮询调度算法在交换机节点上从8个队列中出队数据包。

服务器节点上的队列使用普通的FIFO队列。
# 实现步骤

## 1. 定义自定义 Tag

首先，定义一个自定义的 tag 用于记录数据包的接收端口。

```cpp
#include "ns3/core-module.h"

using namespace ns3;

class PortTag : public Tag {
public:
  static TypeId GetTypeId(void) {
    static TypeId tid = TypeId("PortTag")
      .SetParent<Tag>()
      .AddConstructor<PortTag>();
    return tid;
  }

  PortTag() : m_port(0) {}
  PortTag(uint32_t port) : m_port(port) {}

  void SetPort(uint32_t port) {
    m_port = port;
  }

  uint32_t GetPort() const {
    return m_port;
  }

  void Serialize(TagBuffer i) const override {
    i.WriteU32(m_port);
  }

  void Deserialize(TagBuffer i) override {
    m_port = i.ReadU32();
  }

  void Print(std::ostream &os) const override {
    os << "Port=" << m_port;
  }

  TypeId GetInstanceTypeId(void) const override {
    return GetTypeId();
  }

private:
  uint32_t m_port;
};
```

## 2. 实现自定义 QueueDisc

实现一个自定义的 QueueDisc 类，用于在交换机节点上从8个队列中出队数据包。

``` cpp
#include "ns3/queue-disc.h"
#include "ns3/log.h"
#include "port-tag.h"  // 自定义的 Tag 类

using namespace ns3;

class WeightedQueueDisc : public QueueDisc {
public:
  static TypeId GetTypeId(void);
  WeightedQueueDisc();
  virtual ~WeightedQueueDisc();

private:
  virtual bool DoEnqueue(Ptr<QueueDiscItem> item) override;
  virtual Ptr<QueueDiscItem> DoDequeue(void) override;
  virtual Ptr<const QueueDiscItem> DoPeek(void) const override;
  virtual bool CheckConfig(void) override;
  virtual void InitializeParams(void) override;

  std::vector<uint32_t> m_weights;
  std::vector<uint32_t> m_counters;
  uint32_t m_currentQueue;
};

NS_OBJECT_ENSURE_REGISTERED(WeightedQueueDisc);

TypeId
WeightedQueueDisc::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::WeightedQueueDisc")
    .SetParent<QueueDisc>()
    .SetGroupName("TrafficControl")
    .AddConstructor<WeightedQueueDisc>();
  return tid;
}

WeightedQueueDisc::WeightedQueueDisc()
  : m_currentQueue(0) {
  m_weights = {8, 7, 6, 5, 4, 3, 2, 1};
  m_counters = m_weights;
}

WeightedQueueDisc::~WeightedQueueDisc() {}

bool
WeightedQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item) {
  Ptr<Packet> packet = item->GetPacket();
  PortTag tag;
  if (packet->PeekPacketTag(tag)) {
    uint32_t port = tag.GetPort();
    uint32_t queueIndex = port % 8; // 根据端口号选择队列

    if (GetInternalQueue(queueIndex)->Enqueue(item)) {
      return true;
    } else {
      return false;
    }
  }
  return false;
}

Ptr<QueueDiscItem>
WeightedQueueDisc::DoDequeue(void) {
  for (uint32_t i = 0; i < GetNInternalQueues(); ++i) {
    if (m_counters[m_currentQueue] > 0) {
      Ptr<QueueDiscItem> item = GetInternalQueue(m_currentQueue)->Dequeue();
      if (item) {
        m_counters[m_currentQueue]--;
        return item;
      }
    }
    m_currentQueue = (m_currentQueue + 1) % GetNInternalQueues();
    if (m_counters[m_currentQueue] == 0) {
      m_counters[m_currentQueue] = m_weights[m_currentQueue];
    }
  }
  return nullptr;
}

Ptr<const QueueDiscItem>
WeightedQueueDisc::DoPeek(void) const {
  for (uint32_t i = 0; i < GetNInternalQueues(); ++i) {
    Ptr<const QueueDiscItem> item = GetInternalQueue(i)->Peek();
    if (item) {
      return item;
    }
  }
  return nullptr;
}

bool
WeightedQueueDisc::CheckConfig(void) {
  if (GetNInternalQueues() != 8) {
    NS_LOG_ERROR("WeightedQueueDisc needs 8 internal queues.");
    return false;
  }
  return true;
}

void
WeightedQueueDisc::InitializeParams(void) {}
```

## 3. 配置交换机和模拟环境

在主程序中使用自定义的 QueueDisc 并配置交换机和模拟环境。

``` cpp
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/udp-header.h"
#include "ns3/application-container.h"
#include "ns3/on-off-helper.h"
#include "ns3/packet-sink-helper.h"
#include "port-tag.h"  // 自定义的 Tag 类

using namespace ns3;

class SwitchApp : public Application {
public:
  SwitchApp() {}
  void Setup(Ptr<Node> node, std::vector<Ptr<Socket>> ports);

private:
  virtual void StartApplication(void) override;
  virtual void StopApplication(void) override;
  void HandleRead(Ptr<Socket> socket);

  Ptr<Node> m_node;
  std::vector<Ptr<Socket>> m_ports;
};

void
SwitchApp::Setup(Ptr<Node> node, std::vector<Ptr<Socket>> ports) {
  m_node = node;
  m_ports = ports;
}

void
SwitchApp::StartApplication(void) {
  for (uint32_t i = 0; i < m_ports.size(); ++i) {
    m_ports[i]->SetRecvCallback(MakeCallback(&SwitchApp::HandleRead, this));
  }
}

void
SwitchApp::StopApplication(void) {
  for (uint32_t i = 0; i < m_ports.size(); ++i) {
    m_ports[i]->Close();
  }
}

void
SwitchApp::HandleRead(Ptr<Socket> socket) {
  Ptr<Packet> packet;
  Address from;
  while ((packet = socket->RecvFrom(from))) {
    uint32_t portIndex = std::distance(m_ports.begin(), std::find(m_ports.begin(), m_ports.end(), socket));
    PortTag tag(portIndex);
    packet->AddPacketTag(tag);

    InetSocketAddress address = InetSocketAddress::ConvertFrom(from);
    Ipv4Address dstAddr = address.GetIpv4();

    Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4>();
    int32_t ifIndex = ipv4->GetInterfaceForAddress(dstAddr);
    Ptr<NetDevice> device = ipv4->GetNetDevice(ifIndex);
    Ptr<PointToPointNetDevice> ptpDevice = DynamicCast<PointToPointNetDevice>(device);
    ptpDevice->Send(packet, dstAddr, Ipv4L3Protocol::PROT_NUMBER);
  }
}

int main(int argc, char *argv[]) {
  // 启用日志组件
  LogComponentEnable("SwitchExample", LOG_LEVEL_INFO);

  // 创建节点容器
  NodeContainer hosts;
  hosts.Create(4); // 创建四个主机节点

  NodeContainer switchNode;
  switchNode.Create(1); // 创建一个交换机节点

  // 创建和配置 PointToPointHelper
  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
  pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

  NetDeviceContainer devices;

  // 将每个主机节点连接到交换机节点
  for (uint32_t i = 0; i < hosts.GetN(); ++i) {
    NetDeviceContainer link = pointToPoint.Install(NodeContainer(hosts.Get(i), switchNode.Get(0)));
    devices.Add(link);
  }

  // 安装网络协议栈
  InternetStackHelper stack;
  stack.Install(hosts);
  stack.Install(switchNode);

  // 配置 IP 地址
  Ipv4AddressHelper address;
  for (uint32_t i = 0; i < hosts.GetN(); ++i) {
    std::ostringstream subnet;
    subnet << "10.1." << i + 1 << ".0";
    address.SetBase(subnet.str().c_str(), "255.255.255.0");

    Ipv4InterfaceContainer interfaces = address.Assign(devices.Get(i * 2));
    address.Assign(devices.Get(i * 2 + 1));
  }

  // 配置自定义队列管理器并为交换机端口设置 8 个队列
  TrafficControlHelper tch;
  tch.SetRootQueueDisc("ns3::WeightedQueueDisc");

  tch.Install(devices.Get(2)); // 安装在交换机的第一个端口上
  tch.Install(devices.Get(3)); // 安装在交换机的第二个端口上

  // 创建并安装交换机应用程序
  std::vector<Ptr<Socket>> ports;
  for (uint32_t i = 0; i < 4; ++i) {
    Ptr<Socket> socket = Socket::CreateSocket(switchNode.Get(0), TypeId::LookupByName("ns3::UdpSocketFactory"));
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), 9 + i);
    socket->Bind(local);
    ports.push_back(socket);
  }

  Ptr<SwitchApp> switchApp = CreateObject<SwitchApp>();
  switchApp->Setup(switchNode.Get(0), ports);
  switchNode.Get(0)->AddApplication(switchApp);
  switchApp->SetStartTime(Seconds(1.0));
  switchApp->SetStopTime(Seconds(10.0));

  // 创建和配置应用程序
  uint16_t port = 9;
  OnOffHelper onOff("ns3::UdpSocketFactory", Address(InetSocketAddress(Ipv4Address("10.1.1.2"), port)));
  onOff.SetConstantRate(DataRate("500kbps"));
  ApplicationContainer app = onOff.Install(hosts.Get(0));
  app.Start(Seconds(1.0));
  app.Stop(Seconds(10.0));

  PacketSinkHelper sink("ns3::UdpSocketFactory", Address(InetSocketAddress(Ipv4Address::GetAny(), port)));
  app = sink.Install(hosts.Get(1));
  app.Start(Seconds(1.0));
  app.Stop(Seconds(10.0));

  // 启用全局路由
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // 运行仿真
  Simulator::Run();
  Simulator::Destroy();

  return 0;
}
```

解释

自定义 Tag：PortTag 用于记录数据包接收端口。

自定义 QueueDisc：WeightedQueueDisc 实现了加权轮询调度算法，根据权重选择队列出队数据包。

交换机应用程序：SwitchApp 类在接收到数据包时添加 PortTag，并将数据包发送到目的地。

主程序：配置交换机节点，安装网络协议栈，配置 IP 地址，配置自定义队列管理器，创建并安装交换机应用程序，运行仿真。

通过上述步骤，可以在 NS-3 中实现一个带有 4 个端口的交换机节点，并根据 UDP 值和加权轮询调度算法进行数据包调度和发送，服务器上的队列为普通的FIFO队列。
