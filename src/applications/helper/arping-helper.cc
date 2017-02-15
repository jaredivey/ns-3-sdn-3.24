/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008 INRIA
 *
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
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */

#include "arping-helper.h"
#include "ns3/arping.h"
#include "ns3/names.h"

namespace ns3 {

ArPingHelper::ArPingHelper (Ipv4Address remote)
{
  m_factory.SetTypeId ("ns3::ArPing");
  m_factory.Set ("Remote", Ipv4AddressValue (remote));
}

void 
ArPingHelper::SetAttribute (std::string name, const AttributeValue &value)
{
  m_factory.Set (name, value);
}

ApplicationContainer
ArPingHelper::Install (Ptr<Node> node, Ptr<NetDevice> dev) const
{
  return ApplicationContainer (InstallPriv (node, dev));
}

ApplicationContainer
ArPingHelper::Install (std::string nodeName, Ptr<NetDevice> dev) const
{
  Ptr<Node> node = Names::Find<Node> (nodeName);
  return ApplicationContainer (InstallPriv (node, dev));
}

Ptr<Application>
ArPingHelper::InstallPriv (Ptr<Node> node, Ptr<NetDevice> dev) const
{
  Ptr<ArPing> app = m_factory.Create<ArPing> ();
  app->SetDevice(dev);
  node->AddApplication (app);

  return app;
}

} // namespace ns3
