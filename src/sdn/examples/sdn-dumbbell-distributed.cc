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

/* Network topology
 *
 *  l0---     --- c0---     ---r0
 *       \   /         \   /
 *   .    \ /           \ /     .
 *   .     s0----...----sn      .
 *   .    /               \     .
 *       /                 \
 *  ln---                   ---rn
 */
#include <string>
#include <fstream>
#include <vector>
#include <sys/time.h>
#include <sys/types.h>

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/layer2-p2p-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/mpi-interface.h"

#include "ns3/SdnController.h"
#include "ns3/SdnSwitch.h"
#include "ns3/SdnListener.h"

#include "MsgApps.hh"
#include "FirewallApps.hh"
#include "STPApps.hh"

#ifdef NS3_MPI
#include <mpi.h>
#endif

using namespace ns3;

#define REALLY_BIG_TIME 1000000

typedef struct timeval TIMER_TYPE;
#define TIMER_NOW(_t) gettimeofday (&_t,NULL);
#define TIMER_SECONDS(_t) ((double)(_t).tv_sec + (_t).tv_usec * 1e-6)
#define TIMER_DIFF(_t1, _t2) (TIMER_SECONDS (_t1) - TIMER_SECONDS (_t2))

NS_LOG_COMPONENT_DEFINE ("SdnDumbbellDistributed");

enum APPCHOICE
{
  BULK_SEND,
  PING,
  ON_OFF,
} APPCHOICE;

enum ControllerApplication
{
  MSG_APPS,
  STP_APPS,
} ControllerApplication;

int
main (int argc, char *argv[])
{
#ifdef NS3_MPI
  bool tracing = false;
  uint32_t maxBytes = 51200;
  bool verbose = false;
  std::string firewallText = "src/sdn/examples/FirewallBlacklist.txt";
  uint32_t appChoice = ON_OFF;
  uint32_t controllerApplication = MSG_APPS;

  uint32_t numHosts    = 1;
  uint32_t numSwitches = 2;
  bool nullmsg = false;

  std::ostringstream oss;

  CommandLine cmd;
  cmd.AddValue ("verbose", "Tell application to log if true", verbose);
  cmd.AddValue ("firewallText", "If using the firewall application, point to the location of the firewall contents", firewallText);
  cmd.AddValue ("tracing", "Flag to enable/disable tracing", tracing);
  cmd.AddValue ("maxBytes",
                "Total number of bytes for application to send", maxBytes);
  cmd.AddValue ("appChoice",
                "Application to use: (0) Bulk Send; (1) On Off", appChoice);
  cmd.AddValue ("controllerApplication",
                "Controller application that defined behavior: (0) MsgApps; (1) STPApps", controllerApplication);
//  cmd.AddValue ("numSwitches", "Number of switches", numSwitches);
  cmd.AddValue ("numHosts", "Number of hosts per end switch", numHosts);
  cmd.AddValue ("nullmsg", "Enable the use of null-message synchronization", nullmsg);

  cmd.Parse (argc,argv);

  // Distributed simulation setup; by default use granted time window algorithm.
  if(nullmsg)
    {
      GlobalValue::Bind ("SimulatorImplementationType",
                         StringValue ("ns3::NullMessageSimulatorImpl"));
    }
  else
    {
      GlobalValue::Bind ("SimulatorImplementationType",
                         StringValue ("ns3::DistributedSimulatorImpl"));
    }

  // Enable parallel simulator with the command line arguments
  MpiInterface::Enable (&argc, &argv);

  if (verbose)
    {
      LogComponentEnable ("SdnDumbbellDistributed", LOG_LEVEL_INFO);

      LogComponentEnable ("SdnController", LOG_LEVEL_INFO);
      LogComponentEnable ("SdnSwitch", LOG_LEVEL_INFO);
      LogComponentEnable ("SdnListener", LOG_LEVEL_INFO);
      LogComponentEnable ("SdnConnection", LOG_LEVEL_INFO);
      LogComponentEnable ("SdnFlowTable", LOG_LEVEL_DEBUG);

      LogComponentEnable ("CsmaNetDevice", LOG_LEVEL_INFO);

      LogComponentEnable ("BulkSendApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("V4Ping", LOG_LEVEL_INFO);
      LogComponentEnable ("OnOffApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("PacketSink", LOG_LEVEL_INFO);
    }

  TIMER_TYPE t0, t1, t2;
  TIMER_NOW (t0);

  uint32_t systemId = MpiInterface::GetSystemId ();
  uint32_t systemCount = MpiInterface::GetSize ();

  // Check for valid distributed parameters.
  // Must have 3 and only 3 Logical Processors (LPs)
  if (systemCount != 3)
    {
      std::cout << "This simulation requires 3 and only 3 logical processors." << std::endl;
      return 1;
    }

  NS_LOG_INFO ("Create nodes.");
  NodeContainer controllerNode, switchNodes, leftNodes, rightNodes;
  controllerNode.Create (1, 2);
  leftNodes.Create (numHosts, 0);
  for (uint32_t i = 0; i < numSwitches; ++i)
    {
      Ptr<Node> switchNode = CreateObject<Node> (i);
      switchNodes.Add (switchNode);
    }
  rightNodes.Create (numHosts, 1);

  NS_LOG_INFO ("Create channels.");
  Layer2P2PHelper layer2P2P;
  layer2P2P.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  layer2P2P.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (5)));
  
  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("5ms"));

  std::vector<NetDeviceContainer> leftHostToSwitchContainers;
  std::vector<NetDeviceContainer> rightHostToSwitchContainers;
  std::vector<NetDeviceContainer> switchToSwitchContainers;
  std::vector<NetDeviceContainer> switchToControllerContainers;
  
  // Host to Switch connections
  for (uint32_t j = 0; j < numHosts; ++j)
     {
       leftHostToSwitchContainers.push_back (layer2P2P.Install (
                               NodeContainer(leftNodes.Get (j),
                                             switchNodes.Get (0))));
     }
  
  for (uint32_t j = 0; j < numHosts; ++j)
     {
       rightHostToSwitchContainers.push_back (layer2P2P.Install (
                                NodeContainer(switchNodes.Get (numSwitches-1),
                                              rightNodes.Get (j))));
     }

  // Switch to Switch connections
  for (uint32_t j = 0; j < numSwitches; ++j)
     {
       if (j > 0)
         {
           switchToSwitchContainers.push_back (layer2P2P.Install (
                                 NodeContainer(switchNodes.Get (j-1),
                                               switchNodes.Get (j))));
         }
     }
  
  // Switch to Controller connections
  for (uint32_t j = 0; j < numSwitches; ++j)
     {
       switchToControllerContainers.push_back (pointToPoint.Install (
                                 NodeContainer(switchNodes.Get (j),
                                               controllerNode.Get (0))));

     }
