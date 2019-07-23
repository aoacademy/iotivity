#include "iotivity_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <getopt.h>
#include "ocstack.h"
#include "payload_logging.h"
#include "logger.h"
#include "utilities.c"
#include "oic_string.h"
#include "ocpayload.h"
#include "ocprovisioningmanager.h"
#include "oxmjustworks.h"
#include "oxmrandompin.h"
#include "srmutility.h"
#include "spresource.h"
#include "pmtypes.h"
#include "oxmverifycommon.h"
#include "mbedtls/config.h"
#include "mbedtls/pem.h"
#include "mbedtls/x509_csr.h"
#include "occertutility.h"
#include "pmutility.h"
#include "occloudprovisioning.h"
#include "auth.h"
#include "credresource.h"
#include <string>
#include <map>
#include <cstdlib>
#include "OCPlatform.h"
#include "OCApi.h"

#ifdef _MSC_VER
#include <io.h>
#define F_OK 0
#define access _access_s
#endif

#define MAX_RESOURCE_TYPE_SIZE      (32)
#define MAX_RESOURCES_REMEMBERED    (100)
#define MAX_USER_INPUT              (100)
#define DISCOVERY_TIMEOUT   10  // 10 sec
#define TAG "CLIENT_APP"

static char const *g_ctx = "Provision Manager Client Application Context";
static OCProvisionDev_t* g_own_list;
static OCProvisionDev_t* g_unown_list;
static int g_own_cnt;
static int g_unown_cnt;
static const char *gResourceUri = "/switch";

static std::string coapServerResource;
static int coapSecureResource;
static OCConnectivityType ocConnType;
static OCDevAddr* endpoint;

static const char* SVR_DB_FILE_NAME = "oic_svr_db_client.dat";
static const char* PRVN_DB_FILE_NAME = "oic_prvn_mng.db";
const char * COAPS_STR = "coaps";
const char * OIC_STD_URI_PREFIX = "/oic/";
static int WithTcp = 0;
static char DISCOVERY_QUERY[] = "%s/oic/res";

static bool PROMPT_USER = false;
static bool DISCOVERY_DONE = true;
static bool DISCOVERING = false;
static bool REQUEST_DONE = true;
static bool OT_DONE = true;
static bool GET_DONE = true;
static bool VALUE = false;
static bool STOP = false;

static int printDevList(const OCProvisionDev_t*);
static void printUuid(const OicUuid_t*);
static size_t printResultList(const OCProvisionResult_t*, const size_t);
static void initialMenu(void);
static int requests(void);
int parseClientResponse(OCClientResponse * clientResponse);

using namespace OC;

#define CA_OPTION_CONTENT_VERSION 2053
#define COAP_OPTION_CONTENT_FORMAT 12

//Payload sent to the Server to toggle the LED value

OCPayload *postLEDPayload()
{
    OCRepPayload *payload = OCRepPayloadCreate();

    if (!payload)
    {
        std::cout << "Failed to create put payload object" << std::endl;
        std::exit(1);
    }

    if(VALUE) {
        OCRepPayloadSetPropBool(payload, "value", false);
        VALUE = false;
    } else {
        OCRepPayloadSetPropBool(payload, "value", true);
        VALUE = true;
    }

    if (payload->values)
    {
        return (OCPayload *) payload;
    }
    else
    {
        OCRepPayloadDestroy(payload);
        return NULL;
    }
}

//Payload sent to the Server containing new LED URI

OCPayload *postURIPayload()
{
    OCRepPayload *payload = OCRepPayloadCreate();

    if (!payload)
    {
        std::cout << "Failed to create put payload object" << std::endl;
        std::exit(1);
    }

    char input[MAX_USER_INPUT] = {0};

    printf("Enter new URI:\n");
    char *ret = fgets(input, sizeof(input), stdin);
    (void) ret;
    printf("\n\n%s\n\n", input);
    OCRepPayloadSetPropString(payload, "uri", input);

    return (OCPayload *) payload;
}

//Function to format the request sent to the Server

