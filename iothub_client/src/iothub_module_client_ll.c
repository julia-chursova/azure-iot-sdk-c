// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h>
#include <string.h>
#include "azure_c_shared_utility/optimize_size.h"
#include "azure_c_shared_utility/gballoc.h"
#include "azure_c_shared_utility/string_tokenizer.h"
#include "azure_c_shared_utility/doublylinkedlist.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/tickcounter.h"
#include "azure_c_shared_utility/constbuffer.h"
#include "azure_c_shared_utility/platform.h"
#include "azure_c_shared_utility/envvariable.h"

#include "internal/iothub_module_client_ll_method.h"
#include "iothub_client_core_ll.h"
#include "iothub_client_authorization.h"
#include "iothub_module_client_ll.h"
#include "iothub_transport_ll.h"
#include "iothub_client_private.h"
#include "iothub_client_options.h"
#include "iothub_client_version.h"
#include "iothub_client_diagnostic.h"
#include "azure_prov_client/iothub_security_factory.h"
#include <stdint.h>

#ifndef DONT_USE_UPLOADTOBLOB
#include "iothub_client_ll_uploadtoblob.h"
#endif

static const char* ENVIRONMENT_VAR_EDGEHUBCONNECTIONSTRING = "EdgeHubConnectionString";
static const char* ENVIRONMENT_VAR_EDGEAUTHSCHEME = "IOTEDGE_AUTHSCHEME";
static const char* ENVIRONMENT_VAR_EDGEDEVICEID = "IOTEDGE_DEVICEID";
static const char* ENVIRONMENT_VAR_EDGEMODULEID = "IOTEDGE_MODULEID";
static const char* ENVIRONMENT_VAR_EDGEHUBHOSTNAME = "IOTEDGE_IOTHUBHOSTNAME";
static const char* ENVIRONMENT_VAR_EDGEGATEWAYHOST = "IOTEDGE_GATEWAYHOSTNAME";

static const char* SAS_TOKEN_AUTH = "SasToken";

typedef struct IOTHUB_MODULE_CLIENT_LL_HANDLE_DATA_TAG
{
    IOTHUB_CLIENT_CORE_LL_HANDLE coreHandle;
    IOTHUB_MODULE_CLIENT_METHOD_HANDLE methodHandle;
} IOTHUB_MODULE_CLIENT_LL_HANDLE_DATA;


typedef struct EDGE_ENVIRONMENT_VARIABLES_TAG
{
    const char* connection_string;
    const char* auth_scheme;
    const char* device_id;
    const char* iothub_name;
    const char* iothub_suffix;
    const char* gatewayhostname;
    const char* module_id;
    char* iothub_buffer;
} EDGE_ENVIRONMENT_VARIABLES;

static int retrieve_edge_environment_variabes(EDGE_ENVIRONMENT_VARIABLES *edge_environment_variables)
{
    int result;
    const char* edgehubhostname;
    char* edgehubhostname_separator;

    if ((edge_environment_variables->connection_string = environment_get_variable(ENVIRONMENT_VAR_EDGEHUBCONNECTIONSTRING)) != NULL)
    {
        // If a connection string is set, we use it and ignore all other environment variables.
        result = 0;
    }
    else
    {
        if ((edge_environment_variables->auth_scheme = environment_get_variable(ENVIRONMENT_VAR_EDGEAUTHSCHEME)) == NULL)
        {
            LogError("Environment %s not set", ENVIRONMENT_VAR_EDGEAUTHSCHEME);
            result = __FAILURE__;
        }
        else if (strcmp(edge_environment_variables->auth_scheme, SAS_TOKEN_AUTH) != 0)
        {
            LogError("Environment %s was set to %s, but only support for %s", ENVIRONMENT_VAR_EDGEAUTHSCHEME, edge_environment_variables->auth_scheme, SAS_TOKEN_AUTH);
            result = __FAILURE__;
        }
        else if ((edge_environment_variables->device_id = environment_get_variable(ENVIRONMENT_VAR_EDGEDEVICEID)) == NULL)
        {
            LogError("Environment %s not set", ENVIRONMENT_VAR_EDGEDEVICEID);
            result = __FAILURE__;
        }
        else if ((edgehubhostname = environment_get_variable(ENVIRONMENT_VAR_EDGEHUBHOSTNAME)) == NULL)
        {
            LogError("Environment %s not set", ENVIRONMENT_VAR_EDGEHUBHOSTNAME);
            result = __FAILURE__;
        }
        else if ((edge_environment_variables->gatewayhostname = environment_get_variable(ENVIRONMENT_VAR_EDGEGATEWAYHOST)) == NULL)
        {
            LogError("Environment %s not set", ENVIRONMENT_VAR_EDGEGATEWAYHOST);
            result = __FAILURE__;
        }
        else if ((edge_environment_variables->module_id = environment_get_variable(ENVIRONMENT_VAR_EDGEMODULEID)) == NULL)
        {
            LogError("Environment %s not set", ENVIRONMENT_VAR_EDGEMODULEID);
            result = __FAILURE__;
        }
        // Make a copy of just ENVIRONMENT_VAR_EDGEHUBHOSTNAME.  We need to make changes in place (namely inserting a '\0')
        // and can't do this with system environment variable safely.
        else if (mallocAndStrcpy_s(&edge_environment_variables->iothub_buffer, edgehubhostname) != 0)
        {
            LogError("Unable to copy buffer");
            result = __FAILURE__;
        }
        else if ((edgehubhostname_separator = strchr(edge_environment_variables->iothub_buffer, '.')) == NULL)
        {
            LogError("Environment edgehub %s invalid, requires '.' separator", edge_environment_variables->iothub_buffer);
            result = __FAILURE__;
        }
        else if (*(edgehubhostname_separator + 1) == 0)
        {
            LogError("Environment edgehub %s invalid, no content after '.' separator", edge_environment_variables->iothub_buffer);
            result = __FAILURE__;
        }
        else
        {
            edge_environment_variables->iothub_name = edge_environment_variables->iothub_buffer;
            *edgehubhostname_separator = 0;
            edge_environment_variables->iothub_suffix = edgehubhostname_separator + 1;
            result = 0;
        }
    }

    return result;
}

