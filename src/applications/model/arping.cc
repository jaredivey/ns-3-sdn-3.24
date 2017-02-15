/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "arping.h"
#include "ns3/arp-header.h"
#include "ns3/mac48-address.h"
#include "ns3/assert.h"
#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/csma-net-device.h"
#include "ns3/socket.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/inet-socket-address.h"
#include "ns3/packet.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/arp-l3-protocol.h"
#include "ns3/arp-cache.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("ArPing");

NS_OBJECT_ENSURE_REGISTERED (ArPing);

TypeId 
ArPing::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ArPing")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<ArPing> ()
    .AddAttribute ("Remote", 
                   "The address of the machine we want to ping.",
                   Ipv4AddressValue (),
                   MakeIpv4AddressAccessor (&ArPing::m_remote),
                   MakeIpv4AddressChecker ())
	.AddAttribute ("DestHwAddress",
				   "The network interface to send from.",
				   AddressValue (),
				   MakeAddressAccessor (&ArPing::m_dstAddr),
				   MakeAddressChecker ())
    .AddAttribute ("SourceHwAddress",
                   "The network interface to send from.",
                   AddressValue (),
                   MakeAddressAccessor (&ArPing::m_srcAddr),
                   MakeAddressChecker ())
    .AddAttribute ("SourceIpAddress",
                   "Our IPv4 address.",
                   Ipv4AddressValue (),
                   MakeIpv4AddressAccessor (&ArPing::m_srcIpAddr),
                   MakeIpv4AddressChecker ())
    .AddAttribute ("Verbose",
                   "Produce usual output.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&ArPing::m_verbose),
                   MakeBooleanChecker ())
    .AddAttribute ("Interval", "Wait  interval  seconds between sending each packet.",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&ArPing::m_interval),
                   MakeTimeChecker ())
	.AddAttribute ("Count", "The number of pings to send.",
				   UintegerValue (0),
				   MakeUintegerAccessor (&ArPing::m_count),
				   MakeUintegerChecker<uint32_t> (0))
    .AddAttribute ("Size", "The number of data bytes to be sent, real packet will be 8 (ICMP) + 20 (IP) bytes longer.",
                   UintegerValue (56),
                   MakeUintegerAccessor (&ArPing::m_size),
                   MakeUintegerChecker<uint32_t> (28))
    .AddTraceSource ("Rtt",
                     "The rtt calculated by the ping.",
                     MakeTraceSourceAccessor (&ArPing::m_traceRtt),
                     "ns3::Time::TracedCallback");
  ;
  return tid;
}

ArPing::ArPing ()
  : m_interval (Seconds (1)),
    m_size (56),
    m_count (0),
    m_socket (0),
    m_seq (0),
    m_verbose (false),
    m_recv (0)
{
  NS_LOG_FUNCTION (this);
}
ArPing::~ArPing ()
{
  NS_LOG_FUNCTION (this);
}

void
ArPing::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;
  Application::DoDispose ();
}

uint32_t
ArPing::GetApplicationId (void) const
{
  NS_LOG_FUNCTION (this);
  Ptr<Node> node = GetNode ();
  for (uint32_t i = 0; i < node->GetNApplications (); ++i)
    {
      if (node->GetApplication (i) == this)
        {
          return i;
        }
    }
  NS_ASSERT_MSG (false, "forgot to add application to node");
  return 0; // quiet compiler
}

bool
ArPing::Receive (Ptr<NetDevice> device, Ptr<const Packet> packet,
                         uint16_t protocol, const Address &from,
                         const Address &to, NetDevice::PacketType packetType)
{
  NS_LOG_FUNCTION (this << device << packet << protocol << from << to << packetType);

  NS_LOG_DEBUG ("recv " << packet->GetSize () << " bytes");
  if (protocol == 0x0806)
  {
	  ArpHeader arp;
	  packet->PeekHeader (arp);
	  uint32_t recvSize = packet->GetSize ();
	  if (arp.IsReply ())
	  {
		  if (arp.GetDestinationIpv4Address() == m_srcIpAddr &&
				  arp.GetSourceIpv4Address() == m_remote )
		  {
			  Ptr<ArpL3Protocol> arp_prot = GetNode()->GetObject<ArpL3Protocol> ();
			  Ptr<ArpCache> cache = arp_prot->FindCache (device);
	          ArpCache::Entry *entry = cache->Lookup (arp.GetSourceIpv4Address());
	          if (entry != 0)
	          {
	        	  if (entry->IsWaitReply())
	        	  {
					  entry->MarkAlive (arp.GetSourceHardwareAddress());
					  //entry->MarkPermanent();
					  Ptr<Packet> pendingPacket = entry->DequeuePending();

					  uint32_t * buf = new uint32_t [m_size];
					  uint32_t nodeId;
					  uint32_t appId;
					  uint32_t seqId;

					  pendingPacket->CopyData ((uint8_t *)buf, pendingPacket->GetSize());
					  Read32 ((const uint8_t *) &buf[0], nodeId);
					  Read32 ((const uint8_t *) &buf[1], appId);
					  Read32 ((const uint8_t *) &buf[2], seqId);
					  std::map<uint32_t, Time>::iterator i = m_sent.find(seqId);
					  if (i != m_sent.end())
					  {
						  Time sendTime = i->second;
						  NS_ASSERT (Simulator::Now () >= sendTime);
						  Time delta = Simulator::Now () - sendTime;

						  m_sent.erase (i);
						  m_avgRtt.Update (delta.GetMilliSeconds ());
						  m_recv++;
						  m_traceRtt (delta);

						  if (m_verbose)
						  {
							  std::cout << recvSize << " bytes from " << arp.GetSourceIpv4Address() << " ["
									  << arp.GetSourceHardwareAddress() << "] "
									  << delta.GetMilliSeconds () << " ms\n";
						  }

						  if (m_count && m_recv >= m_count)
						  {
							  Simulator::ScheduleNow (&ArPing::StopApplication, this);
						  }
					  }
	        	  }
	          }
		  }
	  }
  }
  return true;
}