OCStackResult InvokeOCDoResource(std::ostringstream &query,
                                 OCMethod method,
                                 const OCDevAddr *dest, OCPayload *payload,
                                 OCQualityOfService qos,
                                 OCClientResponseHandler cb,
                                 OCHeaderOption *options, uint8_t numOptions)
{
    OCStackResult ret;
    OCCallbackData cbData;

    cbData.cb = cb;
    cbData.context = NULL;
    cbData.cd = NULL;

    //OCPayload *payload = (method == OC_REST_PUT || method == OC_REST_POST) ? postPayload() : NULL;

    ret = OCDoRequest(NULL, method, query.str().c_str(), dest,
                      payload, ocConnType, qos, &cbData, options, numOptions);

    OCPayloadDestroy(payload);

    if (ret != OC_STACK_OK)
    {
        OIC_LOG_V(ERROR, TAG, "OCDoResource returns error %s", decode_oc_stack_result(ret));
        PROMPT_USER = true;
    }
    
    return ret;
}

//Function to handle exit signal

void
SIGINTHandlerCallBack(int signalNumber)
{
    OIC_LOG_V(INFO, TAG, "[%s] Received SIGINT", __func__);
    if (signalNumber == SIGINT)
    {
        STOP = true;
    }
}

//Function to open .dat files that define client and server characteristics

FILE*
ClientFOpen(const char *file, const char *mode)
{
    /*if (0 == strcmp(file, DEV_PROP))
    {
        return fopen(DEV_PROP, mode);
    }
    else */if (0 == strncmp(file, OC_SECURITY_DB_DAT_FILE_NAME, strlen(OC_SECURITY_DB_DAT_FILE_NAME)))
    {
        // input |g_svr_db_fname| internally by force, not using |path| parameter
        // because |OCPersistentStorage::open| is called |OCPersistentStorage| internally
        // with its own |SVR_DB_FILE_NAME|
        return fopen(SVR_DB_FILE_NAME, mode);
    }
    else
    {
        return fopen(file, mode);
    }
}

static int discoverUnownedDevices(void) {
    // delete unowned device list before updating it
    if(g_unown_list)
    {
        OCDeleteDiscoveredDevices(g_unown_list);
        g_unown_list = NULL;
    }

    // call |OCDiscoverUnownedDevices| API
    printf("   Discovering Only Unowned Devices on Network..\n");
    if(OC_STACK_OK != OCDiscoverUnownedDevices(DISCOVERY_TIMEOUT, &g_unown_list))
    {
        OIC_LOG(ERROR, TAG, "OCDiscoverUnownedDevices API error");
        return -1;
    }

    // display the discovered unowned list
    printf("   > Discovered Unowned Devices\n");
    g_unown_cnt = printDevList(g_unown_list);

    return 0;
}

static int discoverOwnedDevices(void)
{
    // delete owned device list before updating it
    if(g_own_list)
    {
        OCDeleteDiscoveredDevices(g_own_list);
        g_own_list = NULL;
    }

    // call |OCDiscoverOwnedDevices| API
    printf("   Discovering Only Owned Devices on Network..\n");
    if(OC_STACK_OK != OCDiscoverOwnedDevices(DISCOVERY_TIMEOUT, &g_own_list))
    {
        OIC_LOG(ERROR, TAG, "OCDiscoverOwnedDevices API error");
        return -1;
    }

    // display the discovered owned list
    printf("   > Discovered Owned Devices\n");
    g_own_cnt = printDevList(g_own_list);
#ifdef __WITH_TLS__
    setDevProtocol(g_own_list);
#endif
    return 0;
}

// callback function(s) for provisioning client using C-level provisioning API
static void ownershipTransferCB(void* ctx, size_t nOfRes, OCProvisionResult_t* arr, bool hasError)
{
    if(!hasError)
    {
        OIC_LOG_V(INFO, TAG, "Ownership Transfer SUCCEEDED - ctx: %s", (char*) ctx);
        OT_DONE = true;
    }
    else
    {
        OIC_LOG_V(ERROR, TAG, "Ownership Transfer FAILED - ctx: %s", (char*) ctx);
        printResultList((const OCProvisionResult_t*) arr, nOfRes);
        OT_DONE = true;
    }
    PROMPT_USER = true;
}

