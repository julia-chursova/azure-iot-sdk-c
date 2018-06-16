// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "azure_c_shared_utility/gballoc.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/constbuffer.h"
#include "azure_c_shared_utility/httpapiex.h"
#include "azure_c_shared_utility/httpapiexsas.h"
#include "azure_c_shared_utility/uniqueid.h"
#include "azure_c_shared_utility/shared_util_options.h"

#include "parson.h"

#include "internal/iothub_client_private.h"
#include "iothub_client_authorization.h"
#include "iothub_client_core_common.h"
#include "iothub_client_version.h"
#include "iothub_module_client_ll_method.h"

#define  HTTP_HEADER_KEY_AUTHORIZATION  "Authorization"
#define  HTTP_HEADER_VAL_AUTHORIZATION  " "
#define  HTTP_HEADER_KEY_REQUEST_ID  "Request-Id"
#define  HTTP_HEADER_KEY_USER_AGENT  "User-Agent"
#define  HTTP_HEADER_VAL_USER_AGENT  CLIENT_DEVICE_TYPE_PREFIX CLIENT_DEVICE_BACKSLASH IOTHUB_SDK_VERSION
#define  HTTP_HEADER_KEY_CONTENT_TYPE  "Content-Type"
#define  HTTP_HEADER_VAL_CONTENT_TYPE  "application/json; charset=utf-8"
#define  UID_LENGTH 37

#define SASTOKEN_LIFETIME 3600

static const char* const URL_API_VERSION = "?api-version=2017-11-08-preview";
static const char* const RELATIVE_PATH_FMT_METHOD = "/twins/%s/modules/%s/methods%s";
static const char* const RELATIVE_PATH_FMT_METHOD_PAYLOAD = "{\"methodName\":\"%s\",\"timeout\":%d,\"payload\":%s}";
static const char* const SCOPE_FMT = "%s/devices/%s/modules/%s";

typedef struct IOTHUB_MODULE_CLIENT_METHOD_HANDLE_DATA_TAG
{
    const char* hostname;
    const char* deviceId;
    const char* moduleId;
    IOTHUB_AUTHORIZATION_HANDLE authorizationHandle;
} IOTHUB_MODULE_CLIENT_METHOD_HANDLE_DATA;


IOTHUB_MODULE_CLIENT_METHOD_HANDLE IoTHubModuleClient_LL_MethodHandle_Create(const IOTHUB_CLIENT_CONFIG* config, IOTHUB_AUTHORIZATION_HANDLE authorizationHandle, const char* module_id)
{
    IOTHUB_MODULE_CLIENT_METHOD_HANDLE_DATA* handleData;

    if ((config == NULL) || (authorizationHandle == NULL))
    {
        LogError("input cannot be NULL");
        handleData = NULL;
    }
    else if ((handleData = malloc(sizeof(IOTHUB_MODULE_CLIENT_METHOD_HANDLE_DATA))) == NULL)
    {
        LogError("memory allocation error");
    }
    else
    {
        handleData->authorizationHandle = authorizationHandle;
        handleData->deviceId = config->deviceId;
        handleData->moduleId = module_id;
        handleData->hostname = config->protocolGatewayHostName;
    }

    return (IOTHUB_MODULE_CLIENT_METHOD_HANDLE)handleData;
}

void IoTHubModuleClient_LL_MethodHandle_Destroy(IOTHUB_MODULE_CLIENT_METHOD_HANDLE methodHandle)
{
    if (methodHandle != NULL)
    {
        free(methodHandle);
    }
}

static const char* generateGuid(void)
{
    char* result;

    if ((result = malloc(UID_LENGTH)) != NULL)
    {
        result[0] = '\0';
        if (UniqueId_Generate(result, UID_LENGTH) != UNIQUEID_OK)
        {
            LogError("UniqueId_Generate failed");
            free((void*)result);
            result = NULL;
        }
    }
    return (const char*)result;
}

