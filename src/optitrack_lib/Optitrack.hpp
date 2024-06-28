#ifndef OPTITRACKLIB_OPTITRACK_HPP
#define OPTITRACKLIB_OPTITRACK_HPP

#include <map>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <memory>
#include <chrono>

#include <inttypes.h>
#include <termios.h>
#include <unistd.h>

#include <NatNet/NatNetCAPI.h>
#include <NatNet/NatNetClient.h>
#include <NatNet/NatNetTypes.h>

#include <Eigen/Core>
#include <unordered_map>

using namespace std::chrono_literals;

namespace optitrack_lib {
    std::vector<sNatNetDiscoveredServer> discoveredServers;

    void NATNET_CALLCONV ServerDiscoveredCallback(const sNatNetDiscoveredServer* pDiscoveredServer, void* pUserContext)
    {
        char serverHotkey = '.';
        if (discoveredServers.size() < 9) {
            serverHotkey = static_cast<char>('1' + discoveredServers.size());
        }

        printf("[%c] %s %d.%d at %s ",
            serverHotkey,
            pDiscoveredServer->serverDescription.szHostApp,
            pDiscoveredServer->serverDescription.HostAppVersion[0],
            pDiscoveredServer->serverDescription.HostAppVersion[1],
            pDiscoveredServer->serverAddress);

        if (pDiscoveredServer->serverDescription.bConnectionInfoValid) {
            printf("(%s)\n", pDiscoveredServer->serverDescription.ConnectionMulticast ? "multicast" : "unicast");
        }
        else {
            printf("(WARNING: Legacy server, could not autodetect settings. Auto-connect may not work reliably.)\n");
        }

        discoveredServers.push_back(*pDiscoveredServer);
    }

    struct MocapFrameWrapper
    {
        std::shared_ptr<sFrameOfMocapData> data;
        double transitLatencyMillisec;
        double clientLatencyMillisec;
    };

    class Optitrack {
    public:
        Optitrack(const std::string& address = "")
        {
            // print version info
            unsigned char ver[4];
            NatNet_GetVersion(ver);
            printf("NatNet Sample Client (NatNet ver. %d.%d.%d.%d)\n", ver[0], ver[1], ver[2], ver[3]);

            // Install logging callback
            NatNet_SetLogCallback(MessageHandler);

            _client = std::make_unique<NatNetClient>();

            // set the frame callback handler
            _client->SetFrameReceivedCallback(dataHandler, this);
        }

        virtual ~Optitrack()
        {
            _client->Disconnect();
        }

        bool connect(const std::string& server = "", const std::string& local = "")
        {
            if (server.empty()) {
                NatNetDiscoveryHandle discovery;
                NatNet_CreateAsyncServerDiscovery(&discovery, ServerDiscoveredCallback);

                while (const int c = getch()) {
                    if (c >= '1' && c <= '9') {
                        const size_t serverIndex = c - '1';
                        if (serverIndex < discoveredServers.size()) {
                            const sNatNetDiscoveredServer& discoveredServer = discoveredServers[serverIndex];

                            if (discoveredServer.serverDescription.bConnectionInfoValid) {
                                // Build the connection parameters.
                                snprintf(
                                    _discoveredMulticastGroupAddr, sizeof _discoveredMulticastGroupAddr,
                                    "%" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 "",
                                    discoveredServer.serverDescription.ConnectionMulticastAddress[0],
                                    discoveredServer.serverDescription.ConnectionMulticastAddress[1],
                                    discoveredServer.serverDescription.ConnectionMulticastAddress[2],
                                    discoveredServer.serverDescription.ConnectionMulticastAddress[3]);

                                _connectParams.connectionType = discoveredServer.serverDescription.ConnectionMulticast ? ConnectionType_Multicast : ConnectionType_Unicast;
                                _connectParams.serverCommandPort = discoveredServer.serverCommandPort;
                                _connectParams.serverDataPort = discoveredServer.serverDescription.ConnectionDataPort;
                                _connectParams.serverAddress = discoveredServer.serverAddress;
                                _connectParams.localAddress = discoveredServer.localAddress;
                                _connectParams.multicastAddress = _discoveredMulticastGroupAddr;
                            }
                            else {
                                // We're missing some info because it's a legacy server.
                                // Guess the defaults and make a best effort attempt to connect.
                                _connectParams.connectionType = ConnectionType_Multicast;
                                _connectParams.serverCommandPort = discoveredServer.serverCommandPort;
                                _connectParams.serverDataPort = 0;
                                _connectParams.serverAddress = discoveredServer.serverAddress;
                                _connectParams.localAddress = discoveredServer.localAddress;
                                _connectParams.multicastAddress = NULL;
                            }

                            break;
                        }
                    }
                    else if (c == 'q') {
                        return false;
                    }
                }

                NatNet_FreeAsyncServerDiscovery(discovery);
            }
            else {
                _connectParams.connectionType = ConnectionType_Multicast;
                _connectParams.serverAddress = server.c_str();

                if (!local.empty())
                    _connectParams.localAddress = local.c_str();
            }

            // Connect to Motive
            int iResult = connectClient();
            if (iResult != ErrorCode_OK) {
                printf("Error initializing client. See log for details. Exiting.\n");
                return false;
            }
            else
                printf("Client initialized and ready.\n");

            // Send/receive test request
            void* response;
            int nBytes;
            printf("[SampleClient] Sending Test Request\n");
            iResult = _client->SendMessageAndWait("TestRequest", &response, &nBytes);
            if (iResult == ErrorCode_OK)
                printf("[SampleClient] Received: %s\n", (char*)response);

            return true;
        }

