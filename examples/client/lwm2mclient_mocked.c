#include "lwm2mclient.h"
#include "liblwm2m.h"
#include "commandline.h"
#ifdef WITH_TINYDTLS
#include "dtlsconnection.h"
#else
#include "connection.h"
#endif

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Define some constants
#define MOCK_SERVER_URI "coap://localhost:5683"
#define MOCK_CLIENT_NAME "mocklwm2mclient"
#define OBJ_COUNT 5

// Structure to hold client data
typedef struct
{
    lwm2m_object_t * securityObjP;
    lwm2m_object_t * serverObject;
    int sock;
    // Defined in wakaama/include/liblwm2m.h - Line 817
    lwm2m_context_t * lwm2mH;
    connection_t * connList;
    int addressFamily;
} client_data_t;

// Declare object array and client data
lwm2m_object_t *objArray[OBJ_COUNT];
client_data_t clientData;

// Mock functions
void *lwm2m_connect_server(uint16_t secObjInstID, void *userData) {
    // Simulate server connection
    printf("Connecting to server with instance ID %d\n", secObjInstID);
    return (void *)1; // Mock connection handle
}

void lwm2m_close_connection(void *sessionH, void *userData) {
    // Simulate closing the connection
    printf("Closing connection\n");
}

/*
 * Reads the value of a resource from a given object instance of an LwM2M client
 *
 * Parameters:
 * - context: The LwM2M context
 * - objectId: The ID of the object to read from
 * - instanceId: The ID of the object instance to read from
 * - resourceId: The ID of the resource to read
 *
 * Returns:
 * - 0 on success
 * - -1 on failure
 */
int lwm2m_data_read(lwm2m_context_t *context, uint8_t objectId, uint8_t instanceId, uint8_t resourceId) {
    int result = -1;
    // Perform the read operation
    if ((objectId == 3) & (instanceId == 0) & (resourceId == 0)) {
        result = 0;
    } else {
        printf("Failed to read resource value (error %d)\n", result);
        return -1;
    }
    return result;
}

// Handle read operation
void handle_read(lwm2m_context_t *context) {
    // Simulate reading from /3/0
    int result = lwm2m_data_read(context, 3, 0, 0);
    if (result == 0) {
        printf("Read request on /3/0 is successful");
    } else {
        printf("Read request on /3/0 failed\n");
    }
}

int lwm2m_write_handler(lwm2m_context_t *context, lwm2m_uri_t *uri, lwm2m_data_t *data) {
    printf("Mock write handler called\n");
    printf("Writing to object ID: %d, instance ID: %d, resource ID: %d\n", uri->objectId, uri->instanceId, uri->resourceId);
    printf("Data type: %d, Data length: %lu\n", data->type, (unsigned long)data->value.asBuffer.length);
    printf("Data value: %s\n", (char *)data->value.asBuffer.buffer);

    // Simulate a successful write operation
    return COAP_NO_ERROR;
}

void handle_write(lwm2m_context_t *context) {
    lwm2m_uri_t uri;
    lwm2m_data_t data;

    // Simulate writing to /3/0/1
    uri.objectId = 3;
    uri.instanceId = 0;
    uri.resourceId = 1;

    data.id = uri.resourceId;
    data.type = LWM2M_TYPE_STRING;
    const char *value = "new_value";
    data.value.asBuffer.buffer = (uint8_t *)value;
    data.value.asBuffer.length = strlen(value);

    int result = lwm2m_write_handler(context, &uri, &data);
    if (result == COAP_NO_ERROR) {
        printf("Write request on /3/0/1 successful\n");
    } else {
        printf("Write request on /3/0/1 failed\n");
    }
}

// Mock version of lwm2m_discover_handler
int lwm2m_discover_handler(lwm2m_context_t *context, lwm2m_uri_t *uri) {
    printf("Mock discover handler called\n");
    printf("Discovering object ID: %d, instance ID: %d, resource ID: %d\n",
           uri->objectId, uri->instanceId, uri->resourceId);

    // Simulate a successful discover operation
    return COAP_NO_ERROR;
}

// Handle discover operation
void handle_discover(lwm2m_context_t *context) {
    lwm2m_uri_t uri;

    // Simulate discovering /3
    uri.objectId = 3;
    uri.instanceId = 0;
    uri.resourceId = 222;

    int result = lwm2m_discover_handler(context, &uri);
    if (result == COAP_NO_ERROR) {
        printf("Discover request on /3 successful\n");
    } else {
        printf("Discover request on /3 failed\n");
    }
}


// Mock version of lwm2m_exec_handler
int lwm2m_exec_handler(lwm2m_context_t *context, lwm2m_uri_t *uri, lwm2m_data_t *data, void *userData, size_t numData) {
    // Simulate execution logic
    if (uri->objectId == 3 && uri->instanceId == 0 && uri->resourceId == 4) {
        // Simulate successful execution
        printf("Executing resource /%d/%d/%d\n", uri->objectId, uri->instanceId, uri->resourceId);
        return COAP_NO_ERROR;
    } else {
        // Simulate execution failure for other URIs
        printf("Failed to execute resource /%d/%d/%d\n", uri->objectId, uri->instanceId, uri->resourceId);
        return -1;
    }
}

// Function to handle exec operation
void handle_exec(lwm2m_context_t *context) {
    lwm2m_uri_t uri;
    lwm2m_data_t data;

    // Simulate executing /3/0/4
    uri.objectId = 3;
    uri.instanceId = 0;
    uri.resourceId = 4;

    int result = lwm2m_exec_handler(context, &uri, &data, NULL, 0);
    if (result == COAP_NO_ERROR) {
        printf("Exec request on /3/0/4 successful\n");
    } else {
        printf("Exec request on /3/0/4 failed\n");
    }
}

void lwm2m_start(void) {
    // Initialize LwM2M context
    lwm2m_context_t *lwm2mH = lwm2m_init(&clientData);
    if (lwm2mH == NULL) {
        fprintf(stderr, "lwm2m_init() failed\n");
        return;
    }
    // clientData.ctx = lwm2mH;
    clientData.lwm2mH = lwm2mH;

    // Initialize objects
    // objArray[0] = get_security_object(123, MOCK_SERVER_URI, NULL, NULL, 0, false);
    // objArray[1] = get_server_object(123, "U", 300, false);
    // objArray[2] = get_object_device();
    // objArray[3] = get_object_firmware();
    // objArray[4] = get_test_object();

    // Configure client
    // if (lwm2m_configure(lwm2mH, MOCK_CLIENT_NAME, NULL, NULL, OBJ_COUNT, objArray) != 0) {
    //     fprintf(stderr, "lwm2m_configure() failed\n");
    //     return;
    // }

    // Main loop
    while (1) {
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        int result = lwm2m_step(lwm2mH, &(tv.tv_sec));
        if (result != 0) {
            fprintf(stderr, "lwm2m_step() failed: 0x%X\n", result);
            break;
        }

        // Simulate receiving requests from the server
        handle_read(lwm2mH);
        handle_write(lwm2mH);
        handle_discover(lwm2mH);
        handle_exec(lwm2mH);

        sleep(5); // Wait for some time before the next loop iteration
    }
}

int main(void) {
    lwm2m_start();
    return 0;
}
