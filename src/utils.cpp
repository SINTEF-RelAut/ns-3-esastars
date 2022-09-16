/**
 * @file utils.cpp
 * @see utils.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#include <cmath>
#include <random>
#include <set>

#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {

double
GetMedian (std::multiset<int64_t> &data)
{
  if (data.empty ())
    throw std::length_error ("Cannot calculate median value for empty dataset");

  const size_t n = data.size ();
  double median = 0;

  auto iter = data.cbegin ();
  std::advance (iter, n / 2);

  // Middle or average of two middle values
  if (n % 2 == 0)
    {
      const auto iter2 = iter--;
      median = double (*iter + *iter2) / 2; // data[n/2 - 1] AND data[n/2]
    }
  else
    {
      median = *iter;
    }
  return median;
}

std::vector<std::string> &
Split (const std::string &s, char delim, std::vector<std::string> &elems)
{
  std::stringstream ss (s);
  std::string item;
  while (std::getline (ss, item, delim))
    {
      elems.push_back (item);
    }
  return elems;
}

Ld_t
LinkLevelJaccardDistanceBetweenTwoPaths (Beacon *beacon1, Beacon *beacon2)
{
  std::set<uint32_t> set_of_links_on_path1;
  int32_t intersection = 0;

  for (auto const &linkInfo : beacon1->path)
    {
      set_of_links_on_path1.insert (UPPER_32_BITS (linkInfo));
    }

  for (auto const &linkInfo : beacon2->path)
    {
      if (set_of_links_on_path1.find (UPPER_32_BITS (linkInfo)) != set_of_links_on_path1.end ())
        {
          intersection++;
        }
      else
        {
          set_of_links_on_path1.insert (UPPER_32_BITS (linkInfo));
        }
    }

  return 1 - 1.0 * intersection / set_of_links_on_path1.size ();
}

Ld_t
AsLevelJaccardDistanceBetweenTwoPaths (Beacon *beacon1, Beacon *beacon2)
{
  std::set<uint16_t> set_of_ASes_on_path1;
  int32_t intersection = 0;

  for (auto const &linkInfo : beacon1->path)
    {
      set_of_ASes_on_path1.insert (UPPER_16_BITS (linkInfo));
    }

  for (auto const &linkInfo : beacon2->path)
    {
      if (set_of_ASes_on_path1.find (UPPER_16_BITS (linkInfo)) != set_of_ASes_on_path1.end ())
        {
          intersection++;
        }
      else
        {
          set_of_ASes_on_path1.insert (UPPER_16_BITS (linkInfo));
        }
    }
  return 1 - 1.0 * intersection / set_of_ASes_on_path1.size ();
}

Ld_t
CalculateGreatCircleLatency (Ld_t lat1Deg, Ld_t long1Deg, Ld_t lat2Deg, Ld_t long2Deg)
{
  Ld_t distance = CalculateGreatCircleDistance (lat1Deg, long1Deg, lat2Deg, long2Deg);
  // 0.005 millisecods of latency per kilometer
  Ld_t latency = distance * 0.005;
  return latency;
}

Ld_t
CalculateGreatCircleDistance (Ld_t lat1Deg, Ld_t long1Deg, Ld_t lat2Deg, Ld_t long2Deg)
{
  Ld_t lat1 = lat1Deg * (M_PI) / 180;
  Ld_t long1 = long1Deg * (M_PI) / 180;
  Ld_t lat2 = lat2Deg * (M_PI) / 180;
  Ld_t long2 = long2Deg * (M_PI) / 180;

  // Haversine Formula
  Ld_t dlong = long2 - long1;
  Ld_t dlat = lat2 - lat1;

  Ld_t distance =
      6371 * 2 *
      asin (sqrt (pow (sin (dlat / 2), 2) + cos (lat1) * cos (lat2) * pow (sin (dlong / 2), 2)));

  return distance;
}

std::string
GetAttribute (rapidxml::xml_node<> *node, const std::string &name)
{
  rapidxml::xml_attribute<> *attr = node->first_attribute (name.c_str ());
  if (attr)
    {
      return attr->value ();
    }
  else
    {
      return std::string ();
    }
}

std::string
PropertyContainer::GetProperty (const std::string &name) const
{
  PropertiesType_t::const_iterator it;
  it = this->properties.find (name);

  if (it != this->properties.end ())
    return it->second;
  else
    exit (1);
}

void
PropertyContainer::SetProperty (const std::string &name, const std::string &value)
{
  this->properties[name] = value;
}

bool
PropertyContainer::HasProperty (const std::string &name) const
{
  PropertiesType_t::const_iterator it = this->properties.find (name);
  if (it == this->properties.end ())
    {
      return false;
    }
  else
    {
      return true;
    }
}

PropertyContainer
ParseProperties (rapidxml::xml_node<> *node)
{
  PropertyContainer p;
  rapidxml::xml_node<> *curNode = node->first_node ("property");

  while (curNode)
    {
      std::string name = GetAttribute (curNode, "name");
      if (name != "")
        {
          p.SetProperty (name, curNode->value ());
        }
      curNode = curNode->next_sibling ("property");
    }

  return p;
}

} // namespace ns3