static int registerDevices()
{
    // check |unown_list| for registering devices
    if(!g_unown_list || 0>=g_unown_cnt)
    {
        printf("   > Unowned Device List, to Register Devices, is Empty\n");
        printf("   > Please Discover Unowned Devices first\n");
        return 0;  // normal case
    }

    OCProvisionDev_t *devList = NULL;
    devList = g_unown_list;

    // call |OCDoOwnershipTransfer| API
    // calling this API with callback actually acts like blocking
    // for error checking, the return value saved and printed
    OT_DONE = false;
    printf("   Registering All Discovered Unowned Devices..\n");
    OCStackResult rst = OCDoOwnershipTransfer((void*) g_ctx, devList, ownershipTransferCB);

    if(OC_STACK_OK != rst)
    {
        OIC_LOG_V(ERROR, TAG, "OCDoOwnershipTransfer API error: %d", rst);
        return -1;
    }

    // display the registered result
    printf("   > Registered Discovered Unowned Devices\n");
    printf("   > Please Discover Owned Devices for the Registered Result\n");

    return 0;
}

static int printDevList(const OCProvisionDev_t* dev_lst)
{
    if(!dev_lst)
    {
        printf("     Device List is Empty..\n\n");
        return 0;
    }

    OCProvisionDev_t* lst = (OCProvisionDev_t*) dev_lst;
    int lst_cnt = 0;
    for( ; lst; )
    {
        printf("     [%d] ", ++lst_cnt);
        printUuid((const OicUuid_t*) &lst->doxm->deviceID);
        printf("   %s:%d   %s", lst->endpoint.addr, lst->endpoint.port,lst->specVer);
        printf("\n");
        lst = lst->next;
    }
    printf("\n");

    return lst_cnt;
}

static void printUuid(const OicUuid_t* uid)
{
    for(int i=0; i<UUID_LENGTH; )
    {
        printf("%02X", (*uid).id[i++]);
        if(i==4 || i==6 || i==8 || i==10)  // canonical format for UUID has '8-4-4-4-12'
        {
            printf("-");
        }
    }
}

static size_t printResultList(const OCProvisionResult_t* rslt_lst, const size_t rslt_cnt)
{
    if (!rslt_lst || (0 == rslt_cnt))
    {
        printf("     Device List is Empty..\n\n");
        return 0;
    }

    size_t lst_cnt = 0;
    for (; rslt_cnt > lst_cnt; ++lst_cnt)
    {
        printf("     [%" PRIuPTR "] ", lst_cnt + 1);
        printUuid((const OicUuid_t*)&rslt_lst[lst_cnt].deviceId);
        printf(" - result: %d\n", rslt_lst[lst_cnt].res);
    }
    printf("\n");

    return lst_cnt;
}

static int provisioning() {

    int pmn_num = 0;

    while(!STOP && OT_DONE && !PROMPT_USER) {
        printf("\n");
        printf("1. Discover unowned devices\n");
        printf("2. Discover owned devices\n");
        printf("3. Register unowned devices\n\n");
        printf("9. Initial Menu\n");
        printf("0. Exit\n\n");
        printf(">> Enter Menu Number: ");
        for(int ret=0; 1!=ret; ) {
            ret = scanf("%d", &pmn_num);
            for( ; 0x20<=getchar(); );
        }
        printf("\n");
        switch(pmn_num) {
            case 1:
                if(discoverUnownedDevices()) {
                    OIC_LOG(ERROR, TAG, "Discover unowned devices failed");
                }
                break;
            case 2:
                if(discoverOwnedDevices()) {
                    OIC_LOG(ERROR, TAG, "Discover owned devices failed");
                }
                break;
            case 3:
                if(registerDevices()) {
                    OIC_LOG(ERROR, TAG, "Registering devices failed");
                }
                break;
            case 9:
                PROMPT_USER = false;
                initialMenu();
                break;
            case 0:
                STOP = true;
                break;
            default:
                printf(">> Entered wrong number. Please enter again\n\n");
        }
        if(STOP) {
            break;
        }
    }
    return 0;
}


