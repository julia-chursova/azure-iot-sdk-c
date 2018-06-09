#ifndef IOTHUB_MODULE_CLIENT_LL_METHOD_H
#define IOTHUB_MODULE_CLIENT_LL_METHOD_H

#include <stddef.h>

#include "azure_c_shared_utility/macro_utils.h"
#include "azure_c_shared_utility/umock_c_prod.h"

#include "iothub_client_authorization.h"
#include "iothub_client_core_common.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct IOTHUB_MODULE_CLIENT_METHOD_HANDLE_DATA_TAG* IOTHUB_MODULE_CLIENT_METHOD_HANDLE;

    MOCKABLE_FUNCTION(, IOTHUB_MODULE_CLIENT_METHOD_HANDLE, IoTHubModuleClient_LL_MethodHandle_Create, const IOTHUB_CLIENT_CONFIG*, config, IOTHUB_AUTHORIZATION_HANDLE, authorizationHandle, const char*, module_id);
    MOCKABLE_FUNCTION(, void, IoTHubModuleClient_LL_MethodHandle_Destroy, IOTHUB_MODULE_CLIENT_METHOD_HANDLE, methodHandle);

    MOCKABLE_FUNCTION(, IOTHUB_CLIENT_RESULT, IoTHubModuleClient_LL_MethodInvoke_Impl, IOTHUB_MODULE_CLIENT_METHOD_HANDLE, moduleMethodHandle, const char*, deviceId, const char*, moduleId, const char*, methodName, const char*, methodPayload, unsigned int, timeout, int*, responseStatus, unsigned char**, responsePayload, size_t*, responsePayloadSize);

#ifdef __cplusplus
}
#endif

#endif /* IOTHUB_MODULE_CLIENT_LL_METHOD_H */