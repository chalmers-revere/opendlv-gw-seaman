/*
 * Copyright (C) 2018 Ola Benderius
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <thread>
#include <cstring>

#include <zmq.h>
#include <json.hpp>

#include "cluon-complete.hpp"
#include "opendlv-standard-message-set.hpp"

int32_t main(int32_t argc, char **argv)
{
  int32_t retCode{0};
  auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
  if (0 == commandlineArguments.count("cid") || 0 == commandlineArguments.count("seaman_ip")) {
    std::cerr << argv[0] << " is an OpenDLV interface to the SSPA Seaman ship simulator." << std::endl;
    std::cerr << "Usage:   " << argv[0] << " --cid=<OpenDaVINCI session> --seaman_ip=<IP to the Seaman simulation server> --verbose" << std::endl;
    std::cerr << "Example: " << argv[0] << " --cid=111 --seaman_ip=192.168.0.1" << std::endl;
    retCode = 1;
  } else {
    bool const VERBOSE{commandlineArguments.count("verbose") != 0};
    uint16_t const CID = static_cast<uint16_t>(std::stoi(commandlineArguments["cid"]));
    std::string const SEAMAN_IP = commandlineArguments["seaman_ip"];
    uint16_t const TCP_PORT = 43000;
    uint16_t const UDP_PORT = 8888;

    int16_t starboardEngineRequest = 0; // -100 (max reverse), 100 (max forwards)
    int16_t portEngineRequest = 0; // -100 (max reverse), 100 (max forwards)
    int16_t starboardRudderRequest = 0; // -100 (max port), 100 (max starboard)
    int16_t portRudderRequest = 0; // -100 (max port), 100 (max starboard)
    int16_t tunnerThruster1Request = 0; // -100 (max port), 100 (max starboard)
    int16_t tunnerThruster2Request = 0; // -100 (max port), 100 (max starboard)

    cluon::UDPSender sender{SEAMAN_IP, UDP_PORT};

    auto onPedalPositionRequest{[&VERBOSE, &starboardEngineRequest, &portEngineRequest, 
      &starboardRudderRequest, &portRudderRequest, &tunnerThruster1Request, 
      &tunnerThruster2Request, &sender](cluon::data::Envelope &&envelope)
      {
        uint32_t const SENDER_STAMP = envelope.senderStamp();
        auto const PEDAL_POSITION_REQUEST = cluon::extractMessage<opendlv::proxy::PedalPositionRequest>(std::move(envelope));
        if (0 == SENDER_STAMP) {
          starboardEngineRequest = static_cast<int16_t>(PEDAL_POSITION_REQUEST.position() * 100.0);
        } else if (1 == SENDER_STAMP) {
          portEngineRequest = static_cast<int16_t>(PEDAL_POSITION_REQUEST.position() * 100.0);
        } else if (2 == SENDER_STAMP) {
          starboardRudderRequest = static_cast<int16_t>(PEDAL_POSITION_REQUEST.position() * 100.0);
        } else if (3 == SENDER_STAMP) {
          portRudderRequest = static_cast<int16_t>(PEDAL_POSITION_REQUEST.position() * 100.0);
        } else if (4 == SENDER_STAMP) {
          tunnerThruster1Request = static_cast<int16_t>(PEDAL_POSITION_REQUEST.position() * 100.0);
        } else if (5 == SENDER_STAMP) {
          tunnerThruster2Request = static_cast<int16_t>(PEDAL_POSITION_REQUEST.position() * 100.0);
        }
      
        if (VERBOSE) {
          std::cout << "Sending:" << std::endl;
          std::cout << " .. starboard engine request: " << starboardEngineRequest << std::endl;
          std::cout << " .. port engine request: " << portEngineRequest << std::endl;
          std::cout << " .. starboard rudder request: " << starboardRudderRequest << std::endl;
          std::cout << " .. port rudder request: " << portRudderRequest << std::endl;
          std::cout << " .. tunner thruster 1 request: " << tunnerThruster1Request << std::endl;
          std::cout << " .. tunner thruster 2 request: " << tunnerThruster2Request << std::endl;
        }

        uint8_t const DATA_LENGTH = 16;
        uint8_t buffer[DATA_LENGTH];
        buffer[0] = 1;
        buffer[1] = 7; // Number of fields (last one is empty).
        memcpy(buffer + 2, &starboardEngineRequest, 2);
        memcpy(buffer + 4, &portEngineRequest, 2);
        memcpy(buffer + 6, &starboardRudderRequest, 2);
        memcpy(buffer + 8, &portRudderRequest, 2);
        memcpy(buffer + 10, &tunnerThruster1Request, 2);
        memcpy(buffer + 12, &tunnerThruster2Request, 2);

        std::string output(reinterpret_cast<char const*>(buffer), DATA_LENGTH);
        sender.send(std::move(output));
      }};
    
    cluon::OD4Session od4{CID};
    od4.dataTrigger(opendlv::proxy::PedalPositionRequest::ID(), onPedalPositionRequest);

    std::string const ZMQ_ADDRESS = "tcp://" + SEAMAN_IP + ":" + std::to_string(TCP_PORT);
  
    void *context = zmq_ctx_new();
    void *subscriber = zmq_socket(context, ZMQ_SUB);
    zmq_connect(subscriber, ZMQ_ADDRESS.c_str());
    zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);

    char buffer[1024];
    while (od4.isRunning()) {
      int32_t const INPUT_LENGTH = zmq_recv(subscriber, buffer, 1024, 0);

      std::string const BUFFER_STR(buffer);
      std::string const INPUT = BUFFER_STR.substr(0, INPUT_LENGTH);
      auto const INPUT_JSON = nlohmann::json::parse(INPUT);

      if (VERBOSE) {
        std::cout << "Got: " << INPUT << std::endl;

        double const SPEED = INPUT_JSON["shiman"]["sog"];
        double const HEADING = INPUT_JSON["shiman"]["psdg"];
        std::cout << "Speed: " << SPEED << " knots." << std::endl;
        std::cout << "Heading: " << HEADING << " knots." << std::endl;
      }
    }
  }
  return retCode;
}