// This is a function called back when a device is discovered
OCStackApplicationResult discoveryReqCB(void *, OCDoHandle,
                                        OCClientResponse *clientResponse)
{
    OIC_LOG(INFO, TAG, "Callback Context for DISCOVER query recvd successfully");

    if (clientResponse)
    {
        
        if (DISCOVERING) {
            OIC_LOG_V(INFO, TAG, "StackResult: %s", decode_oc_stack_result(clientResponse->result));
            OIC_LOG_V(INFO, TAG,
                  "Device =============> Discovered @ %s:%d",
                  clientResponse->devAddr.addr,
                  clientResponse->devAddr.port);
        }

        if (clientResponse->result == OC_STACK_OK)
        {

            if (DISCOVERING) {
                OIC_LOG_PAYLOAD(INFO, clientResponse->payload);
            }

            ocConnType = clientResponse->connType;

            if (parseClientResponse(clientResponse) != -1)
            {
                OCDiscoveryPayload *payload = (OCDiscoveryPayload *) clientResponse->payload;
                OCResourcePayload *resource = (OCResourcePayload *) payload->resources;
                for (;resource; resource = resource->next)
                {

                    if ((0 == strcmp(gResourceUri, resource->uri))
                         && (0 == strcmp(COAPS_STR, resource->eps->tps)))
                    {
                        
                        endpoint = &clientResponse->devAddr;
                        strcpy(endpoint->addr, resource->eps->addr);
                        endpoint->port = resource->eps->port;
                        endpoint->flags = resource->eps->family;

                        requests();

                    }
                }
            }
        }
    }
    PROMPT_USER = true;
    DISCOVERY_DONE = true;
    DISCOVERING = false;
    return OC_STACK_KEEP_TRANSACTION ;

}

static int discovery() {
    OCStackResult ret;
    OCCallbackData cbData;
    char queryUri[200];
    char ipaddr[100] = { '\0' };
    PROMPT_USER = false;
    DISCOVERY_DONE = false;
    DISCOVERING = true;

    snprintf(queryUri, sizeof (queryUri), DISCOVERY_QUERY, ipaddr);

    cbData.cb = discoveryReqCB;
    cbData.context = NULL;
    cbData.cd = NULL;

    /* Start a discovery query*/
    OIC_LOG_V(INFO, TAG, "Initiating %s Resource Discovery : %s\n",
              "Multicast",
              queryUri);

    ret = OCDoRequest(NULL, OC_REST_DISCOVER, queryUri, 0, 0, CT_DEFAULT,
                      OC_LOW_QOS, &cbData, NULL, 0);


    if (ret != OC_STACK_OK)
    {
        OIC_LOG(ERROR, TAG, "OCStack resource error");
    }
    return ret;
}

// This is a function called back when a PUT response is received
OCStackApplicationResult putReqCB(void *, OCDoHandle, OCClientResponse *clientResponse)
{
    OIC_LOG(INFO, TAG, "Callback Context for PUT recvd successfully");

    if (clientResponse)
    {
        OIC_LOG_V(INFO, TAG, "StackResult: %s",  decode_oc_stack_result(clientResponse->result));
        OIC_LOG_PAYLOAD(INFO, clientResponse->payload);
        OIC_LOG(INFO, TAG, "=============> Put Response");
    }
    REQUEST_DONE = true;
    PROMPT_USER = true;
    return OC_STACK_DELETE_TRANSACTION;
}

int InitPutRequest(OCDevAddr *endpoint, OCQualityOfService qos)
{
    OIC_LOG_V(INFO, TAG, "Executing %s", __func__);
    std::ostringstream query;
    query << coapServerResource;
    REQUEST_DONE = false;
    PROMPT_USER = false;

    return (InvokeOCDoResource(query, OC_REST_PUT, endpoint, NULL,
                               ((qos == OC_HIGH_QOS) ? OC_HIGH_QOS : OC_LOW_QOS), putReqCB, NULL, 0));
}