static HTTP_HEADERS_HANDLE createHttpHeader()
{
    /*Codes_SRS_IOTHUBDEVICEMETHOD_12_020: [ IoTHubDeviceMethod_GetTwin shall add the following headers to the created HTTP GET request: authorization=sasToken,Request-Id=1001,Accept=application/json,Content-Type=application/json,charset=utf-8 ]*/
    HTTP_HEADERS_HANDLE httpHeader;
    const char* guid;

    if ((httpHeader = HTTPHeaders_Alloc()) == NULL)
    {
        LogError("HTTPHeaders_Alloc failed");
    }
    else if (HTTPHeaders_AddHeaderNameValuePair(httpHeader, HTTP_HEADER_KEY_AUTHORIZATION, HTTP_HEADER_VAL_AUTHORIZATION) != HTTP_HEADERS_OK)
    {
        LogError("HTTPHeaders_AddHeaderNameValuePair failed for Authorization header");
        HTTPHeaders_Free(httpHeader);
        httpHeader = NULL;
    }
    else if ((guid = generateGuid()) == NULL)
    {
        LogError("GUID creation failed");
        HTTPHeaders_Free(httpHeader);
        httpHeader = NULL;
    }
    else if (HTTPHeaders_AddHeaderNameValuePair(httpHeader, HTTP_HEADER_KEY_REQUEST_ID, guid) != HTTP_HEADERS_OK)
    {
        LogError("HTTPHeaders_AddHeaderNameValuePair failed for RequestId header");
        free((void*)guid);
        HTTPHeaders_Free(httpHeader);
        httpHeader = NULL;
    }
    else if (HTTPHeaders_AddHeaderNameValuePair(httpHeader, HTTP_HEADER_KEY_USER_AGENT, HTTP_HEADER_VAL_USER_AGENT) != HTTP_HEADERS_OK)
    {
        LogError("HTTPHeaders_AddHeaderNameValuePair failed for User-Agent header");
        free((void*)guid);
        HTTPHeaders_Free(httpHeader);
        httpHeader = NULL;
    }
    else if (HTTPHeaders_AddHeaderNameValuePair(httpHeader, HTTP_HEADER_KEY_CONTENT_TYPE, HTTP_HEADER_VAL_CONTENT_TYPE) != HTTP_HEADERS_OK)
    {
        LogError("HTTPHeaders_AddHeaderNameValuePair failed for Content-Type header");
        free((void*)guid);
        HTTPHeaders_Free(httpHeader);
        httpHeader = NULL;
    }
    else
    {
        free((void*)guid);
    }

    return httpHeader;
}

static IOTHUB_CLIENT_RESULT parseResponseJson(BUFFER_HANDLE responseJson, int* responseStatus, unsigned char** responsePayload, size_t* responsePayloadSize)
{
    IOTHUB_CLIENT_RESULT result;
    JSON_Value* root_value;
    JSON_Object* json_object;
    JSON_Value* statusJsonValue;
    JSON_Value* payloadJsonValue;
    char* payload;
    STRING_HANDLE jsonStringHandle;
    const char* jsonStr;
    unsigned char* bufferStr;

    if ((bufferStr = BUFFER_u_char(responseJson)) == NULL)
    {
        LogError("BUFFER_u_char failed");
        result = IOTHUB_CLIENT_ERROR;
    }
    else if ((jsonStringHandle = STRING_from_byte_array(bufferStr, BUFFER_length(responseJson))) == NULL)
    {
        LogError("STRING_construct_n failed");
        result = IOTHUB_CLIENT_ERROR;
    }
    else if ((jsonStr = STRING_c_str(jsonStringHandle)) == NULL)
    {
        LogError("STRING_c_str failed");
        STRING_delete(jsonStringHandle);
        result = IOTHUB_CLIENT_ERROR;
    }
    else if ((root_value = json_parse_string(jsonStr)) == NULL)
    {
        LogError("json_parse_string failed");
        STRING_delete(jsonStringHandle);
        result = IOTHUB_CLIENT_ERROR;
    }
    else if ((json_object = json_value_get_object(root_value)) == NULL)
    {
        LogError("json_value_get_object failed");
        STRING_delete(jsonStringHandle);
        json_value_free(root_value);
        result = IOTHUB_CLIENT_ERROR;
    }
    else if ((statusJsonValue = json_object_get_value(json_object, "status")) == NULL)
    {
        LogError("json_object_get_value failed for status");
        STRING_delete(jsonStringHandle);
        json_value_free(root_value);
        result = IOTHUB_CLIENT_ERROR;
    }
    else if ((payloadJsonValue = json_object_get_value(json_object, "payload")) == NULL)
    {
        LogError("json_object_get_value failed for payload");
        STRING_delete(jsonStringHandle);
        json_value_free(root_value);
        result = IOTHUB_CLIENT_ERROR;
    }
    else if ((payload = json_serialize_to_string(payloadJsonValue)) == NULL)
    {
        LogError("json_serialize_to_string failed for payload");
        STRING_delete(jsonStringHandle);
        json_value_free(root_value);
        result = IOTHUB_CLIENT_ERROR;
    }
    else
    {
        *responseStatus = (int)json_value_get_number(statusJsonValue);
        *responsePayload = (unsigned char *)payload;
        *responsePayloadSize = strlen(payload);

        STRING_delete(jsonStringHandle);
        json_value_free(root_value);
        result = IOTHUB_CLIENT_OK;
    }

    return result;
}

