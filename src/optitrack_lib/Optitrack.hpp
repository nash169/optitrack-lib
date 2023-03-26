#ifndef OPTITRACKLIB_OPTITRACK_HPP
#define OPTITRACKLIB_OPTITRACK_HPP

#include <memory>
#include <string>
#include <vector>

#include <inttypes.h>
#include <termios.h>
#include <unistd.h>

#include <NatNet/NatNetCAPI.h>
#include <NatNet/NatNetClient.h>
#include <NatNet/NatNetTypes.h>

#include <Eigen/Core>
#include <unordered_map>

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
            int iResult = ConnectClient();
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

        void request()
        {
            // Retrieve Data Descriptions from Motive
            printf("\n\n[SampleClient] Requesting Data Descriptions...\n");
            sDataDescriptions* pDataDefs = NULL;
            int iResult = _client->GetDataDescriptionList(&pDataDefs);
            if (iResult != ErrorCode_OK || pDataDefs == NULL) {
                printf("[SampleClient] Unable to retrieve Data Descriptions.\n");
            }
            else {
                printf("[SampleClient] Received %d Data Descriptions:\n", pDataDefs->nDataDescriptions);
                for (int i = 0; i < pDataDefs->nDataDescriptions; i++) {
                    printf("Data Description # %d (type=%d)\n", i, pDataDefs->arrDataDescriptions[i].type);
                    if (pDataDefs->arrDataDescriptions[i].type == Descriptor_MarkerSet) {
                        // MarkerSet
                        sMarkerSetDescription* pMS = pDataDefs->arrDataDescriptions[i].Data.MarkerSetDescription;
                        printf("MarkerSet Name : %s\n", pMS->szName);
                        for (int i = 0; i < pMS->nMarkers; i++)
                            printf("%s\n", pMS->szMarkerNames[i]);
                    }
                    else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_RigidBody) {
                        // RigidBody
                        sRigidBodyDescription* pRB = pDataDefs->arrDataDescriptions[i].Data.RigidBodyDescription;
                        printf("RigidBody Name : %s\n", pRB->szName);
                        printf("RigidBody ID : %d\n", pRB->ID);
                        printf("RigidBody Parent ID : %d\n", pRB->parentID);
                        printf("Parent Offset : %3.2f,%3.2f,%3.2f\n", pRB->offsetx, pRB->offsety, pRB->offsetz);

                        if (pRB->MarkerPositions != NULL && pRB->MarkerRequiredLabels != NULL) {
                            for (int markerIdx = 0; markerIdx < pRB->nMarkers; ++markerIdx) {
                                const MarkerData& markerPosition = pRB->MarkerPositions[markerIdx];
                                const int markerRequiredLabel = pRB->MarkerRequiredLabels[markerIdx];

                                printf("\tMarker #%d:\n", markerIdx);
                                printf("\t\tPosition: %.2f, %.2f, %.2f\n", markerPosition[0], markerPosition[1], markerPosition[2]);

                                if (markerRequiredLabel != 0) {
                                    printf("\t\tRequired active label: %d\n", markerRequiredLabel);
                                }
                            }
                        }
                    }
                    else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Skeleton) {
                        // Skeleton
                        sSkeletonDescription* pSK = pDataDefs->arrDataDescriptions[i].Data.SkeletonDescription;
                        printf("Skeleton Name : %s\n", pSK->szName);
                        printf("Skeleton ID : %d\n", pSK->skeletonID);
                        printf("RigidBody (Bone) Count : %d\n", pSK->nRigidBodies);
                        for (int j = 0; j < pSK->nRigidBodies; j++) {
                            sRigidBodyDescription* pRB = &pSK->RigidBodies[j];
                            printf("  RigidBody Name : %s\n", pRB->szName);
                            printf("  RigidBody ID : %d\n", pRB->ID);
                            printf("  RigidBody Parent ID : %d\n", pRB->parentID);
                            printf("  Parent Offset : %3.2f,%3.2f,%3.2f\n", pRB->offsetx, pRB->offsety, pRB->offsetz);
                        }
                    }
                    else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_ForcePlate) {
                        // Force Plate
                        sForcePlateDescription* pFP = pDataDefs->arrDataDescriptions[i].Data.ForcePlateDescription;
                        printf("Force Plate ID : %d\n", pFP->ID);
                        printf("Force Plate Serial : %s\n", pFP->strSerialNo);
                        printf("Force Plate Width : %3.2f\n", pFP->fWidth);
                        printf("Force Plate Length : %3.2f\n", pFP->fLength);
                        printf("Force Plate Electrical Center Offset (%3.3f, %3.3f, %3.3f)\n", pFP->fOriginX, pFP->fOriginY, pFP->fOriginZ);
                        for (int iCorner = 0; iCorner < 4; iCorner++)
                            printf("Force Plate Corner %d : (%3.4f, %3.4f, %3.4f)\n", iCorner, pFP->fCorners[iCorner][0], pFP->fCorners[iCorner][1], pFP->fCorners[iCorner][2]);
                        printf("Force Plate Type : %d\n", pFP->iPlateType);
                        printf("Force Plate Data Type : %d\n", pFP->iChannelDataType);
                        printf("Force Plate Channel Count : %d\n", pFP->nChannels);
                        for (int iChannel = 0; iChannel < pFP->nChannels; iChannel++)
                            printf("\tChannel %d : %s\n", iChannel, pFP->szChannelNames[iChannel]);
                    }
                    else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Device) {
                        // Peripheral Device
                        sDeviceDescription* pDevice = pDataDefs->arrDataDescriptions[i].Data.DeviceDescription;
                        printf("Device Name : %s\n", pDevice->strName);
                        printf("Device Serial : %s\n", pDevice->strSerialNo);
                        printf("Device ID : %d\n", pDevice->ID);
                        printf("Device Channel Count : %d\n", pDevice->nChannels);
                        for (int iChannel = 0; iChannel < pDevice->nChannels; iChannel++)
                            printf("\tChannel %d : %s\n", iChannel, pDevice->szChannelNames[iChannel]);
                    }
                    else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Camera) {
                        // Camera
                        sCameraDescription* pCamera = pDataDefs->arrDataDescriptions[i].Data.CameraDescription;
                        printf("Camera Name : %s\n", pCamera->strName);
                        printf("Camera Position (%3.2f, %3.2f, %3.2f)\n", pCamera->x, pCamera->y, pCamera->z);
                        printf("Camera Orientation (%3.2f, %3.2f, %3.2f, %3.2f)\n", pCamera->qx, pCamera->qy, pCamera->qz, pCamera->qw);
                    }
                    else {
                        printf("Unknown data type.\n");
                        // Unknown
                    }
                }
            }
        }

        const Eigen::MatrixXd& rigidBodies() { return _rigidBodies; }

    protected:
        std::unique_ptr<NatNetClient> _client;
        sNatNetClientConnectParams _connectParams;
        char _discoveredMulticastGroupAddr[kNatNetIpv4AddrStrLenMax] = NATNET_DEFAULT_MULTICAST_ADDRESS;
        int _analogSamplesPerMocapFrame = 0;
        sServerDescription _serverDescription;

        Eigen::MatrixXd _rigidBodies;

        // DataHandler receives data from the server
        // This function is called by NatNet when a frame of mocap data is available
        void update(sFrameOfMocapData* data)
        {
            _rigidBodies.setZero(data->nRigidBodies, 7);

            for (int i = 0; i < data->nRigidBodies; i++) {
                // 0x01 : bool, rigid body was successfully tracked in this frame
                bool bTrackingValid = data->RigidBodies[i].params & 0x01;
                _rigidBodies(i, 0) = data->RigidBodies[i].x;
                _rigidBodies(i, 1) = data->RigidBodies[i].y;
                _rigidBodies(i, 2) = data->RigidBodies[i].z;
                _rigidBodies(i, 3) = data->RigidBodies[i].qx;
                _rigidBodies(i, 4) = data->RigidBodies[i].qy;
                _rigidBodies(i, 5) = data->RigidBodies[i].qz;
                _rigidBodies(i, 6) = data->RigidBodies[i].qw;
            }
        }

        // Establish a NatNet Client connection
        int ConnectClient()
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

        static void NATNET_CALLCONV dataHandler(sFrameOfMocapData* data, void* pUserData)
        {
            static_cast<Optitrack*>(pUserData)->update(data);
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
    };

} // namespace optitrack_lib

#endif // OPTITRACKLIB_OPTITRACK_HPP