// This is a function called back when a GET response is received
OCStackApplicationResult getReqCB(void *, OCDoHandle, OCClientResponse *clientResponse)
{
    OIC_LOG(INFO, TAG, "Callback Context for GET query recvd successfully");

    if (clientResponse)
    {
        OCRepPayload* payload = (OCRepPayload*)clientResponse->payload;
        OCRepPayloadValue* values = payload->values;
        VALUE = values->b;
        OIC_LOG_V(INFO, TAG, "StackResult: %s",  decode_oc_stack_result(clientResponse->result));
        OIC_LOG_V(INFO, TAG, "SEQUENCE NUMBER: %d", clientResponse->sequenceNumber);
        OIC_LOG_PAYLOAD(INFO, clientResponse->payload);
        OIC_LOG(INFO, TAG, "=============> Get Response");
    }

    REQUEST_DONE = true;
    PROMPT_USER = true;
    return OC_STACK_DELETE_TRANSACTION;
}

int InitGetRequest(OCDevAddr *endpoint,  OCQualityOfService qos)
{
    OCStackResult ret;
    OIC_LOG_V(INFO, TAG, "Executing %s", __func__);
    std::ostringstream query;
    query << coapServerResource;
    REQUEST_DONE = false;
    PROMPT_USER = false;

    ret = InvokeOCDoResource(query, OC_REST_GET, endpoint, NULL,
                               qos,
                               getReqCB, NULL, 0);

    return ret;
}

// This is a function called back when a POST response is received
OCStackApplicationResult postReqCB(void *, OCDoHandle, OCClientResponse *clientResponse)
{
    OIC_LOG(INFO, TAG, "Callback Context for POST recvd successfully");

    if (clientResponse)
    {
        OIC_LOG_V(INFO, TAG, "StackResult: %s",  decode_oc_stack_result(clientResponse->result));
        OIC_LOG_PAYLOAD(INFO, clientResponse->payload);
        OIC_LOG(INFO, TAG, "=============> Post Response");
    }
    PROMPT_USER = true;
    REQUEST_DONE = true;
    return OC_STACK_DELETE_TRANSACTION;
}

//Initialize LED toggle value request
int InitLEDPostRequest(OCDevAddr *endpoint, OCQualityOfService qos) {

    OCStackResult result;
    OIC_LOG_V(INFO, TAG, "Executing %s", __func__);
    std::ostringstream query;
    query << coapServerResource;
    REQUEST_DONE = false;
    PROMPT_USER = false;

    result = InvokeOCDoResource(query, OC_REST_POST, endpoint, postLEDPayload(),
                                ((qos == OC_HIGH_QOS) ? OC_HIGH_QOS : OC_LOW_QOS),
                                postReqCB, NULL, 0);
    if (OC_STACK_OK != result)
    {
        OIC_LOG(INFO, TAG, "POST call did not succeed");
    }

    return result;

}

//Initialize 'Change LED URI' request
int InitURIPostRequest(OCDevAddr *endpoint, OCQualityOfService qos) {

    OCStackResult result;
    OIC_LOG_V(INFO, TAG, "Executing %s", __func__);
    std::ostringstream query;
    query << coapServerResource;
    REQUEST_DONE = false;
    PROMPT_USER = false;

    result = InvokeOCDoResource(query, OC_REST_POST, endpoint, postURIPayload(),
                                ((qos == OC_HIGH_QOS) ? OC_HIGH_QOS : OC_LOW_QOS),
                                postReqCB, NULL, 0);
    if (OC_STACK_OK != result)
    {
        OIC_LOG(INFO, TAG, "POST call did not succeed");
    }

    return result;

}


static int requests(void) {
    int rmn_num = 0;

    if (endpoint == NULL) {
            printf("Please first discover resources\n");
            initialMenu();
            return 0;
        }

    while(!STOP && REQUEST_DONE && GET_DONE) {

        printf("\n\n");
        printf("1. Create (New LED)\n");
        printf("2. Retrieve (LED resource)\n");
        printf("3. Update (LED value)\n");
        printf("4. Update (URI)\n");
        printf("9. Initial Menu\n");
        printf("0. Exit\n\n");
        printf(">> Enter Menu Number: ");
        for(int ret=0; 1!=ret; ) {
            ret = scanf("%d", &rmn_num);
            for( ; 0x20<=getchar(); );
        }
        printf("\n");
        switch(rmn_num) {
            case 1:
                InitPutRequest(endpoint, OC_LOW_QOS);
                break;
            case 2:
                InitGetRequest(endpoint, OC_LOW_QOS);
                break;
            case 3:
                if(InitLEDPostRequest(endpoint, OC_LOW_QOS)) {
                    OIC_LOG(ERROR, TAG, "POST Request Error");
                }
                break;
            case 4:
                if(InitURIPostRequest(endpoint, OC_LOW_QOS)) {
                    OIC_LOG(ERROR, TAG, "POST Request Error");
                }
                break; 
            case 9:
                initialMenu();
                break;
            case 0:
                STOP = true;
                break;
            default:
                printf(">> Entered wrong number. Please enter again\n\n");
        }
        if(STOP){
            break;
        }
    }
    return 0;
}