        // const Eigen::MatrixXd& rigidBodies() { return _rigidBodies; }

        Eigen::Matrix<double, 7, 1> rigidBody(const std::string& bodyName)
        {
            return _rigidBodies[bodyName];
        }

        void updateData()
        {
            std::deque<MocapFrameWrapper> displayQueue;
            if (_networkQueueMutex.try_lock_for(std::chrono::milliseconds(5)))
            {
                for (MocapFrameWrapper f : _networkQueue)
                {
                    displayQueue.push_back(f);
                }

                // Release all frames in network queue
                _networkQueue.clear();
                _networkQueueMutex.unlock();
            }
            
            for (MocapFrameWrapper f : displayQueue)
            {
                sFrameOfMocapData* data = f.data.get();
                _rigidBodies.clear();

                // printf("Rigid Bodies [Count=%d]\n", data->nRigidBodies);
                for (int i = 0; i < data->nRigidBodies; i++)
                {
                    

                    int streamingID = data->RigidBodies[i].ID;
                    Eigen::Matrix<double, 7, 1> pose = (Eigen::Matrix<double, 7, 1>() << data->RigidBodies[i].x, data->RigidBodies[i].y, data->RigidBodies[i].z, data->RigidBodies[i].qx, data->RigidBodies[i].qy, data->RigidBodies[i].qz, data->RigidBodies[i].qw).finished();

                    std::pair<std::map<std::string, Eigen::Matrix<double, 7, 1>>::iterator, bool> insertResult;
                    insertResult = _rigidBodies.insert(std::pair<std::string, Eigen::Matrix<double, 7, 1>>(_assetIDtoAssetName[streamingID], pose));

                    // // params
                    // // 0x01 : bool, rigid body was successfully tracked in this frame
                    // bool bTrackingValid = data->RigidBodies[i].params & 0x01;
                    // // int streamingID = data->RigidBodies[i].ID;
                    // printf("%s [ID=%d  Error(mm)=%.5f  Tracked=%d]\n", _assetIDtoAssetName[streamingID].c_str(), streamingID, data->RigidBodies[i].MeanError*1000.0f, bTrackingValid);
                    // printf("\tx\ty\tz\tqx\tqy\tqz\tqw\n");
                    // printf("\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\n",
                    //     data->RigidBodies[i].x,
                    //     data->RigidBodies[i].y,
                    //     data->RigidBodies[i].z,
                    //     data->RigidBodies[i].qx,
                    //     data->RigidBodies[i].qy,
                    //     data->RigidBodies[i].qz,
                    //     data->RigidBodies[i].qw);
                    // std::this_thread::sleep_for(5ms);
                }
            }
        }

