//
// Created by Seyedali Tabaeiaghdaei on 21.03.22.
//

#include "src/SCION/headers/user_defined_events.h"
#include "src/SCION/headers/time_server.h"

namespace ns3 {
void
UserDefinedEvents::RunUserSpecifiedEvent (const std::string &funcName,
                                             std::vector<std::string> vec)
{
  switch (vec.size ())
    {
    case 0:
      functionToFunctionName[funcName].operator()<0> (vec);
      break;
    case 1:
      functionToFunctionName[funcName].operator()<1> (vec);
      break;
    case 2:
      functionToFunctionName[funcName].operator()<2> (vec);
      break;
    case 3:
      functionToFunctionName[funcName].operator()<3> (vec);
      break;
    case 4:
      functionToFunctionName[funcName].operator()<4> (vec);
      break;
    case 5:
      functionToFunctionName[funcName].operator()<5> (vec);
      break;
    case 6:
      functionToFunctionName[funcName].operator()<6> (vec);
      break;
    case 7:
      functionToFunctionName[funcName].operator()<7> (vec);
      break;
    case 8:
      functionToFunctionName[funcName].operator()<8> (vec);
      break;
    case 9:
      functionToFunctionName[funcName].operator()<9> (vec);
      break;
    case 10:
      functionToFunctionName[funcName].operator()<10> (vec);
      break;
    default:
      break;
    }
}

void
UserDefinedEvents::ConstructFuncMap ()
{
  functionToFunctionName["add_host"] = FunctionFactory (&UserDefinedEvents::AddAHost, this);
  functionToFunctionName["link_down"] = FunctionFactory (&UserDefinedEvents::LinkDown, this);
  functionToFunctionName["link_up"] = FunctionFactory (&UserDefinedEvents::LinkUp, this);
  functionToFunctionName["send_packet"] =
      FunctionFactory (&UserDefinedEvents::SendAPacket, this);
  functionToFunctionName["send_packet_batch"] =
      FunctionFactory (&UserDefinedEvents::SendPacketBatch, this);
  functionToFunctionName["time_references_down"] =
      FunctionFactory (&UserDefinedEvents::TimeReferencesDown, this);
  functionToFunctionName["time_references_up"] =
      FunctionFactory (&UserDefinedEvents::TimeReferencesUp, this);
}

void
UserDefinedEvents::ReadAndScheduleUserDefinedEvents (const std::string &eventsFileStr)
{
  nlohmann::json eventsJson;
  std::ifstream eventsFile (eventsFileStr);
  eventsFile >> eventsJson;
  eventsFile.close ();

  for (auto const &event : eventsJson.at ("events"))
    {
      Time time = Time ((std::string) event["time"]);
      std::string funcName = (std::string) event["type"];
      std::vector<std::string> argsV;

      for (uint32_t i = 0; i < event["args"].size (); ++i)
        {
          argsV.push_back ((std::string) event["args"][i]);
        }

      Simulator::Schedule (time, &UserDefinedEvents::RunUserSpecifiedEvent, this, funcName, argsV);
    }
}

void
UserDefinedEvents::AddAHost (std::string isdNumber, std::string realAsNo,
                               std::string localAddress)
{
}

void
UserDefinedEvents::LinkDown (std::string isdNumber, std::string realAsNo, std::string ifId)
{
}
void
UserDefinedEvents::LinkUp (std::string isdNumber, std::string realAsNo, std::string ifId)
{
}

void
UserDefinedEvents::SendAPacket (std::string srcIsdNumber, std::string realSrcAsNo,
                                  std::string srcLocalAddress, std::string dstIsdNumber,
                                  std::string realDstAsNo, std::string dstLocalAddress,
                                  std::string pyloadSize)
{
}

void
UserDefinedEvents::SendPacketBatch (std::string srcIsdNumber, std::string realSrcAsNo,
                                      std::string srcLocalAddress, std::string dstIsdNumber,
                                      std::string realDstAsNo, std::string dstLocalAddress,
                                      std::string pyloadSize, std::string noPkts)
{
}

void
UserDefinedEvents::TimeReferencesDown ()
{
  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *scionAs = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      TimeServer *timeServer = dynamic_cast<TimeServer *> (scionAs->GetHost (2));

      timeServer->referenceTimeType = ReferenceTimeType::off;
    }
}

void
UserDefinedEvents::TimeReferencesUp ()
{
  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *scionAs = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      TimeServer *time_server = dynamic_cast<TimeServer *> (scionAs->GetHost (2));

      time_server->referenceTimeType = ReferenceTimeType::on;
    }
}
} // namespace ns3
