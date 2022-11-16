#include "optitrack_lib/OptiTrackClient.hpp"
#include <cstdio>
#include <cstring>

namespace optitrack {

    OptiTrackClient::OptiTrackClient()
    {
        client_ = std::make_unique<NatNetClient>();
        client_->SetFrameReceivedCallback(OptiTrackClient::dataHandler, this);
    }

    OptiTrackClient::~OptiTrackClient()
    {
        disconnect();
        client_.reset();
    }

    int OptiTrackClient::connect(const std::string& sourceIP)
    {
        clientConnectParams_.connectionType = ConnectionType_Multicast;
        clientConnectParams_.serverAddress = sourceIP.c_str();

        // close any previous connection
        client_->Disconnect();

        int retCode = client_->Connect(clientConnectParams_);
        if (retCode != ErrorCode_OK) {
            printf("[connectToOptiTrack] Unable to connect to server. Error code: %d.\n", retCode);
            return ErrorCode_Internal;
        }

        void* pResult;
        int nBytes = 0;
        ErrorCode ret = ErrorCode_OK;

        // print server info
        memset(&serverDescription_, 0, sizeof(serverDescription_));
        ret = client_->GetServerDescription(&serverDescription_);
        if (ret != ErrorCode_OK || !serverDescription_.HostPresent) {
            printf("[connectToOptiTrack] Unable to connect to server. Host not present.\n");
            return 1;
        }

        printf("\n[connectToOptiTrack] Server application info:\n");
        printf("Application: %s (ver. %d.%d.%d.%d)\n",
            serverDescription_.szHostApp,
            serverDescription_.HostAppVersion[0],
            serverDescription_.HostAppVersion[1],
            serverDescription_.HostAppVersion[2],
            serverDescription_.HostAppVersion[3]);
        printf("NatNet Version: %d.%d.%d.%d\n", serverDescription_.NatNetVersion[0], serverDescription_.NatNetVersion[1],
            serverDescription_.NatNetVersion[2], serverDescription_.NatNetVersion[3]);
        printf("Client IP:%s\n", clientConnectParams_.localAddress);
        printf("Server IP:%s\n", clientConnectParams_.serverAddress);
        printf("Server Name:%s\n", serverDescription_.szHostComputerName);

        // get mocap frame rate
        ret = client_->SendMessageAndWait("FrameRate", &pResult, &nBytes);
        if (ret == ErrorCode_OK) {
            float fRate = *((float*)pResult);
            printf("Mocap Framerate : %3.2f\n", fRate);
        }
        else {
            printf("Error getting frame rate.\n");
        }

        return ErrorCode_OK;
    }

    bool OptiTrackClient::testConnection()
    {
        if (client_ == nullptr) {
            return false;
        }
        void* response;
        int nBytes;

        printf("[testConnection] Sending Test Request\n");
        int ret = client_->SendMessageAndWait("TestRequest", &response, &nBytes);
        if (ret != ErrorCode_OK) {
            printf("[testConnection] Unable to connect to server. Error code: %d.\n", ret);
            return false;
        }

        printf("[testConnection] Received: %s (%i bytes)\n", (char*)response, nBytes);
        return true;
    }

    bool OptiTrackClient::requestDataDescription()
    {
        if (client_ == nullptr) {
            return false;
        }

        sDataDescriptions* pDataDefs = nullptr;
        int ret = client_->GetDataDescriptionList(&pDataDefs);
        if (ret != ErrorCode_OK || pDataDefs == nullptr) {
            printf("[requestDataDescription] Unable to retrieve Data Descriptions.\n");
            return false;
        }

        printf("[requestDataDescription] Received %d Data Descriptions:\n", pDataDefs->nDataDescriptions);
        for (int i = 0; i < pDataDefs->nDataDescriptions; i++) {
            printf("Data Description # %d (type=%d)\n", i, pDataDefs->arrDataDescriptions[i].type);
            if (pDataDefs->arrDataDescriptions[i].type == Descriptor_MarkerSet) {
                // MarkerSet
                sMarkerSetDescription* pMS = pDataDefs->arrDataDescriptions[i].Data.MarkerSetDescription;
                printf("MarkerSet Name : %s\n", pMS->szName);
                for (int i = 0; i < pMS->nMarkers; i++) {
                    printf("%s\n", pMS->szMarkerNames[i]);
                }
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
                printf("Force Plate Electrical Center Offset (%3.3f, %3.3f, %3.3f)\n",
                    pFP->fOriginX,
                    pFP->fOriginY,
                    pFP->fOriginZ);
                for (int iCorner = 0; iCorner < 4; iCorner++) {
                    printf("Force Plate Corner %d : (%3.4f, %3.4f, %3.4f)\n",
                        iCorner,
                        pFP->fCorners[iCorner][0],
                        pFP->fCorners[iCorner][1],
                        pFP->fCorners[iCorner][2]);
                }
                printf("Force Plate Type : %d\n", pFP->iPlateType);
                printf("Force Plate Data Type : %d\n", pFP->iChannelDataType);
                printf("Force Plate Channel Count : %d\n", pFP->nChannels);
                for (int iChannel = 0; iChannel < pFP->nChannels; iChannel++) {
                    printf("\tChannel %d : %s\n", iChannel, pFP->szChannelNames[iChannel]);
                }
            }
            else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Device) {
                // Peripheral Device
                sDeviceDescription* pDevice = pDataDefs->arrDataDescriptions[i].Data.DeviceDescription;
                printf("Device Name : %s\n", pDevice->strName);
                printf("Device Serial : %s\n", pDevice->strSerialNo);
                printf("Device ID : %d\n", pDevice->ID);
                printf("Device Channel Count : %d\n", pDevice->nChannels);
                for (int iChannel = 0; iChannel < pDevice->nChannels; iChannel++) {
                    printf("\tChannel %d : %s\n", iChannel, pDevice->szChannelNames[iChannel]);
                }
            }
            else {
                printf("Unknown data type.");
                // Unknown
            }
        }

        return true;
    }

    void OptiTrackClient::setCallback(const DataHandlerCallback& callback)
    {
        callback_ = callback;
    }

    void OptiTrackClient::evaluateCallback(sFrameOfMocapData* data)
    {
        if (callback_ != nullptr) {
            callback_(data);
        }
    }

    void NATNET_CALLCONV OptiTrackClient::dataHandler(sFrameOfMocapData* data, void* context)
    {
        static_cast<OptiTrackClient*>(context)->evaluateCallback(data);
    }

    void OptiTrackClient::disconnect()
    {
        client_->Disconnect();
    }

} // namespace optitrack