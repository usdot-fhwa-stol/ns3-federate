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

#include <exception>
#include <string>
#include <unistd.h>

#include "ns3/log.h"
#include "ns3/core-module.h"
#include "mosaic-ns3-server.h"
#include "ns3/config-store.h"

#include <algorithm>
#include <libxml2/libxml/xpath.h>
#include <libxml2/libxml/tree.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("MosaicStarter");

struct NetworkConfig {
    std::string commType;
    int numOfNodes;
};

static LogLevel ParseLogLevel(const std::string & levelString) {
    //Taken from ns-3 environment parsing of log level
    unsigned int level = 0;
    std::string::size_type cur_lev = 0;
    std::string::size_type next_lev;
    bool pre_pipe = true;
    do {
        next_lev = levelString.find("|", cur_lev);
        std::string lev = levelString.substr(cur_lev, next_lev - cur_lev);

        if (lev == "error") {
            level |= LOG_ERROR;
        } else if (lev == "warn") {
            level |= LOG_WARN;
        } else if (lev == "debug") {
            level |= LOG_DEBUG;
        } else if (lev == "info") {
            level |= LOG_INFO;
        } else if (lev == "function") {
            level |= LOG_FUNCTION;
        } else if (lev == "logic") {
            level |= LOG_LOGIC;
        } else if (pre_pipe && ((lev == "all") || (lev == "*"))) {
            level |= LOG_LEVEL_ALL;
        } else if ((lev == "prefix_func") || (lev == "func")) {
            level |= LOG_PREFIX_FUNC;
        } else if ((lev == "prefix_time") || (lev == "time")) {
            level |= LOG_PREFIX_TIME;
        } else if ((lev == "prefix_node") || (lev == "node")) {
            level |= LOG_PREFIX_NODE;
        } else if ((lev == "prefix_level") || (lev == "level")) {
            level |= LOG_PREFIX_LEVEL;
        } else if ((lev == "prefix_all") ||
                (!pre_pipe && ((lev == "all") || (lev == "*")))
                ) {
            level |= LOG_PREFIX_ALL;
        } else if (lev == "level_error") {
            level |= LOG_LEVEL_ERROR;
        } else if (lev == "level_warn") {
            level |= LOG_LEVEL_WARN;
        } else if (lev == "level_debug") {
            level |= LOG_LEVEL_DEBUG;
        } else if (lev == "level_info") {
            level |= LOG_LEVEL_INFO;
        } else if (lev == "level_function") {
            level |= LOG_LEVEL_FUNCTION;
        } else if (lev == "level_logic") {
            level |= LOG_LEVEL_LOGIC;
        } else if (lev == "level_all") {
            level |= LOG_LEVEL_ALL;
        } else if (lev == "**") {
            level |= LOG_LEVEL_ALL | LOG_PREFIX_ALL;
        } else {
            std::cerr << "Could not parse log level " << lev << std::endl;
        }
        cur_lev = next_lev + 1;
        pre_pipe = false;
    } while (next_lev != std::string::npos);
    return (LogLevel) level;
}

void SetLogLevels(const std::string & configFile) {
    xmlChar *xpath = (xmlChar *) "//ns3/LogLevel/component";
    xmlDocPtr doc = xmlParseFile(configFile.c_str());
    xmlXPathContextPtr context = xmlXPathNewContext(doc);
    xmlXPathObjectPtr result = xmlXPathEvalExpression(xpath, context);

    std::string componentString;
    std::string levelString;
    for (int i = 0; result->nodesetval != nullptr && i < result->nodesetval->nodeNr; i++) {
        
        xmlNodePtr nodePtr = result->nodesetval->nodeTab[i];
        for (xmlAttrPtr attr = nodePtr->properties; NULL != attr; attr = attr->next) {
            std::string attrName((char *) attr->name);
            
            if (attrName == "name") {
                componentString.assign((char *) xmlNodeListGetString(doc, attr->children, 1));
            } else if (attrName == "value") {
                levelString.assign((char *) xmlNodeListGetString(doc, attr->children, 1));
            }
        }
        
        if (componentString == "" || levelString == "") {
            std::cerr << "Could not parse log level for component [" << componentString << "], level [" << levelString << "]" << std::endl;
            continue;
        }

        std::transform(levelString.begin(), levelString.end(), levelString.begin(),
                [](unsigned char c) -> unsigned char {
                    return std::tolower(c); });
        LogLevel level = ParseLogLevel(levelString);
        
        if (componentString == "*") {
            LogComponentEnableAll(level);
        } else {
            LogComponentEnable(componentString.c_str(), level);
        }        
    }

    xmlXPathFreeObject(result);
}