        bool updateDataDescriptions()
        {
            // release memory allocated by previous in previous GetDataDescriptionList()
            if (_descriptionFrame)
                NatNet_FreeDescriptions(_descriptionFrame);

            // Retrieve Data Descriptions from Motive
            int iResult = _client->GetDataDescriptionList(&_descriptionFrame);
            if (iResult != ErrorCode_OK || _descriptionFrame == NULL)
                return false;

            updateDataToDescriptionMaps(_descriptionFrame);

            return true;
        }

    protected:
        std::unique_ptr<NatNetClient> _client;
        sNatNetClientConnectParams _connectParams;
        char _discoveredMulticastGroupAddr[kNatNetIpv4AddrStrLenMax] = NATNET_DEFAULT_MULTICAST_ADDRESS;
        int _analogSamplesPerMocapFrame = 0;
        sServerDescription _serverDescription;
        
        std::map<std::string, Eigen::Matrix<double,7,1>> _rigidBodies;
        // Eigen::MatrixXd _rigidBodies;

        // // DataHandler receives data from the server
        // // This function is called by NatNet when a frame of mocap data is available
        // void update(sFrameOfMocapData* data)
        // {
        //     _rigidBodies.setZero(data->nRigidBodies, 7);

        //     for (int i = 0; i < data->nRigidBodies; i++) {
        //         // 0x01 : bool, rigid body was successfully tracked in this frame
        //         bool bTrackingValid = data->RigidBodies[i].params & 0x01;
        //         _rigidBodies(i, 0) = data->RigidBodies[i].x;
        //         _rigidBodies(i, 1) = data->RigidBodies[i].y;
        //         _rigidBodies(i, 2) = data->RigidBodies[i].z;
        //         _rigidBodies(i, 3) = data->RigidBodies[i].qx;
        //         _rigidBodies(i, 4) = data->RigidBodies[i].qy;
        //         _rigidBodies(i, 5) = data->RigidBodies[i].qz;
        //         _rigidBodies(i, 6) = data->RigidBodies[i].qw;
        //     }
        // }

        // Establish a NatNet Client connection
        int connectClient()
        {
            // Release previous server
            _client->Disconnect();

            // Init Client and connect to NatNet server
            int retCode = _client->Connect(_connectParams);
            if (retCode != ErrorCode_OK) {
                printf("Unable to connect to server.  Error code: %d. Exiting.\n", retCode);
                return ErrorCode_Internal;
            }
            else {
                // connection succeeded

                void* pResult;
                int nBytes = 0;
                ErrorCode ret = ErrorCode_OK;

                // print server info
                memset(&_serverDescription, 0, sizeof(_serverDescription));
                ret = _client->GetServerDescription(&_serverDescription);
                if (ret != ErrorCode_OK || !_serverDescription.HostPresent) {
                    printf("Unable to connect to server. Host not present. Exiting.\n");
                    return 1;
                }
                printf("\n[SampleClient] Server application info:\n");
                printf("Application: %s (ver. %d.%d.%d.%d)\n", _serverDescription.szHostApp, _serverDescription.HostAppVersion[0],
                    _serverDescription.HostAppVersion[1], _serverDescription.HostAppVersion[2], _serverDescription.HostAppVersion[3]);
                printf("NatNet Version: %d.%d.%d.%d\n", _serverDescription.NatNetVersion[0], _serverDescription.NatNetVersion[1],
                    _serverDescription.NatNetVersion[2], _serverDescription.NatNetVersion[3]);
                printf("Client IP:%s\n", _connectParams.localAddress);
                printf("Server IP:%s\n", _connectParams.serverAddress);
                printf("Server Name:%s\n", _serverDescription.szHostComputerName);

                // get mocap frame rate
                ret = _client->SendMessageAndWait("FrameRate", &pResult, &nBytes);
                if (ret == ErrorCode_OK) {
                    float fRate = *((float*)pResult);
                    printf("Mocap Framerate : %3.2f\n", fRate);
                }
                else
                    printf("Error getting frame rate.\n");

                // get # of analog samples per mocap frame of data
                ret = _client->SendMessageAndWait("AnalogSamplesPerMocapFrame", &pResult, &nBytes);
                if (ret == ErrorCode_OK) {
                    _analogSamplesPerMocapFrame = *((int*)pResult);
                    printf("Analog Samples Per Mocap Frame : %d\n", _analogSamplesPerMocapFrame);
                }
                else
                    printf("Error getting Analog frame rate.\n");
            }

            return ErrorCode_OK;
        }

