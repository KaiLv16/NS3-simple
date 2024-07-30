#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

int main(int argc, char *argv[])
{
  // 允许日志记录
  LogComponentEnable("TcpCongestionComparison", LOG_LEVEL_INFO);

  // 创建节点
  NodeContainer nodes;
  nodes.Create(4);

  // 创建 P2P 链路
  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
  pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

  // 创建网络设备和安装链路
  NetDeviceContainer devices1 = pointToPoint.Install(nodes.Get(0), nodes.Get(1));
  NetDeviceContainer devices2 = pointToPoint.Install(nodes.Get(2), nodes.Get(3));

  // 安装网络协议栈
  InternetStackHelper stack;
  stack.Install(nodes);

  // 分配 IP 地址
  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces1 = address.Assign(devices1);

  address.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces2 = address.Assign(devices2);

  // 创建 TCP 套接字工厂
  TypeId tcpNewRenoTypeId = TypeId::LookupByName("ns3::TcpNewReno");
  TypeId tcpCubicTypeId = TypeId::LookupByName("ns3::TcpCubic");

  // 设置第一个节点的默认 TCP 套接字工厂为 TCP NewReno
  Config::Set("/NodeList/0/$ns3::TcpL4Protocol/SocketType", TypeIdValue(tcpNewRenoTypeId));
  // 设置第二个节点的默认 TCP 套接字工厂为 TCP Cubic
  Config::Set("/NodeList/2/$ns3::TcpL4Protocol/SocketType", TypeIdValue(tcpCubicTypeId));

  // 安装应用程序
  uint16_t port = 8080;

  // 创建并安装 TCP NewReno 应用程序
  Address sinkAddress1(InetSocketAddress(interfaces1.GetAddress(1), port));
  PacketSinkHelper packetSinkHelper1("ns3::TcpSocketFactory", sinkAddress1);
  ApplicationContainer sinkApps1 = packetSinkHelper1.Install(nodes.Get(1));
  sinkApps1.Start(Seconds(0.0));
  sinkApps1.Stop(Seconds(10.0));

  OnOffHelper client1("ns3::TcpSocketFactory", sinkAddress1);
  client1.SetAttribute("DataRate", DataRateValue(DataRate("1Mbps")));
  client1.SetAttribute("PacketSize", UintegerValue(1024));
  ApplicationContainer clientApps1 = client1.Install(nodes.Get(0));
  clientApps1.Start(Seconds(1.0));
  clientApps1.Stop(Seconds(10.0));

  // 创建并安装 TCP Cubic 应用程序
  Address sinkAddress2(InetSocketAddress(interfaces2.GetAddress(1), port));
  PacketSinkHelper packetSinkHelper2("ns3::TcpSocketFactory", sinkAddress2);
  ApplicationContainer sinkApps2 = packetSinkHelper2.Install(nodes.Get(3));
  sinkApps2.Start(Seconds(0.0));
  sinkApps2.Stop(Seconds(10.0));

  OnOffHelper client2("ns3::TcpSocketFactory", sinkAddress2);
  client2.SetAttribute("DataRate", DataRateValue(DataRate("1Mbps")));
  client2.SetAttribute("PacketSize", UintegerValue(1024));
  ApplicationContainer clientApps2 = client2.Install(nodes.Get(2));
  clientApps2.Start(Seconds(1.0));
  clientApps2.Stop(Seconds(10.0));

  // 启动仿真
  Simulator::Run();
  Simulator::Destroy();

  return 0;
}
