// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdio.h>
#include <stdlib.h>

#include "iothub_module_client_ll.h"
#include "iothubtransportmqtt.h"

const char* targetDevice = "edgeDevice1";
const char* targetModule = "deviceManagement";
const char* targetMethodName = "test-method";
const char* payload = "this-is-a-payload";


int main()
{
    int response;
    char* responsePayload;
    size_t responsePayloadSize;

    IOTHUB_MODULE_CLIENT_LL_HANDLE handle = IoTHubModuleClient_LL_CreateFromEnvironment(MQTT_Protocol);
    IoTHubModuleClient_LL_MethodInvoke(handle, targetDevice, targetModule, targetMethodName, payload, 3600, &response, &responsePayload, &responsePayloadSize);

}
