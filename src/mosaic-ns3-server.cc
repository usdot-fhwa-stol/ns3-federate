/*
 * Copyright (c) 2020 Fraunhofer FOKUS and others. All rights reserved.
 *
 * Contact: mosaic@fokus.fraunhofer.de
 *
 * This class is developed for the MOSAIC-NS-3 coupling.
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
 */

#include "mosaic-ns3-server.h"

#include "ns3/node-list.h"
#include "mosaic-simulator-impl.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("MosaicNs3Server");

namespace ns3 {
    using namespace ClientServerChannelSpace;

    /**
     * @brief initialize the MosaicNs3Server with the given port and the given MosaicNodemanager
     *
     * @param port  port for receiving the commands from MOSAIC
     * @param MosaicNodeManger MosaicNodeManger given from the NS3 starter script
     */
    MosaicNs3Server::MosaicNs3Server(int port, int cmdPort, std::string commType) {
        std::cout << "Starting federate on port " << port << "\n";
        if (commType == "DSRC"){
            m_commType = CommunicationType::DSRC;
        }
        else if (commType == "LTE"){
            m_commType = CommunicationType::LTE;
        }
        else{
            NS_LOG_ERROR("Unknown communication type:" << commType);
            return;
        }            

        if (cmdPort > 0) {
            std::cout << "Once connected, federate will listen to commands on port " << cmdPort << "\n";
        }
        m_nodeManager = CreateObject<MosaicNodeManager>();
        m_nodeManager->Configure(this, m_commType);
        m_closeConnection = false;

        std::cout << "Trying to prepare federateAmbassadorChannel on port " << port << " " << std::endl;
        uint16_t actPort = federateAmbassadorChannel.prepareConnection("0.0.0.0", port);
        std::cout << "Mosaic-NS3-Server connecting on OutPort=" << actPort << std::endl;
        federateAmbassadorChannel.connect();
        federateAmbassadorChannel.writeCommand(CMD_INIT);

        actPort = ambassadorFederateChannel.prepareConnection("0.0.0.0", cmdPort);
        if (actPort < 1) {
            exit(-1);
        }
        federateAmbassadorChannel.writePort(actPort);
        ambassadorFederateChannel.connect();

        if (ambassadorFederateChannel.readCommand() == CMD_INIT) {
            CSC_init_return init_message;
            ambassadorFederateChannel.readInit(init_message);
            m_startTime = init_message.start_time;
            m_endTime = init_message.end_time;
            if (m_startTime >= 0 && m_endTime >= 0 && m_endTime >= m_startTime) {
                ambassadorFederateChannel.writeCommand(CMD_SUCCESS);
            } else {
                ambassadorFederateChannel.writeCommand(CMD_END);
            }
        } else {
            NS_LOG_INFO("ERROR command port not found");
        }


        if (m_commType == CommunicationType::DSRC){
            if (!m_dsrc_init_complete){
                m_nodeManager->InitDsrc();
                m_dsrc_init_complete = true;
            }
        }else if (m_commType == CommunicationType::LTE){
            if (!m_lte_init_complete){
                m_nodeManager->InitLte(m_numOfNodes);
                m_lte_init_complete = true;
            }
        }else{
            NS_LOG_ERROR("Unknown communication type:" << m_commType);
            return;
        }
        std::cout << "ns3Server: created new connection to " << port << std::endl;
    }

    // void MosaicNs3Server::SetNumOfNodes(int numOfNodes){
    //     m_numOfNodes = numOfNodes;
    // }