static void initialMenu() {
    int mn_num = 0;
    while(!STOP && DISCOVERY_DONE && REQUEST_DONE && OT_DONE) {
        printf("\n");
        printf("1. Provision\n");
        printf("2. Discover Resources and Send Requests\n");
        printf("0. Exit\n\n");
        printf(">> Enter Menu Number: ");
        for(int ret=0; 1!=ret; ) {
            ret = scanf("%d", &mn_num);
            for( ; 0x20<=getchar(); );
        }
        printf("\n");
        switch(mn_num) {
            case 1:
                if(provisioning()) {
                    OIC_LOG(ERROR, TAG, "Provisioning Error");
                }
                break;
            case 2:
                if(discovery()) {
                    OIC_LOG(ERROR, TAG, "Discovery Error");
                }
                break;
            case 0:
                STOP = true;
                break;
            default:
                printf(">> Wrong Number. Please Enter Again\n\n");
                break;

        }
    }

}

//Function to get PIN
static void OC_CALL inputPinCB(OicUuid_t deviceId, char *pin, size_t len, void *context)
{
    OC_UNUSED(deviceId);
    OC_UNUSED(context);

    if(!pin || OXM_RANDOM_PIN_MIN_SIZE > len)
    {
        OIC_LOG(ERROR, TAG, "inputPinCB invalid parameters");
        return;
    }

    printf("   > INPUT PIN: ");
    for(int ret=0; 1!=ret; )
    {
        ret = scanf("%32s", pin);
        for( ; 0x20<=getchar(); );  // for removing overflow garbages
                                    // '0x20<=code' is character region
    }
}

int
main(void)
{   

    OCStackResult stack_res;
    OIC_LOG_V(DEBUG, TAG,
              "[%s] Initializing and registering persistent storage",
              __func__);
    OCPersistentStorage ps = {ClientFOpen, fread, fwrite, fclose, unlink};
    OCRegisterPersistentStorageHandler(&ps);
    OIC_LOG_V(DEBUG, TAG,
              "[%s] Initializing IoTivity stack for client_server",
              __func__);
    stack_res = OCInit(NULL, 0, OC_CLIENT_SERVER);
    if (stack_res != OC_STACK_OK)
    {
        OIC_LOG_V(ERROR, TAG,
                  "[%s] Failed to initialize IoTivity stack (%d): %s",
                  __func__,
                  stack_res,
                  decode_oc_stack_result(stack_res));
        return -1;
    }

    if (access(PRVN_DB_FILE_NAME, F_OK) == 0)
    {
        printf("************************************************************\n");
        printf("************Provisioning DB file already exists.************\n");
        printf("************************************************************\n");
    }
    else
    {
        printf("*************************************************************\n");
        printf("************No provisioning DB file, creating new************\n");
        printf("*************************************************************\n");
    }

    if(OC_STACK_OK != OCInitPM(PRVN_DB_FILE_NAME))
    {
        OIC_LOG(ERROR, TAG, "OC_PM init error");
        return -1;
    }

    SetInputPinWithContextCB(inputPinCB, NULL);

    initialMenu();

    signal(SIGINT, SIGINTHandlerCallBack);
    while (!STOP)
    {
        stack_res = OCProcess();
        if (stack_res != OC_STACK_OK)
        {
            OIC_LOG_V(ERROR, TAG,
                      "[%s] IoTivity stack process failure (%d): %s",
                      __func__,
                      stack_res,
                      decode_oc_stack_result(stack_res));
            return -1;
        }

        if (DISCOVERING) {
            sleep(1);
        }

        if (PROMPT_USER)
        {
            PROMPT_USER = false;
            initialMenu();
        }
    }

    OIC_LOG_V(INFO, TAG, "[%s] stopping IoTivity client...", __func__);
    stack_res = OCStop();
    if (stack_res != OC_STACK_OK)
    {
        OIC_LOG_V(ERROR, TAG,
                  "[%s] Failed to STOP IoTivity client (%d): %s",
                  __func__,
                  stack_res,
                  decode_oc_stack_result(stack_res));
        return -1;
    }

    return 0;
}