//
// Install the internet stack on the nodes
//
  InternetStackHelper internet;
  internet.Install (leftNodes);
  internet.Install (switchNodes);
  internet.Install (rightNodes);
  internet.Install (controllerNode);

//
// We've got the "hardware" in place.  Now we need to add IP addresses.
//
  NS_LOG_INFO ("Assign IP Addresses.");
  Ipv4AddressHelper ipv4;
  std::vector<Ipv4InterfaceContainer> leftHostToSwitchIPContainers;

  oss.str ("");
  oss << "10.0.0.0";
  ipv4.SetBase (oss.str ().c_str (), "255.255.255.0");

  for (uint32_t i=0; i < leftHostToSwitchContainers.size(); ++i)
    {
      leftHostToSwitchIPContainers.push_back(ipv4.Assign (leftHostToSwitchContainers[i]));

      NS_LOG_INFO ("Host " << i << " address: " << leftHostToSwitchIPContainers[i].GetAddress(0));
    }

  std::vector<Ipv4InterfaceContainer> switchToSwitchIPContainers;
  for (uint32_t i=0; i < switchToSwitchContainers.size(); ++i)
    {
      switchToSwitchIPContainers.push_back(ipv4.Assign (switchToSwitchContainers[i]));
    }

  std::vector<Ipv4InterfaceContainer> rightHostToSwitchIPContainers;
  for (uint32_t i=0; i < rightHostToSwitchContainers.size(); ++i)
    {
      rightHostToSwitchIPContainers.push_back(ipv4.Assign (rightHostToSwitchContainers[i]));

      NS_LOG_INFO ("Host " << i << " address: " << rightHostToSwitchIPContainers[i].GetAddress(1));
    }

  std::vector<Ipv4InterfaceContainer> switchToControllerIPContainers;
  for (uint32_t i=0; i < switchToControllerContainers.size(); ++i)
    {
      oss.str ("");
      oss << "192.168." << i+1 << ".0";
      ipv4.SetBase (oss.str ().c_str (), "255.255.255.0");
      switchToControllerIPContainers.push_back(ipv4.Assign (switchToControllerContainers[i]));
    }

