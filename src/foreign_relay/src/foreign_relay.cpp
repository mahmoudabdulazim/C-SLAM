
// relay just passes messages on. it can be useful if you're trying to ensure
// that a message doesn't get sent twice over a wireless link, by having the
// relay catch the message and then do the fanout on the far side of the
// wireless link.
//
// Copyright (C) 2009, Morgan Quigley
//
 // Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//   * Neither the name of Stanford University nor the names of its
//     contributors may be used to endorse or promote products derived from
//     this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#include <cstdio>
#include "topic_tools/shape_shifter.h"
#include "topic_tools/parse.h"
#include "XmlRpc.h"
#include "ros/xmlrpc_manager.h"
#include "ros/network.h"

using std::string;
using std::vector;
using namespace topic_tools;

static ros::NodeHandle *g_node = NULL;
static bool g_advertised = false;
static string g_foreign_topic;
static string g_local_topic;
static ros::Publisher g_pub;
static string g_host;
static uint32_t g_port = 0;
static bool g_error = false;
typedef enum{MODE_ADV,MODE_SUB} relay_mode_t;
static relay_mode_t g_mode;

#define USAGE "USAGE: foreign_relay {adv|sub} FOREIGN_MASTER_URI FOREIGN_TOPIC LOCAL_TOPIC"
ros::XMLRPCManagerPtr g_xmlrpc_manager = ros::XMLRPCManager::instance();
void foreign_advertise(const std::string &type);
void foreign_unadvertise();
void foreign_subscribe();
void foreign_unsubscribe();
void in_cb(const boost::shared_ptr<ShapeShifter const>& msg);
void foreign_advertise(const std::string &type)
{
    XmlRpc::XmlRpcClient *client = g_xmlrpc_manager->getXMLRPCClient(g_host, g_port, "/");
    XmlRpc::XmlRpcValue args, result;
    args[0] = ros::this_node::getName();
    args[1] = g_foreign_topic;
    args[2] = type;
    args[3] = g_xmlrpc_manager->getServerURI();
    if (!client->execute("registerPublisher", args, result))
    {
        ROS_FATAL("Failed to contact foreign master at [%s:%d] to register [%s].", g_host.c_str(), g_port, g_foreign_topic.c_str());
        g_error = true;
        ros::shutdown();
    }
00084   ros::XMLRPCManager::instance()->releaseXMLRPCClient(client);
00085 }
00086
00087 void foreign_unadvertise()
00088 {
00089   XmlRpc::XmlRpcClient *client = g_xmlrpc_manager->getXMLRPCClient(g_host, g_port, "/");
00090   XmlRpc::XmlRpcValue args, result;
00091   args[0] = ros::this_node::getName();
00092   args[1] = g_foreign_topic;
00093   args[2] = g_xmlrpc_manager->getServerURI();
00094   if (!client->execute("unregisterPublisher", args, result))
00095   {
00096     ROS_ERROR("Failed to contact foreign master at [%s:%d] to unregister [%s].", g_host.c_str(), g_port, g_foreign_topic.c_str());
00097     g_error = true;
00098   }
00099   ros::XMLRPCManager::instance()->releaseXMLRPCClient(client);
00100 }
00101
00102 void foreign_subscribe()
00103 {
00104   XmlRpc::XmlRpcClient *client =
00105           g_xmlrpc_manager->getXMLRPCClient(g_host, g_port, "/");
00106   XmlRpc::XmlRpcValue args, result, payload;
00107   args[0] = ros::this_node::getName();
00108   args[1] = g_foreign_topic;
00109   args[2] = "*";
00110   args[3] = g_xmlrpc_manager->getServerURI();
00111   if (!client->execute("registerSubscriber", args, result))
00112   {
00113     ROS_FATAL("Failed to contact foreign master at [%s:%d] to register [%s].",
00114               g_host.c_str(), g_port, g_foreign_topic.c_str());
00115     g_error = true;
00116     ros::shutdown();
00117     return;
00118   }
00119
00120   {
00121     // Horrible hack: the response from registerSubscriber() can contain a
00122     // list of current publishers.  But we don't have a way of injecting them
00123     // into roscpp here.  Now, if we get a publisherUpdate() from the master,
00124     // everything will work.  So, we ask the master if anyone is currently
00125     // publishing the topic, grab the advertised type, use it to advertise
00126     // ourselves, then unadvertise, triggering a publisherUpdate() along the
00127     // way.
00128     XmlRpc::XmlRpcValue args, result, payload;
00129     args[0] = ros::this_node::getName();
00130     args[1] = std::string("");
00131     if(!client->execute("getPublishedTopics", args, result))
00132     {
00133       ROS_FATAL("Failed to call getPublishedTopics() on foreign master at [%s:%d]",
00134                 g_host.c_str(), g_port);
00135       g_error = true;
00136       ros::shutdown();
00137       return;
00138     }
00139     if (!ros::XMLRPCManager::instance()->validateXmlrpcResponse("getPublishedTopics", result, payload))
00140     {
00141       ROS_FATAL("Failed to get validate response to getPublishedTopics() from foreign master at [%s:%d]",
00142                 g_host.c_str(), g_port);
00143       g_error = true;
00144       ros::shutdown();
00145       return;
00146     }
00147     for(int i=0;i<payload.size();i++)
00148     {
00149       std::string topic = std::string(payload[i][0]);
00150       std::string type = std::string(payload[i][1]);
00151
00152       if(topic == g_foreign_topic)
00153       {
00154         foreign_advertise(type);
00155         foreign_unadvertise();
00156         break;
00157       }
00158     }
00159   }
00160
00161   ros::XMLRPCManager::instance()->releaseXMLRPCClient(client);
00162 }
00163
00164 void foreign_unsubscribe()
00165 {
00166   XmlRpc::XmlRpcClient *client = g_xmlrpc_manager->getXMLRPCClient(g_host, g_port, "/");
00167   XmlRpc::XmlRpcValue args, result;
00168   args[0] = ros::this_node::getName();
00169   args[1] = g_foreign_topic;
00170   args[2] = g_xmlrpc_manager->getServerURI();
00171   if (!client->execute("unregisterSubscriber", args, result))
00172   {
00173     ROS_ERROR("Failed to contact foreign master at [%s:%d] to unregister [%s].", g_host.c_str(), g_port, g_foreign_topic.c_str());
00174     g_error = true;
00175   }
00176   ros::XMLRPCManager::instance()->releaseXMLRPCClient(client);
00177 }
00178
00179 void in_cb(const boost::shared_ptr<ShapeShifter const>& msg)
00180 {
00181   if (!g_advertised)
00182   {
00183     ROS_INFO("Received message from: %s",
00184              (*(msg->__connection_header))["callerid"].c_str());
00185     if(g_mode == MODE_SUB)
00186     {
00187       // Advertise locally
00188       g_pub = msg->advertise(*g_node, g_local_topic, 10);
00189       ROS_INFO("Advertised locally as %s, with type %s",
00190                g_local_topic.c_str(),
00191                (*(msg->__connection_header))["type"].c_str());
00192     }
00193     else
00194     {
00195       // We advertise locally as a hack, to get things set up properly.
00196       g_pub = msg->advertise(*g_node, g_foreign_topic, 10);
00197       // Advertise at the foreign master.
00198       foreign_advertise((*(msg->__connection_header))["type"]);
00199       ROS_INFO("Advertised foreign as %s, with type %s",
00200                g_foreign_topic.c_str(),
00201                (*(msg->__connection_header))["type"].c_str());
00202     }
00203     g_advertised = true;
00204   }
00205   g_pub.publish(msg);
00206 }
00207
00208 int main(int argc, char **argv)
00209 {
00210   if (argc < 5)
00211   {
00212     ROS_FATAL(USAGE);
00213     return 1;
00214   }
00215   if(std::string(argv[1]) == "adv")
00216     g_mode = MODE_ADV;
00217   else if(std::string(argv[1]) == "sub")
00218     g_mode = MODE_SUB;
00219   else
00220   {
00221     ROS_FATAL(USAGE);
00222     return 1;
00223   }
00224   std::string foreign_master_uri;
00225   foreign_master_uri = argv[2];
00226   g_foreign_topic = argv[3];
00227   g_local_topic = argv[4];
00228   std::string local_topic_basename;
00229   if(!getBaseName(g_local_topic, local_topic_basename))
00230   {
00231     ROS_FATAL("Failed to extract basename from topic [%s]",
          g_local_topic.c_str());
return 1;
}
if (!ros::network::splitURI(foreign_master_uri, g_host, g_port))
{
ROS_FATAL("Couldn't parse the foreign master URI [%s] into a host:port pair.", foreign_master_uri.c_str());
  return 1;
}

char buf[1024];
snprintf(buf, sizeof(buf), "%d", ros::master::getPort());
ros::init(argc, argv, local_topic_basename + string("_foreign_relay_") + buf + "_" + ((g_mode == MODE_ADV) ? "adv" : "sub"),
          ros::init_options::AnonymousName);
ros::NodeHandle n;
ros::NodeHandle pnh("~");
g_node = &pnh;

ros::Subscriber sub;
if(g_mode == MODE_SUB)
{
// We subscribe locally as a hack, to get our callback set up properly.
sub = n.subscribe<ShapeShifter>(g_foreign_topic, 10, &in_cb,
ros::TransportHints().unreliable());
// Subscribe at foreign master.
foreign_subscribe();
}
else
{
// Subscribe at local master.
  sub = n.subscribe<ShapeShifter>(g_local_topic, 10, &in_cb);
}

ros::spin();

if(g_mode == MODE_SUB)
foreign_unsubscribe();
else
foreign_unadvertise();

return g_error;
}

