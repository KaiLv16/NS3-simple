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