//
// Set up default next hops for host nodes.
//
  NS_LOG_INFO ("Set up fakeways.");
  for (uint32_t i = 0; i < numHosts; ++i)
    {
      Ptr<Ipv4> ipv4li = leftNodes.Get(i)->GetObject<Ipv4> ();
      Ptr<Ipv4> ipv4ri = rightNodes.Get(i)->GetObject<Ipv4> ();

      Ipv4StaticRoutingHelper ipv4RoutingHelper;
      Ptr<Ipv4StaticRouting> staticRoutingli = ipv4RoutingHelper.GetStaticRouting (ipv4li);
      Ptr<Ipv4StaticRouting> staticRoutingri = ipv4RoutingHelper.GetStaticRouting (ipv4ri);

      NS_LOG_INFO ("Adding default route from h" << i << " to h" << numHosts+i << ": " 
                     << leftHostToSwitchIPContainers[i].GetAddress(1));
      staticRoutingli->SetDefaultRoute (leftHostToSwitchIPContainers[i].GetAddress(1), 1);

      NS_LOG_INFO ("Adding default route from h" << numHosts+i << " to h" << i << ": " 
                     << rightHostToSwitchIPContainers[i].GetAddress(0));
      staticRoutingli->SetDefaultRoute (rightHostToSwitchIPContainers[i].GetAddress(0), 1);
    }
  //Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  NS_LOG_INFO ("Create Applications.");

  ApplicationContainer sourceApps;
  uint16_t port = 0;

  Ptr<UniformRandomVariable> randTime = CreateObject<UniformRandomVariable>();
  randTime->SetAttribute ("Min", DoubleValue(numSwitches));
  randTime->SetAttribute ("Max", DoubleValue(numSwitches+numHosts));

  for (uint32_t i = 0; i < numHosts; ++i)
    {
      if (appChoice == BULK_SEND)
	{
	  ApplicationContainer sourceApp;
	  port = 9;  // well-known echo port number

	  NS_LOG_INFO ("Bulk Send app to address: " << rightHostToSwitchIPContainers[i].GetAddress(1));
	  BulkSendHelper source ("ns3::TcpSocketFactory",
				 InetSocketAddress (rightHostToSwitchIPContainers[i].GetAddress(1), port));

	  // Set the amount of data to send in bytes.  Zero is unlimited.
	  source.SetAttribute ("MaxBytes", UintegerValue (maxBytes));

	  if (systemId == 0)
	    {
	      sourceApp = source.Install (leftNodes.Get (i));
	      sourceApp.Start (Seconds (randTime->GetValue()));
	      sourceApps.Add(sourceApp);
	    }
	}
      else if (appChoice == PING)
	{
	  for (uint32_t j = 0; j < numHosts; ++j)
	    {
	      NS_LOG_INFO ("PING app to address: " << rightHostToSwitchIPContainers[j].GetAddress(1));
	      V4PingHelper source1 (rightHostToSwitchIPContainers[j].GetAddress(1));
	      source1.SetAttribute ("Verbose", BooleanValue (true));
	      source1.SetAttribute ("PingAll", BooleanValue (true));
	      source1.SetAttribute ("Count", UintegerValue (1));

	      NS_LOG_INFO ("PING app to address: " << leftHostToSwitchIPContainers[j].GetAddress(0));
	      V4PingHelper source2 (leftHostToSwitchIPContainers[j].GetAddress(0));
	      source2.SetAttribute ("Verbose", BooleanValue (true));
	      source2.SetAttribute ("PingAll", BooleanValue (true));
	      source2.SetAttribute ("Count", UintegerValue (1));

	      ApplicationContainer sourceAppL2R;
	      if (systemId == 0)
	        {
		  sourceAppL2R = source1.Install (leftNodes.Get (i));
		  if ((i == 0) && (j == 0))
		    {
		      sourceAppL2R.Start (Seconds (numSwitches*2));
		    }
		  else
		    {
		      sourceAppL2R.Start (Seconds (REALLY_BIG_TIME));
		    }
		  sourceApps.Add(sourceAppL2R);
	        }

	      ApplicationContainer sourceAppL2L;
	      if (i != j)
		{
		  if (systemId == 0)
		    {
		      sourceAppL2L = source2.Install (leftNodes.Get (i));
		      sourceAppL2L.Start (Seconds (REALLY_BIG_TIME));
		      sourceApps.Add(sourceAppL2L);
		    }
		}

	      ApplicationContainer sourceAppR2L;
	      if (systemId == 1)
	        {
		  sourceAppR2L = source2.Install (rightNodes.Get (i));
		  sourceAppR2L.Start (Seconds (REALLY_BIG_TIME));
		  sourceApps.Add(sourceAppR2L);
	        }

	      ApplicationContainer sourceAppR2R;
	      if (i != j)
		{
		  if (systemId == 1)
		    {
		      sourceAppR2R = source1.Install (rightNodes.Get (i));
		      sourceAppR2R.Start (Seconds (REALLY_BIG_TIME));
		      sourceApps.Add(sourceAppR2R);
		    }
		}
	    }
	}
      else if (appChoice == ON_OFF)
	{
	  ApplicationContainer sourceApp;
	  port = 50000;

	  OnOffHelper source ("ns3::TcpSocketFactory",
			      InetSocketAddress (rightHostToSwitchIPContainers[i].GetAddress(1), port));
	  source.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
	  source.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
	  source.SetAttribute ("MaxBytes", UintegerValue (51200));

	  if (systemId == 0)
	    {
	      sourceApp = source.Install (leftNodes.Get (i));
	      sourceApp.Start (Seconds (randTime->GetValue()));
	      sourceApps.Add(sourceApp);
	    }
	}
    }

