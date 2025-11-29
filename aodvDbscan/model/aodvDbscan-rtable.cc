/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 IITP RAS
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
 * Based on
 *      NS-2 aodvDbscan model developed by the CMU/MONARCH group and optimized and
 *      tuned by Samir Das and Mahesh Marina, University of Cincinnati;
 *
 *      aodvDbscan-UU implementation by Erik Nordström of Uppsala University
 *      http://core.it.uu.se/core/index.php/aodvDbscan-UU
 *
 * Authors: Elena Buchatskaia <borovkovaes@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */

#include "aodvDbscan-rtable.h"
#include <iomanip>
#include "ns3/simulator.h"
#include "ns3/log.h"
#include <cmath>           // untuk std::sqrt
#include <algorithm>       // untuk std::min, std::max
#include <unordered_set>   // untuk std::unordered_set
#include <queue>           // untuk std::queue

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("aodvDbscanRoutingTable");

namespace aodvDbscan {

/*
 The Routing Table
 */

RoutingTableEntry::RoutingTableEntry (Ptr<NetDevice> dev, Ipv4Address dst, bool vSeqNo, uint32_t seqNo,
                                      Ipv4InterfaceAddress iface, uint16_t hops, Ipv4Address nextHop, Time lifetime,
                                      uint32_t txError, uint32_t positionX, uint32_t positionY, uint32_t freeSpace)
  : m_ackTimer (Timer::CANCEL_ON_DESTROY),
    m_validSeqNo (vSeqNo),
    m_seqNo (seqNo),
    m_hops (hops),
    m_lifeTime (lifetime + Simulator::Now ()),
    m_iface (iface),
    m_flag (VALID),
    m_reqCount (0),
    m_blackListState (false),
    m_blackListTimeout (Simulator::Now ()),
    m_txerrorCount(txError),
    m_positionX(positionX),
    m_positionY(positionY),
    m_freeSpace(freeSpace)
{
  m_ipv4Route = Create<Ipv4Route> ();
  m_ipv4Route->SetDestination (dst);
  m_ipv4Route->SetGateway (nextHop);
  m_ipv4Route->SetSource (m_iface.GetLocal ());
  m_ipv4Route->SetOutputDevice (dev);
}

RoutingTableEntry::~RoutingTableEntry ()
{
}

bool
RoutingTableEntry::InsertPrecursor (Ipv4Address id)
{
  NS_LOG_FUNCTION (this << id);
  if (!LookupPrecursor (id))
    {
      m_precursorList.push_back (id);
      return true;
    }
  else
    {
      return false;
    }
}

bool
RoutingTableEntry::LookupPrecursor (Ipv4Address id)
{
  NS_LOG_FUNCTION (this << id);
  for (std::vector<Ipv4Address>::const_iterator i = m_precursorList.begin (); i
       != m_precursorList.end (); ++i)
    {
      if (*i == id)
        {
          NS_LOG_LOGIC ("Precursor " << id << " found");
          return true;
        }
    }
  NS_LOG_LOGIC ("Precursor " << id << " not found");
  return false;
}

bool
RoutingTableEntry::DeletePrecursor (Ipv4Address id)
{
  NS_LOG_FUNCTION (this << id);
  std::vector<Ipv4Address>::iterator i = std::remove (m_precursorList.begin (),
                                                      m_precursorList.end (), id);
  if (i == m_precursorList.end ())
    {
      NS_LOG_LOGIC ("Precursor " << id << " not found");
      return false;
    }
  else
    {
      NS_LOG_LOGIC ("Precursor " << id << " found");
      m_precursorList.erase (i, m_precursorList.end ());
    }
  return true;
}

void
RoutingTableEntry::DeleteAllPrecursors ()
{
  NS_LOG_FUNCTION (this);
  m_precursorList.clear ();
}

bool
RoutingTableEntry::IsPrecursorListEmpty () const
{
  return m_precursorList.empty ();
}

void
RoutingTableEntry::GetPrecursors (std::vector<Ipv4Address> & prec) const
{
  NS_LOG_FUNCTION (this);
  if (IsPrecursorListEmpty ())
    {
      return;
    }
  for (std::vector<Ipv4Address>::const_iterator i = m_precursorList.begin (); i
       != m_precursorList.end (); ++i)
    {
      bool result = true;
      for (std::vector<Ipv4Address>::const_iterator j = prec.begin (); j
           != prec.end (); ++j)
        {
          if (*j == *i)
            {
              result = false;
            }
        }
      if (result)
        {
          prec.push_back (*i);
        }
    }
}

void
RoutingTableEntry::Invalidate (Time badLinkLifetime)
{
  NS_LOG_FUNCTION (this << badLinkLifetime.As (Time::S));
  if (m_flag == INVALID)
    {
      return;
    }
  m_flag = INVALID;
  m_reqCount = 0;
  m_lifeTime = badLinkLifetime + Simulator::Now ();
}

void
RoutingTableEntry::Print (Ptr<OutputStreamWrapper> stream, Time::Unit unit /* = Time::S */) const
{
  std::ostream* os = stream->GetStream ();
  // Copy the current ostream state
  std::ios oldState (nullptr);
  oldState.copyfmt (*os);

  *os << std::resetiosflags (std::ios::adjustfield) << std::setiosflags (std::ios::left);

  std::ostringstream dest, gw, iface, expire;
  dest << m_ipv4Route->GetDestination ();
  gw << m_ipv4Route->GetGateway ();
  iface << m_iface.GetLocal ();
  expire << std::setprecision (2) << (m_lifeTime - Simulator::Now ()).As (unit);
  *os << std::setw (16) << dest.str();
  *os << std::setw (16) << gw.str();
  *os << std::setw (16) << iface.str();
  *os << std::setw (16);
  switch (m_flag)
    {
    case VALID:
      {
        *os << "UP";
        break;
      }
    case INVALID:
      {
        *os << "DOWN";
        break;
      }
    case IN_SEARCH:
      {
        *os << "IN_SEARCH";
        break;
      }
    }

  *os << std::setw (16) << expire.str();
  *os << m_hops << std::endl;
  // Restore the previous ostream state
  (*os).copyfmt (oldState);
}

/*
 The Routing Table
 */

RoutingTable::RoutingTable (Time t)
  : m_badLinkLifetime (t)
{
}

bool
RoutingTable::LookupRoute (Ipv4Address id, RoutingTableEntry & rt)
{
  NS_LOG_FUNCTION (this << id);
  Purge ();
  if (m_ipv4AddressEntry.empty ())
    {
      NS_LOG_LOGIC ("Route to " << id << " not found; m_ipv4AddressEntry is empty");
      return false;
    }
  std::map<Ipv4Address, RoutingTableEntry>::const_iterator i =
    m_ipv4AddressEntry.find (id);
  if (i == m_ipv4AddressEntry.end ())
    {
      NS_LOG_LOGIC ("Route to " << id << " not found");
      return false;
    }
  rt = i->second;
  NS_LOG_LOGIC ("Route to " << id << " found");
  return true;
}

bool
RoutingTable::LookupValidRoute (Ipv4Address id, RoutingTableEntry & rt)
{
  NS_LOG_FUNCTION (this << id);
  if (!LookupRoute (id, rt))
    {
      NS_LOG_LOGIC ("Route to " << id << " not found");
      return false;
    }
  NS_LOG_LOGIC ("Route to " << id << " flag is " << ((rt.GetFlag () == VALID) ? "valid" : "not valid"));
  return (rt.GetFlag () == VALID);
}

bool
RoutingTable::DeleteRoute (Ipv4Address dst)
{
  NS_LOG_FUNCTION (this << dst);
  Purge ();
  if (m_ipv4AddressEntry.erase (dst) != 0)
    {
      NS_LOG_LOGIC ("Route deletion to " << dst << " successful");
      return true;
    }
  NS_LOG_LOGIC ("Route deletion to " << dst << " not successful");
  return false;
}

bool
RoutingTable::AddRoute (RoutingTableEntry & rt)
{
  NS_LOG_FUNCTION (this);
  Purge ();
  if (rt.GetFlag () != IN_SEARCH)
    {
      rt.SetRreqCnt (0);
    }
  std::pair<std::map<Ipv4Address, RoutingTableEntry>::iterator, bool> result =
    m_ipv4AddressEntry.insert (std::make_pair (rt.GetDestination (), rt));
  return result.second;
}

bool
RoutingTable::Update (RoutingTableEntry & rt)
{
  NS_LOG_FUNCTION (this);
  std::map<Ipv4Address, RoutingTableEntry>::iterator i =
    m_ipv4AddressEntry.find (rt.GetDestination ());
  if (i == m_ipv4AddressEntry.end ())
    {
      NS_LOG_LOGIC ("Route update to " << rt.GetDestination () << " fails; not found");
      return false;
    }
  i->second = rt;
  if (i->second.GetFlag () != IN_SEARCH)
    {
      NS_LOG_LOGIC ("Route update to " << rt.GetDestination () << " set RreqCnt to 0");
      i->second.SetRreqCnt (0);
    }
  return true;
}

bool
RoutingTable::SetEntryState (Ipv4Address id, RouteFlags state)
{
  NS_LOG_FUNCTION (this);
  std::map<Ipv4Address, RoutingTableEntry>::iterator i =
    m_ipv4AddressEntry.find (id);
  if (i == m_ipv4AddressEntry.end ())
    {
      NS_LOG_LOGIC ("Route set entry state to " << id << " fails; not found");
      return false;
    }
  i->second.SetFlag (state);
  i->second.SetRreqCnt (0);
  NS_LOG_LOGIC ("Route set entry state to " << id << ": new state is " << state);
  return true;
}

void
RoutingTable::GetListOfDestinationWithNextHop (Ipv4Address nextHop, std::map<Ipv4Address, uint32_t> & unreachable )
{
  NS_LOG_FUNCTION (this);
  Purge ();
  unreachable.clear ();
  for (std::map<Ipv4Address, RoutingTableEntry>::const_iterator i =
         m_ipv4AddressEntry.begin (); i != m_ipv4AddressEntry.end (); ++i)
    {
      if (i->second.GetNextHop () == nextHop)
        {
          NS_LOG_LOGIC ("Unreachable insert " << i->first << " " << i->second.GetSeqNo ());
          unreachable.insert (std::make_pair (i->first, i->second.GetSeqNo ()));
        }
    }
}

void
RoutingTable::InvalidateRoutesWithDst (const std::map<Ipv4Address, uint32_t> & unreachable)
{
  NS_LOG_FUNCTION (this);
  Purge ();
  for (std::map<Ipv4Address, RoutingTableEntry>::iterator i =
         m_ipv4AddressEntry.begin (); i != m_ipv4AddressEntry.end (); ++i)
    {
      for (std::map<Ipv4Address, uint32_t>::const_iterator j =
             unreachable.begin (); j != unreachable.end (); ++j)
        {
          if ((i->first == j->first) && (i->second.GetFlag () == VALID))
            {
              NS_LOG_LOGIC ("Invalidate route with destination address " << i->first);
              i->second.Invalidate (m_badLinkLifetime);
            }
        }
    }
}

void
RoutingTable::DeleteAllRoutesFromInterface (Ipv4InterfaceAddress iface)
{
  NS_LOG_FUNCTION (this);
  if (m_ipv4AddressEntry.empty ())
    {
      return;
    }
  for (std::map<Ipv4Address, RoutingTableEntry>::iterator i =
         m_ipv4AddressEntry.begin (); i != m_ipv4AddressEntry.end (); )
    {
      if (i->second.GetInterface () == iface)
        {
          std::map<Ipv4Address, RoutingTableEntry>::iterator tmp = i;
          ++i;
          m_ipv4AddressEntry.erase (tmp);
        }
      else
        {
          ++i;
        }
    }
}
std::vector<Ipv4Address> 
RoutingTable::DBSCAN (Ipv4Address dst, uint32_t positionX, uint32_t positionY, 
                      double epsilon, int minPts)
{
    Purge();

    // --- Step 1: Build feature vector -----------------------------------------

    struct FeaturePoint {
        Ipv4Address ip;
        double f[3];
    };

    std::vector<FeaturePoint> points;
    points.reserve(m_ipv4AddressEntry.size());

    for (auto &it : m_ipv4AddressEntry)
    {
        const Ipv4Address& ip = it.first;
        const RoutingTableEntry& entry = it.second;

        if (ip.IsBroadcast() || ip.IsLocalhost() || ip.IsMulticast() ||
            ip.IsSubnetDirectedBroadcast(Ipv4Mask("255.255.255.0")) ||
            entry.GetFlag() == INVALID || entry.GetHop() > 2)
        {
            continue;
        }

        FeaturePoint p;
        p.ip = ip;

        double dx = (double)positionX - entry.GetPositionX();
        double dy = (double)positionY - entry.GetPositionY();
        p.f[0] = std::sqrt(dx*dx + dy*dy);
        p.f[1] = (double)entry.GetTxErrorCount();
        p.f[2] = (double)entry.GetFreeSpace();

        points.push_back(p);
    }

    int n = points.size();
    if (n == 0) return {};

    // --- Step 2: Normalize features -------------------------------------------

    double minv[3], maxv[3];

    for (int d = 0; d < 3; d++)
    {
        minv[d] = maxv[d] = points[0].f[d];

        for (int i = 1; i < n; i++)
        {
            minv[d] = std::min(minv[d], points[i].f[d]);
            maxv[d] = std::max(maxv[d], points[i].f[d]);
        }

        double range = maxv[d] - minv[d];

        for (int i = 0; i < n; i++)
        {
            points[i].f[d] = (range != 0) ? 
                (points[i].f[d] - minv[d]) / range : 0.0;
        }
    }

    // --- Step 3: DBSCAN -------------------------------------------------------

    const int UNVISITED = -1;
    const int NOISE     = -2;

    std::vector<int> label(n, UNVISITED);
    int clusterId = 0;

    auto dist = [&](int a, int b) {
        double s = 0.0;
        for (int d = 0; d < 3; d++) {
            double diff = points[a].f[d] - points[b].f[d];
            s += diff * diff;
        }
        return std::sqrt(s);
    };

    auto regionQuery = [&](int idx) {
        std::vector<int> neighbors;
        neighbors.reserve(n);

        for (int i = 0; i < n; i++)
        {
            if (i != idx && dist(idx, i) <= epsilon)
                neighbors.push_back(i);
        }
        return neighbors;
    };

    for (int i = 0; i < n; i++)
    {
        if (label[i] != UNVISITED)
            continue;

        auto neighbors = regionQuery(i);

        if ((int)neighbors.size() < minPts)
        {
            label[i] = NOISE;
            continue;
        }

        // expand cluster
        std::unordered_set<int> visited;
        std::queue<int> q;

        for (int nb : neighbors) q.push(nb);
        q.push(i);

        while (!q.empty())
        {
            int p = q.front();
            q.pop();

            if (visited.count(p)) continue;
            visited.insert(p);

            if (label[p] == UNVISITED || label[p] == NOISE)
                label[p] = clusterId;

            auto nb2 = regionQuery(p);
            if ((int)nb2.size() >= minPts)
            {
                for (int x : nb2)
                    if (!visited.count(x))
                        q.push(x);
            }
        }

        clusterId++;
    }

    // --- Step 4: Cluster Scoring ----------------------------------------------

    double ideal[3] = {0.0, 0.0, 1.0};

    std::map<int, std::vector<int>> clusterMembers;
    for (int i = 0; i < n; i++)
        if (label[i] >= 0)
            clusterMembers[label[i]].push_back(i);

    int bestCluster = -1;
    double bestScore = 1e18;

    for (auto &c : clusterMembers)
    {
        auto &members = c.second;

        double centroid[3] = {0,0,0};
        for (int idx : members)
            for (int d=0; d<3; d++)
                centroid[d] += points[idx].f[d];

        for (int d=0; d<3; d++)
            centroid[d] /= members.size();

        double score = 0.0;
        for (int d=0; d<3; d++)
            score += (centroid[d] - ideal[d]) * (centroid[d] - ideal[d]);

        if (score < bestScore)
        {
            bestScore = score;
            bestCluster = c.first;
        }
    }


    NS_LOG_DEBUG("DBSCAN: Found " << clusterId << " clusters from " 
             << n << " nodes. Best cluster has " 
             << clusterMembers[bestCluster].size() << " members");

    // --- Step 5: Output --------------------------------------------------------

    std::vector<Ipv4Address> output;

    if (bestCluster >= 0)
    {
        for (int idx : clusterMembers[bestCluster])
            output.push_back(points[idx].ip);
    }

    if (output.empty())
    {
        for (auto &p : points)
            output.push_back(p.ip);
    }

    return output;
}

void
RoutingTable::Purge ()
{
  NS_LOG_FUNCTION (this);
  if (m_ipv4AddressEntry.empty ())
    {
      return;
    }
  for (std::map<Ipv4Address, RoutingTableEntry>::iterator i =
         m_ipv4AddressEntry.begin (); i != m_ipv4AddressEntry.end (); )
    {
      if (i->second.GetLifeTime () < Seconds (0))
        {
          if (i->second.GetFlag () == INVALID)
            {
              std::map<Ipv4Address, RoutingTableEntry>::iterator tmp = i;
              ++i;
              m_ipv4AddressEntry.erase (tmp);
            }
          else if (i->second.GetFlag () == VALID)
            {
              NS_LOG_LOGIC ("Invalidate route with destination address " << i->first);
              i->second.Invalidate (m_badLinkLifetime);
              ++i;
            }
          else
            {
              ++i;
            }
        }
      else
        {
          ++i;
        }
    }
}

void
RoutingTable::Purge (std::map<Ipv4Address, RoutingTableEntry> &table) const
{
  NS_LOG_FUNCTION (this);
  if (table.empty ())
    {
      return;
    }
  for (std::map<Ipv4Address, RoutingTableEntry>::iterator i =
         table.begin (); i != table.end (); )
    {
      if (i->second.GetLifeTime () < Seconds (0))
        {
          if (i->second.GetFlag () == INVALID)
            {
              std::map<Ipv4Address, RoutingTableEntry>::iterator tmp = i;
              ++i;
              table.erase (tmp);
            }
          else if (i->second.GetFlag () == VALID)
            {
              NS_LOG_LOGIC ("Invalidate route with destination address " << i->first);
              i->second.Invalidate (m_badLinkLifetime);
              ++i;
            }
          else
            {
              ++i;
            }
        }
      else
        {
          ++i;
        }
    }
}

bool
RoutingTable::MarkLinkAsUnidirectional (Ipv4Address neighbor, Time blacklistTimeout)
{
  NS_LOG_FUNCTION (this << neighbor << blacklistTimeout.As (Time::S));
  std::map<Ipv4Address, RoutingTableEntry>::iterator i =
    m_ipv4AddressEntry.find (neighbor);
  if (i == m_ipv4AddressEntry.end ())
    {
      NS_LOG_LOGIC ("Mark link unidirectional to  " << neighbor << " fails; not found");
      return false;
    }
  i->second.SetUnidirectional (true);
  i->second.SetBlacklistTimeout (blacklistTimeout);
  i->second.SetRreqCnt (0);
  NS_LOG_LOGIC ("Set link to " << neighbor << " to unidirectional");
  return true;
}

void
RoutingTable::Print (Ptr<OutputStreamWrapper> stream, Time::Unit unit /* = Time::S */) const
{
  std::map<Ipv4Address, RoutingTableEntry> table = m_ipv4AddressEntry;
  Purge (table);
  std::ostream* os = stream->GetStream ();
  // Copy the current ostream state
  std::ios oldState (nullptr);
  oldState.copyfmt (*os);

  *os << std::resetiosflags (std::ios::adjustfield) << std::setiosflags (std::ios::left);
  *os << "\nAODV Routing table\n";
  *os << std::setw (16) << "Destination";
  *os << std::setw (16) << "Gateway";
  *os << std::setw (16) << "Interface";
  *os << std::setw (16) << "Flag";
  *os << std::setw (16) << "Expire";
  *os << "Hops" << std::endl;
  for (std::map<Ipv4Address, RoutingTableEntry>::const_iterator i =
         table.begin (); i != table.end (); ++i)
    {
      i->second.Print (stream, unit);
    }
  *stream->GetStream () << "\n";
}

}
}