static BUFFER_HANDLE createMethodPayloadJson(const char* methodName, unsigned int timeout, const char* payload)
{
    STRING_HANDLE stringHandle;
    const char* stringHandle_c_str;
    BUFFER_HANDLE result;

    if ((stringHandle = STRING_construct_sprintf(RELATIVE_PATH_FMT_METHOD_PAYLOAD, methodName, timeout, payload)) == NULL)
    {
        LogError("STRING_construct_sprintf failed");
        result = NULL;
    }
    else if ((stringHandle_c_str = STRING_c_str(stringHandle)) == NULL)
    {
        LogError("STRING_c_str failed");
        STRING_delete(stringHandle);
        result = NULL;
    }
    else
    {
        result = BUFFER_create((const unsigned char*)stringHandle_c_str, strlen(stringHandle_c_str));
        STRING_delete(stringHandle);
    }
    return result;
}

static IOTHUB_CLIENT_RESULT sendHttpRequestMethod(IOTHUB_MODULE_CLIENT_METHOD_HANDLE moduleMethodHandle, const char* deviceId, const char* moduleId, BUFFER_HANDLE deviceJsonBuffer, BUFFER_HANDLE responseBuffer)
{
    IOTHUB_CLIENT_RESULT result;

    HTTPAPIEX_HANDLE httpExApiHandle;
    HTTP_HEADERS_HANDLE httpHeader;
    STRING_HANDLE relativePath;
    STRING_HANDLE scope;
    char* sastoken;
    char* trustedCertificate;
    unsigned int statusCode = 0;

    if ((httpHeader = createHttpHeader()) == NULL)
    {
        LogError("HttpHeader creation failed");
        result = IOTHUB_CLIENT_ERROR;
    }
    else if ((scope = STRING_construct_sprintf(SCOPE_FMT, moduleMethodHandle->hostname, moduleMethodHandle->deviceId, moduleMethodHandle->moduleId)) == NULL) //No URL Encode necessary - SCOPE_FMT already URL Encoded
    {
        LogError("Failed constructing scope");
        HTTPHeaders_Free(httpHeader);
        result = IOTHUB_CLIENT_ERROR;
    }
    else if ((sastoken = IoTHubClient_Auth_Get_SasToken(moduleMethodHandle->authorizationHandle, STRING_c_str(scope), SASTOKEN_LIFETIME, NULL)) == NULL)
    {
        LogError("SasToken generation failed");
        HTTPHeaders_Free(httpHeader);
        STRING_delete(scope);
        result = IOTHUB_CLIENT_ERROR;
    }
    else if (HTTPHeaders_ReplaceHeaderNameValuePair(httpHeader, HTTP_HEADER_KEY_AUTHORIZATION, sastoken) != HTTP_HEADERS_OK)
    {
        LogError("Failure updating Http Headers");
        HTTPHeaders_Free(httpHeader);
        STRING_delete(scope);
        free(sastoken);
        result = IOTHUB_CLIENT_ERROR;
    }
    else if ((relativePath = STRING_construct_sprintf(RELATIVE_PATH_FMT_METHOD, deviceId, moduleId, URL_API_VERSION)) == NULL)
    {
        LogError("Failure creating relative path");
        HTTPHeaders_Free(httpHeader);
        STRING_delete(scope);
        free(sastoken);
        result = IOTHUB_CLIENT_ERROR;
    }
    else if ((httpExApiHandle = HTTPAPIEX_Create(moduleMethodHandle->hostname)) == NULL)
    {
        LogError("HTTPAPIEX_Create failed");
        HTTPHeaders_Free(httpHeader);
        STRING_delete(scope);
        free(sastoken);
        STRING_delete(relativePath);
        result = IOTHUB_CLIENT_ERROR;
    }
    else if ((trustedCertificate = IoTHubClient_Auth_Get_TrustBundle(moduleMethodHandle->authorizationHandle)) == NULL)
    {
        LogError("Failed to get TrustBundle");
        HTTPHeaders_Free(httpHeader);
        STRING_delete(scope);
        free(sastoken);
        STRING_delete(relativePath);
        HTTPAPIEX_Destroy(httpExApiHandle);
        result = IOTHUB_CLIENT_ERROR;
    }
    else
    {
        if (HTTPAPIEX_SetOption(httpExApiHandle, OPTION_TRUSTED_CERT, trustedCertificate) != HTTPAPIEX_OK)
        {
            LogError("Setting trusted certificate failed");
            result = IOTHUB_CLIENT_ERROR;
        }
        else if (HTTPAPIEX_ExecuteRequest(httpExApiHandle, HTTPAPI_REQUEST_POST, STRING_c_str(relativePath), httpHeader, deviceJsonBuffer, &statusCode, NULL, responseBuffer) != HTTPAPIEX_OK)
        {
                LogError("HTTPAPIEX_ExecuteRequest failed");
                result = IOTHUB_CLIENT_ERROR;
        }
        else
        {
            if (statusCode == 200)
            {
                result = IOTHUB_CLIENT_OK;
            }
            else
            {
                LogError("Http Failure status code %d.", statusCode);
                result = IOTHUB_CLIENT_ERROR;
            }
        }

        HTTPHeaders_Free(httpHeader);
        STRING_delete(scope);
        free(sastoken);
        STRING_delete(relativePath);
        HTTPAPIEX_Destroy(httpExApiHandle);
        free(trustedCertificate);
    }

    return result;
}