IOTHUB_MODULE_CLIENT_LL_HANDLE IoTHubModuleClient_LL_CreateFromConnectionString(const char* connectionString, IOTHUB_CLIENT_TRANSPORT_PROVIDER protocol)
{
    IOTHUB_MODULE_CLIENT_LL_HANDLE_DATA* result;
    
    if ((result = malloc(sizeof(IOTHUB_MODULE_CLIENT_LL_HANDLE_DATA))) == NULL)
    {
        LogError("Failed to allocate module client ll handle");
    }
    else if ((result->coreHandle = IoTHubClientCore_LL_CreateFromConnectionString(connectionString, protocol)) == NULL)
    {
        LogError("Failed to create core handle");
        IoTHubModuleClient_LL_Destroy(result);
        result = NULL;
    }
    else if ((result->methodHandle = IoTHubClientCore_LL_GetMethodHandle(result->coreHandle)) == NULL)
    {
        LogError("Failed to set module method handle");
        IoTHubModuleClient_LL_Destroy(result);
        result = NULL;
    }

    return result;
}

IOTHUB_MODULE_CLIENT_LL_HANDLE IoTHubModuleClient_LL_CreateFromEnvironment(IOTHUB_CLIENT_TRANSPORT_PROVIDER protocol)
{
    IOTHUB_MODULE_CLIENT_LL_HANDLE_DATA* result;
    EDGE_ENVIRONMENT_VARIABLES edge_environment_variables;

    memset(&edge_environment_variables, 0, sizeof(edge_environment_variables));

    if (retrieve_edge_environment_variabes(&edge_environment_variables) != 0)
    {
        LogError("retrieve_edge_environment_variabes failed");
        result = NULL;
    }
    // The presence of a connection string environment variable means we use it, ignoring other settings
    else if (edge_environment_variables.connection_string != NULL)
    {
        result = IoTHubModuleClient_LL_CreateFromConnectionString(edge_environment_variables.connection_string, protocol);
    }
    else if (iothub_security_init(IOTHUB_SECURITY_TYPE_HTTP_EDGE) != 0)
    {
        LogError("iothub_security_init failed");
        result = NULL;
    }
    else
    {
        IOTHUB_CLIENT_CONFIG client_config;

        memset(&client_config, 0, sizeof(client_config));
        client_config.protocol = protocol;
        client_config.deviceId = edge_environment_variables.device_id;
        client_config.iotHubName = edge_environment_variables.iothub_name;
        client_config.iotHubSuffix = edge_environment_variables.iothub_suffix;
        client_config.protocolGatewayHostName = edge_environment_variables.gatewayhostname;

        if ((result = malloc(sizeof(IOTHUB_MODULE_CLIENT_LL_HANDLE_DATA))) == NULL)
        {
            LogError("Failed to allocate module client ll handle");
        }
        else if ((result->coreHandle = IoTHubClientCore_LL_CreateFromEnvironment(&client_config, edge_environment_variables.module_id)) == NULL)
        {
            LogError("Failed to create core handle");
            IoTHubModuleClient_LL_Destroy(result);
            result = NULL;
        }
        else if ((result->methodHandle = IoTHubClientCore_LL_GetMethodHandle(result->coreHandle)) == NULL)
        {
            LogError("Failed to set module method handle");
            IoTHubModuleClient_LL_Destroy(result);
            result = NULL;
        }
    }

    free(edge_environment_variables.iothub_buffer);
    return result;
}

