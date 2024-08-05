#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpExample");

void SendNextMessage(Ptr<Socket> socket, uint32_t messageSize, uint32_t numMessages)
{
    static uint32_t messagesSent = 0;
    if (messagesSent < numMessages)
    {
        Ptr<Packet> packet = Create<Packet>(messageSize);
        socket->Send(packet);
        NS_LOG_INFO("Sent message " << messagesSent + 1 << " of size " << messageSize);
        messagesSent++;

        if (messagesSent < numMessages)
        {
            // Schedule the next message after the previous one is fully sent
            socket->SetSendCallback(MakeCallback(&SendNextMessage));
        }
    }
}

int main(int argc, char *argv[])
{
    LogComponentEnable("TcpExample", LOG_LEVEL_INFO);

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

    // Send first message
    Simulator::Schedule(Seconds(2.0), &SendNextMessage, clientSocket, 1024, 3);

    Simulator::Run();
    Simulator::Destroy();

    return 0;
}

/*
在NS3中使用TCP流发送多个消息并在每个消息发送完之后发送下一个消息的确会影响TCP的发送/接收/拥塞窗口及其他状态。以下是一些可能的影响和原因：

发送窗口和拥塞控制：TCP的拥塞控制机制（如慢启动、拥塞避免等）依赖于持续的数据流来调整拥塞窗口（cwnd）。间断的消息发送会导致拥塞窗口不能有效增长，每次发送新消息时可能会重新进入慢启动阶段。

接收窗口：接收方的接收窗口（rwnd）表示接收方愿意接收的字节数。间断的消息发送可能不会充分利用接收窗口，导致接收方的接收窗口利用率低。

流量控制：TCP的流量控制机制通过发送窗口和接收窗口协同工作，以防止发送方发送数据过快，超过接收方的处理能力。间断的消息发送可能导致发送方和接收方的窗口调整不充分。

状态切换：TCP连接有多种状态（如ESTABLISHED、CLOSE_WAIT、FIN_WAIT等）。间断的发送可能导致状态频繁切换，增加协议开销。

为减少这些影响，可以采用以下几种方法：

持续数据流：尽可能发送一个持续的数据流，而不是间断的单独消息。这可以帮助TCP有效地调整拥塞窗口和接收窗口。

应用层控制：在应用层实现数据流的控制，而不是依赖底层TCP。可以通过应用层的协议来分割和管理消息的发送。

调整TCP参数：根据实际需求和网络情况调整TCP的参数（如初始窗口大小、重传间隔等），以优化性能。

*/
