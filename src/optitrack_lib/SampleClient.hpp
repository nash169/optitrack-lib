#ifndef OPTITRACKLIB_SAMPLECLIENT_HPP
#define OPTITRACKLIB_SAMPLECLIENT_HPP

#include <memory>
#include <string>
#include <vector>

#include <inttypes.h>
#include <termios.h>
#include <unistd.h>

#include <NatNet/NatNetCAPI.h>
#include <NatNet/NatNetClient.h>
#include <NatNet/NatNetTypes.h>

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

        bool run()
        {
            void* response;
            int nBytes, iResult;
            printf("\nClient is connected to server and listening for data...\n");
            bool bExit = false;
            while (const int c = getch()) {
                switch (c) {
                case 'q':
                    bExit = true;
                    break;
                case 'r':
                    resetClient();
                    break;
                case 'p':
                    sServerDescription ServerDescription;
                    memset(&ServerDescription, 0, sizeof(ServerDescription));
                    _client->GetServerDescription(&ServerDescription);
                    if (!ServerDescription.HostPresent) {
                        printf("Unable to connect to server. Host not present. Exiting.");
                        return 1;
                    }
                    break;
                case 's': {
                    printf("\n\n[SampleClient] Requesting Data Descriptions...");
                    sDataDescriptions* pDataDefs = NULL;
                    iResult = _client->GetDataDescriptionList(&pDataDefs);
                    if (iResult != ErrorCode_OK || pDataDefs == NULL) {
                        printf("[SampleClient] Unable to retrieve Data Descriptions.");
                    }
                    else {
                        printf("[SampleClient] Received %d Data Descriptions:\n", pDataDefs->nDataDescriptions);
                    }
                } break;
                case 'm': // change to multicast
                    _connectParams.connectionType = ConnectionType_Multicast;
                    iResult = ConnectClient();
                    if (iResult == ErrorCode_OK)
                        printf("Client connection type changed to Multicast.\n\n");
                    else
                        printf("Error changing client connection type to Multicast.\n\n");
                    break;
                case 'u': // change to unicast
                    _connectParams.connectionType = ConnectionType_Unicast;
                    iResult = ConnectClient();
                    if (iResult == ErrorCode_OK)
                        printf("Client connection type changed to Unicast.\n\n");
                    else
                        printf("Error changing client connection type to Unicast.\n\n");
                    break;
                case 'c': // connect
                    iResult = ConnectClient();
                    break;
                case 'd': // disconnect
                    // note: applies to unicast connections only - indicates to Motive to stop sending packets to that client endpoint
                    iResult = _client->SendMessageAndWait("Disconnect", &response, &nBytes);
                    if (iResult == ErrorCode_OK)
                        printf("[SampleClient] Disconnected");
                    break;
                default:
                    break;
                }
                if (bExit)
                    break;
            }
        }

    protected:
        std::unique_ptr<NatNetClient> _client;
        sNatNetClientConnectParams _connectParams;
        char _discoveredMulticastGroupAddr[kNatNetIpv4AddrStrLenMax] = NATNET_DEFAULT_MULTICAST_ADDRESS;
        int _analogSamplesPerMocapFrame = 0;
        sServerDescription _serverDescription;

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

        // DataHandler receives data from the server
        // This function is called by NatNet when a frame of mocap data is available
        void update(sFrameOfMocapData* data)
        {
            // Software latency here is defined as the span of time between:
            //   a) The reception of a complete group of 2D frames from the camera system (CameraDataReceivedTimestamp)
            // and
            //   b) The time immediately prior to the NatNet frame being transmitted over the network (TransmitTimestamp)
            //
            // This figure may appear slightly higher than the "software latency" reported in the Motive user interface,
            // because it additionally includes the time spent preparing to stream the data via NatNet.
            const uint64_t softwareLatencyHostTicks = data->TransmitTimestamp - data->CameraDataReceivedTimestamp;
            const double softwareLatencyMillisec = (softwareLatencyHostTicks * 1000) / static_cast<double>(_serverDescription.HighResClockFrequency);

            // Transit latency is defined as the span of time between Motive transmitting the frame of data, and its reception by the client (now).
            // The SecondsSinceHostTimestamp method relies on NatNetClient's internal clock synchronization with the server using Cristian's algorithm.
            const double transitLatencyMillisec = _client->SecondsSinceHostTimestamp(data->TransmitTimestamp) * 1000.0;

            // if (g_outputFile) {
            //     _WriteFrame(g_outputFile, data);
            // }

            int i = 0;

            printf("FrameID : %d\n", data->iFrame);
            printf("Timestamp : %3.2lf\n", data->fTimestamp);
            printf("Software latency : %.2lf milliseconds\n", softwareLatencyMillisec);

            // Only recent versions of the Motive software in combination with ethernet camera systems support system latency measurement.
            // If it's unavailable (for example, with USB camera systems, or during playback), this field will be zero.
            const bool bSystemLatencyAvailable = data->CameraMidExposureTimestamp != 0;

            if (bSystemLatencyAvailable) {
                // System latency here is defined as the span of time between:
                //   a) The midpoint of the camera exposure window, and therefore the average age of the photons (CameraMidExposureTimestamp)
                // and
                //   b) The time immediately prior to the NatNet frame being transmitted over the network (TransmitTimestamp)
                const uint64_t systemLatencyHostTicks = data->TransmitTimestamp - data->CameraMidExposureTimestamp;
                const double systemLatencyMillisec = (systemLatencyHostTicks * 1000) / static_cast<double>(_serverDescription.HighResClockFrequency);

                // Client latency is defined as the sum of system latency and the transit time taken to relay the data to the NatNet client.
                // This is the all-inclusive measurement (photons to client processing).
                const double clientLatencyMillisec = _client->SecondsSinceHostTimestamp(data->CameraMidExposureTimestamp) * 1000.0;

                // You could equivalently do the following (not accounting for time elapsed since we calculated transit latency above):
                // const double clientLatencyMillisec = systemLatencyMillisec + transitLatencyMillisec;

                printf("System latency : %.2lf milliseconds\n", systemLatencyMillisec);
                printf("Total client latency : %.2lf milliseconds (transit time +%.2lf ms)\n", clientLatencyMillisec, transitLatencyMillisec);
            }
            else {
                printf("Transit latency : %.2lf milliseconds\n", transitLatencyMillisec);
            }

            // FrameOfMocapData params
            bool bIsRecording = ((data->params & 0x01) != 0);
            bool bTrackedModelsChanged = ((data->params & 0x02) != 0);
            if (bIsRecording)
                printf("RECORDING\n");
            if (bTrackedModelsChanged)
                printf("Models Changed.\n");

            // timecode - for systems with an eSync and SMPTE timecode generator - decode to values
            int hour, minute, second, frame, subframe;
            NatNet_DecodeTimecode(data->Timecode, data->TimecodeSubframe, &hour, &minute, &second, &frame, &subframe);
            // decode to friendly string
            char szTimecode[128] = "";
            NatNet_TimecodeStringify(data->Timecode, data->TimecodeSubframe, szTimecode, 128);
            printf("Timecode : %s\n", szTimecode);

            // Rigid Bodies
            printf("Rigid Bodies [Count=%d]\n", data->nRigidBodies);
            for (i = 0; i < data->nRigidBodies; i++) {
                // params
                // 0x01 : bool, rigid body was successfully tracked in this frame
                bool bTrackingValid = data->RigidBodies[i].params & 0x01;

                printf("Rigid Body [ID=%d  Error=%3.2f  Valid=%d]\n", data->RigidBodies[i].ID, data->RigidBodies[i].MeanError, bTrackingValid);
                printf("\tx\ty\tz\tqx\tqy\tqz\tqw\n");
                printf("\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\n",
                    data->RigidBodies[i].x,
                    data->RigidBodies[i].y,
                    data->RigidBodies[i].z,
                    data->RigidBodies[i].qx,
                    data->RigidBodies[i].qy,
                    data->RigidBodies[i].qz,
                    data->RigidBodies[i].qw);
            }

            // Skeletons
            printf("Skeletons [Count=%d]\n", data->nSkeletons);
            for (i = 0; i < data->nSkeletons; i++) {
                sSkeletonData skData = data->Skeletons[i];
                printf("Skeleton [ID=%d  Bone count=%d]\n", skData.skeletonID, skData.nRigidBodies);
                for (int j = 0; j < skData.nRigidBodies; j++) {
                    sRigidBodyData rbData = skData.RigidBodyData[j];
                    printf("Bone %d\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\n",
                        rbData.ID, rbData.x, rbData.y, rbData.z, rbData.qx, rbData.qy, rbData.qz, rbData.qw);
                }
            }

            // labeled markers - this includes all markers (Active, Passive, and 'unlabeled' (markers with no asset but a PointCloud ID)
            bool bOccluded; // marker was not visible (occluded) in this frame
            bool bPCSolved; // reported position provided by point cloud solve
            bool bModelSolved; // reported position provided by model solve
            bool bHasModel; // marker has an associated asset in the data stream
            bool bUnlabeled; // marker is 'unlabeled', but has a point cloud ID that matches Motive PointCloud ID (In Motive 3D View)
            bool bActiveMarker; // marker is an actively labeled LED marker

            printf("Markers [Count=%d]\n", data->nLabeledMarkers);
            for (i = 0; i < data->nLabeledMarkers; i++) {
                bOccluded = ((data->LabeledMarkers[i].params & 0x01) != 0);
                bPCSolved = ((data->LabeledMarkers[i].params & 0x02) != 0);
                bModelSolved = ((data->LabeledMarkers[i].params & 0x04) != 0);
                bHasModel = ((data->LabeledMarkers[i].params & 0x08) != 0);
                bUnlabeled = ((data->LabeledMarkers[i].params & 0x10) != 0);
                bActiveMarker = ((data->LabeledMarkers[i].params & 0x20) != 0);

                sMarker marker = data->LabeledMarkers[i];

                // Marker ID Scheme:
                // Active Markers:
                //   ID = ActiveID, correlates to RB ActiveLabels list
                // Passive Markers:
                //   If Asset with Legacy Labels
                //      AssetID 	(Hi Word)
                //      MemberID	(Lo Word)
                //   Else
                //      PointCloud ID
                int modelID, markerID;
                NatNet_DecodeID(marker.ID, &modelID, &markerID);

                char szMarkerType[512];
                if (bActiveMarker)
                    strcpy(szMarkerType, "Active");
                else if (bUnlabeled)
                    strcpy(szMarkerType, "Unlabeled");
                else
                    strcpy(szMarkerType, "Labeled");

                printf("%s Marker [ModelID=%d, MarkerID=%d] [size=%3.2f] [pos=%3.2f,%3.2f,%3.2f]\n",
                    szMarkerType, modelID, markerID, marker.size, marker.x, marker.y, marker.z);
            }

            // force plates
            printf("Force Plate [Count=%d]\n", data->nForcePlates);
            for (int iPlate = 0; iPlate < data->nForcePlates; iPlate++) {
                printf("Force Plate %d\n", data->ForcePlates[iPlate].ID);
                for (int iChannel = 0; iChannel < data->ForcePlates[iPlate].nChannels; iChannel++) {
                    printf("\tChannel %d:\t", iChannel);
                    if (data->ForcePlates[iPlate].ChannelData[iChannel].nFrames == 0) {
                        printf("\tEmpty Frame\n");
                    }
                    else if (data->ForcePlates[iPlate].ChannelData[iChannel].nFrames != _analogSamplesPerMocapFrame) {
                        printf("\tPartial Frame [Expected:%d   Actual:%d]\n", _analogSamplesPerMocapFrame, data->ForcePlates[iPlate].ChannelData[iChannel].nFrames);
                    }
                    for (int iSample = 0; iSample < data->ForcePlates[iPlate].ChannelData[iChannel].nFrames; iSample++)
                        printf("%3.2f\t", data->ForcePlates[iPlate].ChannelData[iChannel].Values[iSample]);
                    printf("\n");
                }
            }

            // devices
            printf("Device [Count=%d]\n", data->nDevices);
            for (int iDevice = 0; iDevice < data->nDevices; iDevice++) {
                printf("Device %d\n", data->Devices[iDevice].ID);
                for (int iChannel = 0; iChannel < data->Devices[iDevice].nChannels; iChannel++) {
                    printf("\tChannel %d:\t", iChannel);
                    if (data->Devices[iDevice].ChannelData[iChannel].nFrames == 0) {
                        printf("\tEmpty Frame\n");
                    }
                    else if (data->Devices[iDevice].ChannelData[iChannel].nFrames != _analogSamplesPerMocapFrame) {
                        printf("\tPartial Frame [Expected:%d   Actual:%d]\n", _analogSamplesPerMocapFrame, data->Devices[iDevice].ChannelData[iChannel].nFrames);
                    }
                    for (int iSample = 0; iSample < data->Devices[iDevice].ChannelData[iChannel].nFrames; iSample++)
                        printf("%3.2f\t", data->Devices[iDevice].ChannelData[iChannel].Values[iSample]);
                    printf("\n");
                }
            }
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

#endif // OPTITRACKLIB_SAMPLECLIENT_HPP