void IoTHubModuleClient_LL_Destroy(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle)
{
    if (iotHubModuleClientHandle != NULL)
    {
        IoTHubClientCore_LL_Destroy(iotHubModuleClientHandle->coreHandle);
        //Note that we don't have to free the module method handle because it's (currently, until major refactor) owned by the core
    }
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_SendEventAsync(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, IOTHUB_MESSAGE_HANDLE eventMessageHandle, IOTHUB_CLIENT_EVENT_CONFIRMATION_CALLBACK eventConfirmationCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_SendEventAsync(iotHubModuleClientHandle->coreHandle, eventMessageHandle, eventConfirmationCallback, userContextCallback);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_GetSendStatus(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, IOTHUB_CLIENT_STATUS *iotHubClientStatus)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_GetSendStatus(iotHubModuleClientHandle->coreHandle, iotHubClientStatus);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_SetMessageCallback(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, IOTHUB_CLIENT_MESSAGE_CALLBACK_ASYNC messageCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_SetMessageCallback(iotHubModuleClientHandle->coreHandle, messageCallback, userContextCallback);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_SetConnectionStatusCallback(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, IOTHUB_CLIENT_CONNECTION_STATUS_CALLBACK connectionStatusCallback, void * userContextCallback)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_SetConnectionStatusCallback(iotHubModuleClientHandle->coreHandle, connectionStatusCallback, userContextCallback);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_SetRetryPolicy(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, IOTHUB_CLIENT_RETRY_POLICY retryPolicy, size_t retryTimeoutLimitInSeconds)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_SetRetryPolicy(iotHubModuleClientHandle->coreHandle, retryPolicy, retryTimeoutLimitInSeconds);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_GetRetryPolicy(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, IOTHUB_CLIENT_RETRY_POLICY* retryPolicy, size_t* retryTimeoutLimitInSeconds)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_GetRetryPolicy(iotHubModuleClientHandle->coreHandle, retryPolicy, retryTimeoutLimitInSeconds);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_GetLastMessageReceiveTime(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, time_t* lastMessageReceiveTime)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_GetLastMessageReceiveTime(iotHubModuleClientHandle->coreHandle, lastMessageReceiveTime);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

void IoTHubModuleClient_LL_DoWork(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle)
{
    if (iotHubModuleClientHandle != NULL)
    {
        IoTHubClientCore_LL_DoWork(iotHubModuleClientHandle->coreHandle);
    }
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_SetOption(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, const char* optionName, const void* value)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_SetOption(iotHubModuleClientHandle->coreHandle, optionName, value);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_SetModuleTwinCallback(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, IOTHUB_CLIENT_DEVICE_TWIN_CALLBACK moduleTwinCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_SetDeviceTwinCallback(iotHubModuleClientHandle->coreHandle, moduleTwinCallback, userContextCallback);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_SendReportedState(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, const unsigned char* reportedState, size_t size, IOTHUB_CLIENT_REPORTED_STATE_CALLBACK reportedStateCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_SendReportedState(iotHubModuleClientHandle->coreHandle, reportedState, size, reportedStateCallback, userContextCallback);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_SetModuleMethodCallback(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, IOTHUB_CLIENT_INBOUND_DEVICE_METHOD_CALLBACK inboundDeviceMethodCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_SetDeviceMethodCallback_Ex(iotHubModuleClientHandle->coreHandle, inboundDeviceMethodCallback, userContextCallback);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_ModuleMethodResponse(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, METHOD_HANDLE methodId, const unsigned char* response, size_t response_size, int status_response)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_DeviceMethodResponse(iotHubModuleClientHandle->coreHandle, methodId, response, response_size, status_response);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_SendEventToOutputAsync(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, IOTHUB_MESSAGE_HANDLE eventMessageHandle, const char* outputName, IOTHUB_CLIENT_EVENT_CONFIRMATION_CALLBACK eventConfirmationCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_SendEventToOutputAsync(iotHubModuleClientHandle->coreHandle, eventMessageHandle, outputName, eventConfirmationCallback, userContextCallback);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_SetInputMessageCallback(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, const char* inputName, IOTHUB_CLIENT_MESSAGE_CALLBACK_ASYNC eventHandlerCallback, void* userContextCallback)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubClientCore_LL_SetInputMessageCallback(iotHubModuleClientHandle->coreHandle, inputName, eventHandlerCallback, userContextCallback);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_MethodInvoke(IOTHUB_MODULE_CLIENT_LL_HANDLE iotHubModuleClientHandle, const char* deviceId, const char* moduleId, const char* methodName, const char* methodPayload, unsigned int timeout, int* responseStatus, unsigned char** responsePayload, size_t* responsePayloadSize)
{
    IOTHUB_CLIENT_RESULT result;
    if (iotHubModuleClientHandle != NULL)
    {
        result = IoTHubModuleClient_LL_MethodInvoke_Impl(iotHubModuleClientHandle->methodHandle, deviceId, moduleId, methodName, methodPayload, timeout, responseStatus, responsePayload, responsePayloadSize);
    }
    else
    {
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    return result;
}