int parseClientResponse(OCClientResponse *clientResponse)
{
    OCResourcePayload *res = ((OCDiscoveryPayload *)clientResponse->payload)->resources;

    coapServerResource.clear();
    coapSecureResource = 0;

    while (res)
    {
        coapServerResource.assign(res->uri);
        //OIC_LOG_V(INFO, TAG, "Uri -- %s", coapServerResource.c_str());

        if (0 == strncmp(coapServerResource.c_str(), OIC_STD_URI_PREFIX, strlen(OIC_STD_URI_PREFIX)) ||
            0 == strncmp(coapServerResource.c_str(), "/introspection", strlen("/introspection")))
        {
            //OIC_LOG(INFO, TAG, "Skip resource");
            res = res->next;
            continue;
        }

        OCDevAddr *endpoint = &clientResponse->devAddr;
        if (res && res->eps)
        {
            endpoint->port = 0;
            OCEndpointPayload* eps = res->eps;
            while (NULL != eps)
            {
                if (eps->family & OC_FLAG_SECURE)
                {
#ifdef __WITH_TLS__
                    if (WithTcp && 0 == strcmp(eps->tps, COAPS_TCP_STR))
                    {
                        strncpy(endpoint->addr, eps->addr, sizeof(endpoint->addr) - 1);
                        endpoint->port = eps->port;
                        endpoint->flags = (OCTransportFlags)(eps->family | OC_SECURE);
                        endpoint->adapter = OC_ADAPTER_TCP;
                        coapSecureResource = 1;
                        //OIC_LOG_V(INFO, TAG, "TLS port: %d", endpoint->port);
                        break;
                    }
#endif
                    if (!WithTcp && 0 == strcmp(eps->tps, COAPS_STR))
                    {
                        strncpy(endpoint->addr, eps->addr, sizeof(endpoint->addr) - 1);
                        endpoint->port = eps->port;
                        endpoint->flags = (OCTransportFlags)(eps->family | OC_SECURE);
                        endpoint->adapter = OC_ADAPTER_IP;
                        coapSecureResource = 1;
                        //OIC_LOG_V(INFO, TAG, "DTLS port: %d", endpoint->port);
                    }
                }
                eps = eps->next;
            }
            if (!endpoint->port)
            {
                OIC_LOG(INFO, TAG, "Can not find secure port information.");
            }
        }

        //old servers support
        if (0 == coapSecureResource && res->secure)
        {
#ifdef __WITH_TLS__
            if (WithTcp)
            {
                endpoint->flags = (OCTransportFlags)(endpoint->flags | OC_SECURE);
                endpoint->adapter = OC_ADAPTER_TCP;
                endpoint->port = res->tcpPort;
                //OIC_LOG_V(INFO, TAG, "TLS port: %d", endpoint->port);
            }
            else
#endif
            {
                endpoint->port = res->port;
                endpoint->flags = (OCTransportFlags)(endpoint->flags | OC_SECURE);
                endpoint->adapter = OC_ADAPTER_IP;
                //OIC_LOG_V(INFO, TAG, "DTLS port: %d", endpoint->port);
            }
            coapSecureResource = 1;
        }

       // OIC_LOG_V(INFO, TAG, "Secure -- %s", coapSecureResource == 1 ? "YES" : "NO");

        // If we discovered a secure resource, exit from here
        if (coapSecureResource)
        {
            break;
        }

        res = res->next;
    }

    return 0;
}