    /**
     * @brief NS3 Magic: a specialized entry-point is needed to create this class from a end-user script. The call of the constructor is forbidden by the NS3.
     * @brief this function is called by the starter script and obtains the whole simulation
     * @brief the function will call the dispatcher after the initialization of MosaicNs3Server and the creation of the first dummy event
     *
     * @param port   port for receiving the commands from MOSAIC
     * @param MosaicNodeManger  MosaicNodeManger given from the NS3 starter script
     */
    void MosaicNs3Server::processCommandsUntilSimStep() {
        try {
            if (!m_closeConnection) {

                Ptr<MosaicSimulatorImpl> Sim = DynamicCast<MosaicSimulatorImpl> (Simulator::GetImplementation());
                Sim->AttachNS3Server(this);

                //create the dummy event (end of the simulation) to avoid a empty event-list
                //NS3 will throw an exception if the event-list is empty
                Time tNext = NanoSeconds(m_endTime);
                Simulator::Schedule(tNext, &MosaicNs3Server::Close, this);
            } else {
                return;
            }

            while (!m_closeConnection) {
                NS_LOG_INFO("NumberOfNodes= " << ns3::NodeList::GetNNodes());
                dispatchCommand();
            }

        } catch (std::invalid_argument &e) {
            NS_LOG_INFO("ns-3 server --> Invalid argument in Mosaic-ns3-server.cc, processCommandsUntilSimStep() " << e.what());
        }
        //write the message that the server is finished
        NS_LOG_INFO("ns-3 server --> Finishing server.... ");

        m_closeConnection = true;
    }