NetworkConfig GetNetworkConfig(const std::string &configFile) {
    NetworkConfig config;
    xmlDocPtr doc = xmlParseFile(configFile.c_str());
    xmlXPathContextPtr context = xmlXPathNewContext(doc);

    // XPath to find CommType and NumOfNodes
    xmlChar *commTypeXpath = (xmlChar *) "//ns3/NetworkConfig/component[@name='CommType']";
    xmlChar *numOfNodesXpath = (xmlChar *) "//ns3/NetworkConfig/component[@name='NumOfNodes']";

    // Get CommType
    xmlXPathObjectPtr result = xmlXPathEvalExpression(commTypeXpath, context);
    if (result && result->nodesetval && result->nodesetval->nodeNr > 0) {
        xmlNodePtr node = result->nodesetval->nodeTab[0];
        config.commType = (char *) xmlNodeListGetString(doc, node->children, 1);
    }
    xmlXPathFreeObject(result);

    // Get NumOfNodes
    result = xmlXPathEvalExpression(numOfNodesXpath, context);
    if (result && result->nodesetval && result->nodesetval->nodeNr > 0) {
        xmlNodePtr node = result->nodesetval->nodeTab[0];
        std::string numOfNodesStr = (char *) xmlNodeListGetString(doc, node->children, 1);
        config.numOfNodes = std::atoi(numOfNodesStr.c_str());
    }
    xmlXPathFreeObject(result);

    xmlFreeDoc(doc);
    xmlCleanupParser();

    return config;
}

int main(int argc, char *argv[]) {
    using namespace std;
    //default values
    int port = 0;
    int cmdPort = 0;
    std::string configFile = "scratch/ns3_federate_config.xml";

    GlobalValue::Bind("SchedulerType", StringValue("ns3::ListScheduler"));
    GlobalValue::Bind("SimulatorImplementationType", StringValue("ns3::MosaicSimulatorImpl"));

    MosaicNodeManager::GetTypeId();
    CommandLine cmd;
    cmd.Usage("Mosaic ns-3 federate.\n\tcmdPort - command port");
    cmd.AddValue("cmdPort", "the command port", cmdPort);
    cmd.AddValue("port", "the port", port);
    cmd.AddValue("configFile", "the configuration file to evaluate", configFile);
    cmd.Parse(argc, argv);

    if (access(configFile.c_str(), F_OK) == -1) {
        cerr << "Could not open configuration file \"" << configFile << "\"" << endl;
        return -1;
    }

    Config::SetDefault("ns3::ConfigStore::Filename", StringValue(configFile.c_str()));
    Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("Xml"));
    Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Load"));
    ConfigStore xmlConfig;

    xmlConfig.ConfigureDefaults();
    xmlConfig.ConfigureAttributes();

    SetLogLevels(configFile);
    
    NetworkConfig config = GetNetworkConfig(configFile);

    try {
        MosaicNs3Server server(port, cmdPort, config.commType);
        
        // For LTE mode, all NS-3 nodes need to be pre-define. Provide number of nodes variable is necessary under this mode
        if (config.commType == "LTE")
            server.SetNumOfNode(config.numOfNodes);
        server.processCommandsUntilSimStep();
    } catch (int e) {
        NS_LOG_ERROR("Caught exception [" << e << "]. Exiting ns-3 federate ");
        return -1;
    }

    Simulator::Destroy();
    return 0;
}