        void storeFrames(sFrameOfMocapData* data)
        {
            if (!_client)
                return;
   
            std::shared_ptr<sFrameOfMocapData> pDataCopy = std::make_shared<sFrameOfMocapData>();
            NatNet_CopyFrame(data, pDataCopy.get());

            MocapFrameWrapper f;
            f.data = pDataCopy;
            f.clientLatencyMillisec = _client->SecondsSinceHostTimestamp(data->CameraMidExposureTimestamp) * 1000.0;
            f.transitLatencyMillisec = _client->SecondsSinceHostTimestamp(data->TransmitTimestamp) * 1000.0;

            if (_networkQueueMutex.try_lock_for(std::chrono::milliseconds(5)))
            {
                _networkQueue.push_back(f);

                // Maintain a cap on the queue size, removing oldest as necessary
                while ((int)_networkQueue.size() > kMaxQueueSize)
                {
                    f = _networkQueue.front();
                    NatNet_FreeFrame(f.data.get());
                    _networkQueue.pop_front();
                }
                _networkQueueMutex.unlock();
            }
            else
            {
                // Unable to lock the frame queue and we chose not to wait - drop the frame and notify
                NatNet_FreeFrame(pDataCopy.get());
                printf("\nFrame dropped (Frame : %d)\n", f.data->iFrame);
            }
        }

        static void NATNET_CALLCONV dataHandler(sFrameOfMocapData* data, void* pUserData)
        {
            // static_cast<Optitrack*>(pUserData)->update(data);
            static_cast<Optitrack*>(pUserData)->storeFrames(data);
        }

        // MessageHandler receives NatNet error/debug messages
        static void NATNET_CALLCONV MessageHandler(Verbosity msgType, const char* msg)
        {
            // Optional: Filter out debug messages
            if (msgType < Verbosity_Info) {
                return;
            }

            printf("\n[NatNetLib]");

            switch (msgType) {
            case Verbosity_Debug:
                printf(" [DEBUG]");
                break;
            case Verbosity_Info:
                printf("  [INFO]");
                break;
            case Verbosity_Warning:
                printf("  [WARN]");
                break;
            case Verbosity_Error:
                printf(" [ERROR]");
                break;
            default:
                printf(" [?????]");
                break;
            }

            printf(": %s\n", msg);
        }

        void resetClient()
        {
            int iSuccess;

            printf("\n\nre-setting Client\n\n.");

            iSuccess = _client->Disconnect();
            if (iSuccess != 0)
                printf("error un-initting Client\n");

            iSuccess = _client->Connect(_connectParams);
            if (iSuccess != 0)
                printf("error re-initting Client\n");
        }

        char getch()
        {
            char buf = 0;
            termios old = {0};

            fflush(stdout);

            if (tcgetattr(0, &old) < 0)
                perror("tcsetattr()");

            old.c_lflag &= ~ICANON;
            old.c_lflag &= ~ECHO;
            old.c_cc[VMIN] = 1;
            old.c_cc[VTIME] = 0;

            if (tcsetattr(0, TCSANOW, &old) < 0)
                perror("tcsetattr ICANON");

            if (read(0, &buf, 1) < 0)
                perror("read()");

            old.c_lflag |= ICANON;
            old.c_lflag |= ECHO;

            if (tcsetattr(0, TCSADRAIN, &old) < 0)
                perror("tcsetattr ~ICANON");

            // printf( "%c\n", buf );

            return buf;
        }

