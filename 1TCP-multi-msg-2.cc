
/*
在真实的socket编程中，data从user复制到kernel之后，send()函数就会返回，这保证了可以有连续的发送数据供给。NS3中是否实现了这一点？

ChatGPT
在NS3中，send() 函数的行为与真实的 socket 编程中类似。具体来说，当应用程序调用 send() 函数时，数据从用户空间复制到内核空间，并立即返回。这保证了应用程序可以连续地提供数据进行发送。然而，需要注意的是，在NS3模拟环境中，还有一些额外的考虑因素，例如事件调度和模拟时间推进。

在NS3中，Send() 方法实际上是非阻塞的，数据会被复制到模拟器的发送缓冲区，并且 Send() 方法会立即返回。这意味着应用程序可以连续调用 Send() 方法，而不会等待数据实际被传输到网络。NS3的模拟器会处理数据在模拟时间内的传输。

以下是一个例子，展示如何在NS3中实现连续的数据发送：
*/


#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpContinuousSendExample");

void SendContinuousData(Ptr<Socket> socket, uint32_t dataSize, uint32_t numPackets, Time interPacketInterval)
{
    static uint32_t packetsSent = 0;
    if (packetsSent < numPackets)
    {
        Ptr<Packet> packet = Create<Packet>(dataSize);
        socket->Send(packet);
        NS_LOG_INFO("Sent packet " << packetsSent + 1 << " of size " << dataSize);
        packetsSent++;

        if (packetsSent < numPackets)
        {
            Simulator::Schedule(interPacketInterval, &SendContinuousData, socket, dataSize, numPackets, interPacketInterval);
        }
    }
}

int main(int argc, char *argv[])
{
    LogComponentEnable("TcpContinuousSendExample", LOG_LEVEL_INFO);

    NodeContainer nodes;
    nodes.Create(2);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer devices;
    devices = pointToPoint.Install(nodes);

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");

    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    uint16_t port = 9;
    Address serverAddress = InetSocketAddress(interfaces.GetAddress(1), port);

    // Install the server application
    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", serverAddress);
    ApplicationContainer sinkApps = packetSinkHelper.Install(nodes.Get(1));
    sinkApps.Start(Seconds(1.0));
    sinkApps.Stop(Seconds(10.0));

    // Install the client application
    Ptr<Socket> clientSocket = Socket::CreateSocket(nodes.Get(0), TcpSocketFactory::GetTypeId());
    clientSocket->Connect(serverAddress);

    // Schedule continuous data sending
    uint32_t dataSize = 1024;           // Size of each packet
    uint32_t numPackets = 10;           // Number of packets to send
    Time interPacketInterval = Seconds(0.1);  // Interval between packets

    Simulator::Schedule(Seconds(2.0), &SendContinuousData, clientSocket, dataSize, numPackets, interPacketInterval);

    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
/*
在这个例子中：

节点创建和安装：创建两个节点，并在它们之间建立一个点对点链接。
安装协议栈和分配地址：在节点上安装Internet栈，并分配IP地址。
安装应用程序：在接收方节点上安装一个TCP服务器应用程序，在发送方节点上创建一个TCP套接字并连接到服务器。
连续发送数据：通过 SendContinuousData 函数发送连续的数据包。这个函数在发送完一个数据包后调用自身以发送下一个数据包，直到发送完所有数据包。
在这种方式下，尽管每个数据包之间有一个短暂的间隔（由 interPacketInterval 控制），但数据包的发送是连续进行的，类似于实际的socket编程中连续发送数据的行为。
*/