IOTHUB_CLIENT_RESULT IoTHubModuleClient_LL_MethodInvoke_Impl(IOTHUB_MODULE_CLIENT_METHOD_HANDLE moduleMethodHandle, const char* deviceId, const char* moduleId, const char* methodName, const char* methodPayload, unsigned int timeout, int* responseStatus, unsigned char** responsePayload, size_t* responsePayloadSize)
{
    IOTHUB_CLIENT_RESULT result;

    if ((moduleMethodHandle == NULL) || (deviceId == NULL) || (moduleId == NULL) || (methodName == NULL) || (methodPayload == NULL) || (responseStatus == NULL) || (responsePayload == NULL) || (responsePayloadSize == NULL))
    {
        LogError("Input parameter cannot be NULL");
        result = IOTHUB_CLIENT_INVALID_ARG;
    }
    else
    {
        BUFFER_HANDLE httpPayloadBuffer;
        BUFFER_HANDLE responseBuffer;

        if ((httpPayloadBuffer = createMethodPayloadJson(methodName, timeout, methodPayload)) == NULL)
        {
            LogError("BUFFER creation failed for httpPayloadBuffer");
            result = IOTHUB_CLIENT_ERROR;
        }
        else if ((responseBuffer = BUFFER_new()) == NULL)
        {
            LogError("BUFFER_new failed for responseBuffer");
            BUFFER_delete(httpPayloadBuffer);
            result = IOTHUB_CLIENT_ERROR;
        }
        else if (sendHttpRequestMethod(moduleMethodHandle, deviceId, moduleId, httpPayloadBuffer, responseBuffer) != IOTHUB_CLIENT_OK)
        {
            LogError("Failure sending HTTP request for device method invoke");
            BUFFER_delete(responseBuffer);
            BUFFER_delete(httpPayloadBuffer);
            result = IOTHUB_CLIENT_ERROR;
        }
        else if ((parseResponseJson(responseBuffer, responseStatus, responsePayload, responsePayloadSize)) != IOTHUB_CLIENT_OK)
        {
            LogError("Failure parsing response");
            BUFFER_delete(responseBuffer);
            BUFFER_delete(httpPayloadBuffer);
            result = IOTHUB_CLIENT_ERROR;
        }
        else
        {
            result = IOTHUB_CLIENT_OK;

            BUFFER_delete(responseBuffer);
            BUFFER_delete(httpPayloadBuffer);
        }
    }
    return result;
}