        void updateDataToDescriptionMaps(sDataDescriptions* descriptionFrame)
        {
            _assetIDtoAssetDescriptionOrder.clear();
            _assetIDtoAssetName.clear();
            int assetID = 0, index = 0, cameraIndex = 0;
            std::string assetName = "";

            if (descriptionFrame == nullptr || descriptionFrame->nDataDescriptions <= 0)
                return;

            for (int i = 0; i < descriptionFrame->nDataDescriptions; i++)
            {
                assetID = -1;
                assetName = "";

                if (descriptionFrame->arrDataDescriptions[i].type == Descriptor_RigidBody)
                {
                    sRigidBodyDescription* pRB = descriptionFrame->arrDataDescriptions[i].Data.RigidBodyDescription;
                    assetID = pRB->ID;
                    assetName = std::string(pRB->szName);
                }
                else if (descriptionFrame->arrDataDescriptions[i].type == Descriptor_Skeleton)
                {
                    sSkeletonDescription* pSK = descriptionFrame->arrDataDescriptions[i].Data.SkeletonDescription;
                    assetID = pSK->skeletonID;
                    assetName = std::string(pSK->szName);
                }
                else if (descriptionFrame->arrDataDescriptions[i].type == Descriptor_MarkerSet)
                {
                    // Skip markersets for now as they dont have unique id's, but do increase the index
                    // as they are in the data packet
                    index++;
                    continue;
                }
                else if (descriptionFrame->arrDataDescriptions[i].type == Descriptor_ForcePlate)
                {
                    sForcePlateDescription* pDesc = descriptionFrame->arrDataDescriptions[i].Data.ForcePlateDescription;
                    assetID = pDesc->ID;
                    assetName = pDesc->strSerialNo;
                }
                else if (descriptionFrame->arrDataDescriptions[i].type == Descriptor_Device)
                {
                    sDeviceDescription* pDesc = descriptionFrame->arrDataDescriptions[i].Data.DeviceDescription;
                    assetID = pDesc->ID;
                    assetName = std::string(pDesc->strName);
                }
                else if (descriptionFrame->arrDataDescriptions[i].type == Descriptor_Camera)
                {
                    // skip cameras as they are not in the data packet
                    continue;
                }
                else if (descriptionFrame->arrDataDescriptions[i].type == Descriptor_Asset)
                {
                    sAssetDescription* pDesc = descriptionFrame->arrDataDescriptions[i].Data.AssetDescription;
                    assetID = pDesc->AssetID;
                    assetName = std::string(pDesc->szName);
                }

                // Add to Asset ID to Asset Name map
                if (assetID == -1)
                    printf("\n[SampleClient] Warning : Unknown data type in description list : %d\n", descriptionFrame->arrDataDescriptions[i].type);
                else 
                {
                    std::pair<std::map<int, std::string>::iterator, bool> insertResult;
                    insertResult = _assetIDtoAssetName.insert(std::pair<int,std::string>(assetID, assetName));
                    if (insertResult.second == false)
                        printf("\n[SampleClient] Warning : Duplicate asset ID already in Name map (Existing:%d,%s\tNew:%d,%s\n)",
                            insertResult.first->first, insertResult.first->second.c_str(), assetID, assetName.c_str());
                }

                // Add to Asset ID to Asset Description Order map
                if (assetID != -1)
                {
                    std::pair<std::map<int, int>::iterator, bool> insertResult;
                    insertResult = _assetIDtoAssetDescriptionOrder.insert(std::pair<int, int>(assetID, index++));
                    if (insertResult.second == false)
                        printf("\n[SampleClient] Warning : Duplicate asset ID already in Order map (ID:%d\tOrder:%d\n)", insertResult.first->first, insertResult.first->second);
                }
            }
        }

        sDataDescriptions* _descriptionFrame;
        std::map<int, int> _assetIDtoAssetDescriptionOrder;
        std::map<int, std::string> _assetIDtoAssetName;
        bool _updatedDataDescriptions = false, _needUpdatedDataDescriptions = true;

        
        std::timed_mutex _networkQueueMutex;
        std::deque<MocapFrameWrapper> _networkQueue;
        const int kMaxQueueSize = 1;

        // std::timed_mutex _networkQueueMutex;
        // std::deque<MocapFrameWrapper> _networkQueue;
        // const int kMaxQueueSize = 500;

        // sDataDescriptions* g_descriptionFrame = NULL;
        // map<int, int> _assetIDtoAssetDescriptionOrder;
        // map<int, string> _assetIDtoAssetName;
        // bool gUpdatedDataDescriptions = false;
        // bool gNeedUpdatedDataDescriptions = true;
    };

} // namespace optitrack_lib

#endif // OPTITRACKLIB_OPTITRACK_HPP