    /**
     * @brief This function dispatch all commands from MOSAIC and sends the results back the the framework
     *
     * @return commandId the Id of the last command
     */
    int MosaicNs3Server::dispatchCommand() {

        //gets the pointer of the simulator
        Ptr<MosaicSimulatorImpl> sim = DynamicCast<MosaicSimulatorImpl> (Simulator::GetImplementation());
        if (nullptr == sim) {
            NS_LOG_INFO("Could not find Mosaic simulator implementation \n");
            m_closeConnection = true;
            return 0;
        }

        //read the commandId from the channel
        CMD commandId = ambassadorFederateChannel.readCommand();
        switch (commandId) {
            case CMD_INIT:
                //the CMD:INIT is not permitted after the initialization the the MosaicNs3Servers
                NS_LOG_INFO("ERROR dispatchCommand gets a INIT");
                break;
            case CMD_UPDATE_NODE:
            {
                CSC_update_node_return update_node_message;
                ambassadorFederateChannel.readUpdateNode(update_node_message);
                Time tNext = NanoSeconds(update_node_message.time);
                Time tDelay = tNext - sim->Now();
                for (std::vector<CSC_node_data>::iterator it = update_node_message.properties.begin(); it != update_node_message.properties.end(); ++it) {

                    if (update_node_message.type == UPDATE_ADD_RSU) {
                        sim->Schedule(tDelay, MakeEvent(&MosaicNodeManager::CreateMosaicNode, m_nodeManager, it->id, Vector(it->x, it->y, 0.0)));
                        NS_LOG_DEBUG("Received ADD_RSU: ID=" << it->id << " posx=" << it->x << " posy=" << it->y << " tNext=" << tNext);

                    } else if (update_node_message.type == UPDATE_ADD_VEHICLE) {
                        sim->Schedule(tDelay, MakeEvent(&MosaicNodeManager::CreateMosaicNode, m_nodeManager, it->id, Vector(it->x, it->y, 0.0)));
                        NS_LOG_DEBUG("Received ADD_VEHICLE: ID=" << it->id << " posx=" << it->x << " posy=" << it->y << " tNext=" << tNext);

                    } else if (update_node_message.type == UPDATE_MOVE_NODE) {
                        sim->Schedule(tDelay, MakeEvent(&MosaicNodeManager::UpdateNodePosition, m_nodeManager, it->id, Vector(it->x, it->y, 0.0)));
                        NS_LOG_DEBUG("Received MOVE_NODES: ID=" << it->id << " posx=" << it->x << " posy=" << it->y << " tNext=" << tNext);

                    } else if (update_node_message.type == UPDATE_REMOVE_NODE) {

                        //It is not allowed to delete a node during the simulation step -> the node will be deactivated
                        //void (std::vector<int>::*fctptr)(const int&) = &std::vector<int>::push_back;
                        //sim->ScheduleAtTime(tNext, MakeEvent(fctptr, &m_deactivatedNodes, it->id));
                        sim->Schedule(tDelay, MakeEvent(&MosaicNodeManager::DeactivateNode, m_nodeManager, it->id));
                        NS_LOG_DEBUG("Received REMOVE_NODES: ID=" << it->id << " tNext=" << tNext);
                    }
                }
                ambassadorFederateChannel.writeCommand(CMD_SUCCESS);
                break;
            }

                // advance the next time step and run the simulation read the next time step
            case CMD_ADVANCE_TIME:
                uint64_t advancedTime;
                advancedTime = ambassadorFederateChannel.readTimeMessage();

                NS_LOG_DEBUG("Received ADVANCE_TIME " << advancedTime);
                //run the simulation (function RunSimStep) while the time of the next event is smaller than the next time step
                m_eventSentUp = false;
                while (!Simulator::IsFinished() && NanoSeconds(advancedTime) >= sim->Next().GetNanoSeconds()) {
                    sim->RunOneEvent();
                }

                //write the confirmation at the end of the sequence
                federateAmbassadorChannel.writeCommand(CMD_END);
                federateAmbassadorChannel.writeTimeMessage(Simulator::Now().GetNanoSeconds());
                break;

            case CMD_CONF_RADIO:

                try {

                    CSC_config_message config_message;
                    ambassadorFederateChannel.readConfigurationMessage(config_message);
                    Time tNext = NanoSeconds(config_message.time);
                    Time tDelay = tNext - sim->Now();
                    int transmitPower = -1;
                    bool radioTurnedOn = false;
                    if (config_message.num_radios == SINGLE_RADIO) {
                        radioTurnedOn = true; //other modes currently not supported, other modes turn off radio
                        transmitPower = config_message.primary_radio.tx_power;
                    }

                    sim->Schedule(tDelay, MakeEvent(&MosaicNodeManager::ConfigureNodeRadio, m_nodeManager, config_message.node_id, radioTurnedOn, transmitPower));

                } catch (int e) {
                    NS_LOG_INFO("Error while reading configuration message \n");
                    m_closeConnection = true;
                }
                break;

            case CMD_MSG_SEND:
            {
                try {
                    CSC_send_message send_message;
                    ambassadorFederateChannel.readSendMessage(send_message);
                    //Convert the IP address
                    Ipv4Address ip(send_message.topo_address.ip_address);
                    int id = m_nodeManager->GetNs3NodeId(send_message.node_id);
                    NS_LOG_DEBUG("Received V2X_MESSAGE_TRANSMISSION id: " << id << " sendTime: " << send_message.time << " length: " << send_message.length);

                    //create a sending jitter to avoid concurrently sending
                    unsigned long long rando;
                    rando = (rand() % 100000000);
                    unsigned long long sendTime;
                    sendTime = send_message.time + rando;
                    Time tNext = NanoSeconds(sendTime);
                    Time tDelay = tNext - sim->Now();

                    sim->Schedule(tDelay, MakeEvent(&MosaicNodeManager::SendMsg, m_nodeManager, send_message.node_id, 0, send_message.message_id, send_message.length, ip));
                } catch (int e) {
                }
                break;
            }
            case CMD_SHUT_DOWN:
                m_closeConnection = true;
                Simulator::Destroy();
                break;

            default:
                NS_LOG_INFO("Command not implemented in ns3 " << commandId << "\n");
                m_closeConnection = true;
        }

        return commandId;
    }

    void MosaicNs3Server::writeNextTime(unsigned long long nextTime) {
        federateAmbassadorChannel.writeCommand(CMD_NEXT_EVENT);
        federateAmbassadorChannel.writeTimeMessage(nextTime);
    }

    bool MosaicNs3Server::AddRecvPacket(unsigned long long recvTime, Ptr<Packet> pack, int nodeID, int msgID) {
        federateAmbassadorChannel.writeCommand(CMD_MSG_RECV);
        federateAmbassadorChannel.writeReceiveMessage(recvTime, nodeID, msgID, CCH, 0);
        m_eventSentUp = true;
        return true;
    }

    void MosaicNs3Server::Close() {
        m_closeConnection = true;
    }
} //END Namespace