// Writes data to buffer in little-endian format; least significant byte
// of data is at lowest buffer address
void
ArPing::Write32 (uint8_t *buffer, const uint32_t data)
{
  NS_LOG_FUNCTION (this << buffer << data);
  buffer[0] = (data >> 0) & 0xff;
  buffer[1] = (data >> 8) & 0xff;
  buffer[2] = (data >> 16) & 0xff;
  buffer[3] = (data >> 24) & 0xff;
}

// Writes data from a little-endian formatted buffer to data
void
ArPing::Read32 (const uint8_t *buffer, uint32_t &data)
{
  NS_LOG_FUNCTION (this << buffer << data);
  data = (buffer[3] << 24) + (buffer[2] << 16) + (buffer[1] << 8) + buffer[0];
}

void 
ArPing::Send ()
{
  NS_LOG_FUNCTION (this);

  NS_LOG_INFO ("m_seq=" << m_seq);
  //
  // We must write quantities out in some form of network order.  Since there
  // isn't an htonl to work with we just follow the convention in pcap traces
  // (where any difference would show up anyway) and borrow that code.  Don't
  // be too surprised when you see that this is a little endian convention.
  //
  uint8_t* data = new uint8_t[m_size];
  for (uint32_t i = 0; i < m_size; ++i) data[i] = 0;
  NS_ASSERT (m_size >= 28);

  uint32_t tmp = GetNode ()->GetId ();
  Write32 (&data[0 * sizeof(uint32_t)], tmp);

  tmp = GetApplicationId ();
  Write32 (&data[1 * sizeof(uint32_t)], tmp);

  tmp = m_seq;
  Write32 (&data[2 * sizeof(uint32_t)], tmp);

  m_seq++;


  Ptr<Packet> dataPacket = Create<Packet> ((uint8_t *) data, m_size);
  m_sent.insert (std::make_pair (m_seq - 1, Simulator::Now ()));

  Ptr<ArpL3Protocol> arp_prot = GetNode()->GetObject<ArpL3Protocol> ();
  Ptr<ArpCache> cache = arp_prot->FindCache (m_netdevice);

  ArpCache::Entry *entry = cache->Lookup(m_remote);
  if (entry == 0)
  {
	  entry = cache->Add (m_remote);
  }
  entry->MarkWaitReply (dataPacket);

  arp_prot->SendArpRequest(cache, m_remote);

  m_next = Simulator::Schedule (m_interval, &ArPing::Send, this);
  delete[] data;
}

void 
ArPing::StartApplication (void)
{
  NS_LOG_FUNCTION (this);

  m_started = Simulator::Now ();
  if (m_verbose)
    {
      std::cout << "ARPING  " << m_remote << " from " << m_srcIpAddr << " " << m_srcAddr << "\n";
    }

  Ptr<CsmaNetDevice> csmaDevice = DynamicCast<CsmaNetDevice> (m_netdevice);
  if (csmaDevice)
  {
	  csmaDevice->SetPromiscReceiveCallback(MakeCallback(&ArPing::Receive, this));
  }
  else
  {
	  NS_LOG_ERROR("Device must be CSMA");
  }

  Send ();
}
void 
ArPing::StopApplication (void)
{
  NS_LOG_FUNCTION (this);
  m_next.Cancel ();

  Ptr<CsmaNetDevice> csmaDevice = DynamicCast<CsmaNetDevice> (m_netdevice);
  if (csmaDevice)
  {
	  csmaDevice->SetPromiscReceiveCallback(MakeNullCallback());
  }

  if (m_verbose)
    {
      std::ostringstream os;
      os.precision (4);
      os << "--- " << m_remote << " statistics ---\n"
         << m_seq << " packets transmitted, " << m_recv << " received, "
         << ((m_seq - m_recv) * 100 / m_seq) << "% packet loss, "
         << "time " << (Simulator::Now () - m_started).GetMilliSeconds () << "ms\n";
      std::cout << os.str ();
    }
}


} // namespace ns3