//
// Create a PacketSinkApplication and install it on right nodes
//
  ApplicationContainer sinkApps;
  if (systemId == 1)
    {
      PacketSinkHelper sink ("ns3::TcpSocketFactory",
                             InetSocketAddress (Ipv4Address::GetAny (), port));
      for (uint32_t i = 0; i < numHosts; ++i)
        {
          sinkApps.Add(sink.Install (rightNodes.Get (i)));
        }
      sinkApps.Start (Seconds (0.0));
    }

//
// Install Switch.
//
for (uint32_t j = 0; j < numSwitches; ++j)
  {
    Ptr<SdnSwitch> sdnS = CreateObject<SdnSwitch> ();
    sdnS->SetStartTime (Seconds (0.0));
    if (systemId == j)
      switchNodes.Get (j)->AddApplication (sdnS);
  }

//
// Install Controller.
//
  Ptr<SdnListener> sdnListener;
  if(controllerApplication == STP_APPS)
    {
      sdnListener = CreateObject<STPApps> ();
    }
  if(controllerApplication == MSG_APPS)
    {
      sdnListener = CreateObject<MultiLearningSwitch> ();
    }
  //Ptr<MultiLearningSwitch> sdnListener = CreateObject<MultiLearningSwitch> ();
  //Ptr<Firewall> sdnListener = CreateObject<Firewall> (firewallText);
  Ptr<SdnController> sdnC0 = CreateObject<SdnController> (sdnListener);
  sdnC0->SetStartTime (Seconds (0.0));
  if (systemId == 2)
      controllerNode.Get(0)->AddApplication (sdnC0);

//
// Now, do the actual simulation.
//
  NS_LOG_INFO ("Run Simulation.");
  TIMER_NOW (t1);

//
// Add stop for STP; otherwise, controller events will be continuously scheduled.
//
  if(controllerApplication == STP_APPS) Simulator::Stop (Seconds(500));
  Simulator::Run ();
  TIMER_NOW (t2);

  uint32_t totalRx = 0;
  for (uint32_t i = 0; i < sinkApps.GetN(); ++i)
    {
      Ptr<PacketSink> sink1 = DynamicCast<PacketSink> (sinkApps.Get (i));
      if (sink1)
        {
          totalRx += sink1->GetTotalRx();
        }
    }

  double simTime = Simulator::Now().GetSeconds();
  double d1 = TIMER_DIFF (t1, t0) + TIMER_DIFF (t2, t1);

  if (systemId == 1)
    {
      std::cout << numSwitches << "\t" << numHosts << "\t" << simTime << "\t" << d1;
      std::cout << "\t" << totalRx;
      //std::cout << "\t" << (V4Ping::m_totalSend - V4Ping::m_totalRecv);
      //std::cout << "\t" << V4Ping::m_totalSend << "\t" << V4Ping::m_totalRecv << std::endl;
    }

  Simulator::Destroy ();

  // Exit the MPI execution environment
  MpiInterface::Disable ();
  return 0;
#else
  NS_FATAL_ERROR ("Can't use distributed simulator without MPI compiled in");
#endif
}
