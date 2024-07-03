 /*
 * lwm2mServer.c
 *
 *  Created on: 17.06.2024.
 *      Author:
 *   Copyright:
 */
#include "liblwm2m.h"

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

#include "commandline.h"
#include "connection.h"
#include "lwm2mserver.h"

// Maximum size of a information packet in bytes
// Used for retrieving the data received
#define MAX_PACKET_SIZE 2048

// Flag indicating program termination
// Possible values: 
//     0 - Default value
//     1 - Terminate the program forcefully
//     2 - Initiation of a graceful shutdown
static int g_quit = 0;

/**
 * @brief Prints an error message based on the provided status.
 * 
 * @param status The status code representing the error.
 */
static void prv_print_error(uint8_t status) {
    fprintf(stdout, "Error: ");
    print_status(stdout, status);
    fprintf(stdout, "\r\n");
}

/**
 * @brief Handles the SIGINT signal to quit the server.
 * 
 * @param signum Signal number.
 */
void handle_sigint(int signum) { g_quit = 2; }

/**
 * @brief Prints a URI.
 * 
 * @param uriP Pointer to the URI structure.
 */
static void prv_printUri(const lwm2m_uri_t *uriP) {
    fprintf(stdout, "/%d", uriP->objectId);
    if (LWM2M_URI_IS_SET_INSTANCE(uriP))
        fprintf(stdout, "/%d", uriP->instanceId);
    else if (LWM2M_URI_IS_SET_RESOURCE(uriP))
        fprintf(stdout, "/");
    if (LWM2M_URI_IS_SET_RESOURCE(uriP))
        fprintf(stdout, "/%d", uriP->resourceId);
#ifndef LWM2M_VERSION_1_0
    else if (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP))
        fprintf(stdout, "/");
    if (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP))
        fprintf(stdout, "/%d", uriP->resourceInstanceId);
#endif
}

/**
 * @brief Reads an ID of the client from a buffer.
 * 
 * @param buffer Buffer containing the ID.
 * @param idP    Pointer to store the read ID.
 * @returns The number of items successfully read.
 */
static int prv_read_id(char *buffer, uint16_t *idP) {
    int nb;
    int value;

    nb = sscanf(buffer, "%d", &value);
    if (nb == 1) {
        if (value < 0 || value > LWM2M_MAX_ID) {
            nb = 0;
        } else {
            *idP = value;
        }
    }
    return nb;
}

/**
 * @brief Prints the binding mode of a client.
 * 
 * @param binding The binding mode of the client.
 */
static void prv_dump_binding(lwm2m_binding_t binding) {
    if (BINDING_UNKNOWN == binding) {
        fprintf(stdout, "\tbinding: \"Not specified\"\r\n");
    } else {
        const struct bindingTable {
            lwm2m_binding_t binding;
            const char *text;
        } bindingTable[] = {
            {BINDING_U, "UDP"},    {BINDING_T, "TCP"},        {BINDING_S, "SMS"},
            {BINDING_N, "Non-IP"}, {BINDING_Q, "queue mode"},
        };
        size_t i;
        bool oneSeen = false;
        fprintf(stdout, "\tbinding: \"");
        for (i = 0; i < sizeof(bindingTable) / sizeof(bindingTable[0]); i++) {
            if ((binding & bindingTable[i].binding) != 0) {
                if (oneSeen) {
                    fprintf(stdout, ", %s", bindingTable[i].text);
                } else {
                    fprintf(stdout, "%s", bindingTable[i].text);
                    oneSeen = true;
                }
            }
        }
        fprintf(stdout, "\"\r\n");
    }
}

/**
 * @brief Prints details of a client.
 * 
 * @param targetP Pointer to the client structure.
 */
static void prv_dump_client(lwm2m_client_t *targetP) {
    lwm2m_client_object_t *objectP;

    fprintf(stdout, "Client #%d:\r\n", targetP->internalID);
    fprintf(stdout, "\tname: \"%s\"\r\n", targetP->name);
    prv_dump_binding(targetP->binding);
    if (targetP->msisdn)
        fprintf(stdout, "\tmsisdn: \"%s\"\r\n", targetP->msisdn);
    if (targetP->altPath)
        fprintf(stdout, "\talternative path: \"%s\"\r\n", targetP->altPath);
    fprintf(stdout, "\tlifetime: %d sec\r\n", targetP->lifetime);
    fprintf(stdout, "\tobjects: ");
    for (objectP = targetP->objectList; objectP != NULL; objectP = objectP->next) {
        if (objectP->instanceList == NULL) {
            if (objectP->versionMajor != 0 || objectP->versionMinor != 0) {
                fprintf(stdout, "/%d (%u.%u), ", objectP->id, objectP->versionMajor, objectP->versionMinor);
            } else {
                fprintf(stdout, "/%d, ", objectP->id);
            }
        } else {
            lwm2m_list_t *instanceP;

            if (objectP->versionMajor != 0 || objectP->versionMinor != 0) {
                fprintf(stdout, "/%d (%u.%u), ", objectP->id, objectP->versionMajor, objectP->versionMinor);
            }

            for (instanceP = objectP->instanceList; instanceP != NULL; instanceP = instanceP->next) {
                fprintf(stdout, "/%d/%d, ", objectP->id, instanceP->id);
            }
        }
    }
    fprintf(stdout, "\r\n");
}

/**
 * @brief Callback function to handle result of an operation.
 * 
 * @param contextP    Pointer to the LwM2M context.
 * @param clientID    ID of the client.
 * @param uriP        Pointer to the URI structure.
 * @param status      Status of the operation.
 * @param block_info  Pointer to block information.
 * @param format      Media type format.
 * @param data        Pointer to data.
 * @param dataLength: Length of the data.
 * @param userData:   User data (unused).
 */
static void prv_result_callback(lwm2m_context_t *contextP, uint16_t clientID, lwm2m_uri_t *uriP, int status,
                                block_info_t *block_info, lwm2m_media_type_t format, uint8_t *data, size_t dataLength,
                                void *userData) {
    /* unused parameters */
    (void)contextP;
    (void)userData;

    fprintf(stdout, "\r\nClient #%d ", clientID);
    prv_printUri(uriP);
    fprintf(stdout, " : ");
    print_status(stdout, status);
    fprintf(stdout, "\r\n");

    output_data(stdout, block_info, format, data, dataLength, 1);

    fprintf(stdout, "\r\n> ");
    fflush(stdout);
}


/**
 * @brief Prints details of all registered clients.
 * 
 * @param lwm2mH    Pointer to the LwM2M context.
 * @param buffer    Buffer for output.
 * @param user_data User data (unused).
 */
static void prv_output_clients(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
    lwm2m_client_t *targetP;

    /* unused parameter */
    (void)user_data;

    targetP = lwm2mH->clientList;

    if (targetP == NULL) {
        fprintf(stdout, "No client.\r\n");
        return;
    }

    for (targetP = lwm2mH->clientList; targetP != NULL; targetP = targetP->next) {
        prv_dump_client(targetP);
    }
}


/**
 * @brief Discovers resources of a client.
 * 
 * @param lwm2mH    Pointer to the LwM2M context.
 * @param buffer    Buffer containing client data.
 * @param user_data User data (unused).
 */
static void prv_discover_client(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
    uint16_t clientId;
    lwm2m_uri_t uri;
    char *end = NULL;
    int result;

    /* unused parameter */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1)
        goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0)
        goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0)
        goto syntax_error;

    if (!check_end_of_args(end))
        goto syntax_error;

    // Performing Discovering of client based of its client ID.
    // Creates, adjusts and sends get request to discover client.
    // Returns 404 or 500 response if there are issues with finding the client,
    // creating the transaction, or allocating memory for custom data.
    result = lwm2m_dm_discover(lwm2mH, clientId, &uri, prv_result_callback, NULL);

    if (result == 0) {
        fprintf(stdout, "OK");
    } else {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}


/**
 * @brief Deletes an object instance on the LwM2M client.
 * 
 * @param lwm2mH    Pointer to the LwM2M context.
 * @param buffer    Data buffer containing object instance information.
 * @param user_data User data (unused).
 */
static void prv_delete_client(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
    uint16_t clientId;
    lwm2m_uri_t uri;
    char *end = NULL;
    int result;

    /* unused parameter */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1)
        goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0)
        goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0)
        goto syntax_error;

    if (!check_end_of_args(end))
        goto syntax_error;

    result = lwm2m_dm_delete(lwm2mH, clientId, &uri, prv_result_callback, NULL);

    if (result == 0) {
        fprintf(stdout, "OK");
    } else {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}


/**
 * @brief Callback function to quit the LwM2M server. 
 * 
 * @param lwm2mH    Pointer to the LwM2M context (unused).
 * @param buffer    Pointer to the data buffer.
 * @param user_data User data (unused).
 */
static void prv_quit(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
    /* unused parameters */
    (void)lwm2mH;
    (void)user_data;

    // Set the quit flag to terminate the program
    g_quit = 1;
}


/**
 * @brief Reads data from a client.
 * 
 * @param lwm2mH    Pointer to the LwM2M context.
 * @param buffer    Buffer containing client data.
 * @param user_data User data (unused).
 */
static void prv_read_client(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
    uint16_t clientId;
    lwm2m_uri_t uri;
    char *end = NULL;
    int result;

    /* unused parameters */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1)
        goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0)
        goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0)
        goto syntax_error;

    if (!check_end_of_args(end))
        goto syntax_error;

    result = lwm2m_dm_read(lwm2mH, clientId, &uri, prv_result_callback, NULL);

    if (result == 0) {
        fprintf(stdout, "OK");
    } else {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}


/**
 * @brief Writes data to a client.
 * 
 * @param buffer        Buffer containing client data.
 * @param lwm2mH        Pointer to the LwM2M context.
 * @param partialUpdate Flag indicating whether it's a partial update.
 */
static void prv_do_write_client(char *buffer, lwm2m_context_t *lwm2mH, bool partialUpdate) {
    uint16_t clientId;
    lwm2m_uri_t uri;
    lwm2m_data_t *dataP = NULL;
    int count = 0;
    char *end = NULL;
    int result;

    result = prv_read_id(buffer, &clientId);
    if (result != 1)
        goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0)
        goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0)
        goto syntax_error;

    buffer = get_next_arg(end, &end);
    if (buffer[0] == 0)
        goto syntax_error;

    if (!check_end_of_args(end))
        goto syntax_error;

#ifdef LWM2M_SUPPORT_SENML_JSON
    if (count <= 0) {
        count = lwm2m_data_parse(&uri, (uint8_t *)buffer, end - buffer, LWM2M_CONTENT_SENML_JSON, &dataP);
    }
#endif
#ifdef LWM2M_SUPPORT_JSON
    if (count <= 0) {
        count = lwm2m_data_parse(&uri, (uint8_t *)buffer, end - buffer, LWM2M_CONTENT_JSON, &dataP);
    }
#endif
    if (count > 0) {
        lwm2m_client_t *clientP = NULL;
        clientP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)lwm2mH->clientList, clientId);
        if (clientP != NULL) {
            lwm2m_media_type_t format = clientP->format;
            uint8_t *serialized;
            int length = lwm2m_data_serialize(&uri, count, dataP, &format, &serialized);
            if (length > 0) {
                result = lwm2m_dm_write(lwm2mH, clientId, &uri, format, serialized, length, partialUpdate,
                                        prv_result_callback, NULL);
                lwm2m_free(serialized);
            } else {
                result = COAP_500_INTERNAL_SERVER_ERROR;
            }
        } else {
            result = COAP_404_NOT_FOUND;
        }
        lwm2m_data_free(count, dataP);
    } else if (!partialUpdate) {
        result = lwm2m_dm_write(lwm2mH, clientId, &uri, LWM2M_CONTENT_TEXT, (uint8_t *)buffer, end - buffer,
                                partialUpdate, prv_result_callback, NULL);
    } else {
        goto syntax_error;
    }

    if (result == 0) {
        fprintf(stdout, "OK");
    } else {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}

/**
 * @brief Wrapper for prv_do_write_client method.
 * 
 * @param lwm2mH    Pointer to the LwM2M context.
 * @param buffer    Data buffer containing information to be written.
 * @param user_data User data (unused).
 */
static void prv_write_client(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
    /* unused parameter */
    (void)user_data;

    prv_do_write_client(buffer, lwm2mH, false);
}


/**
 * @brief Callback function for monitoring client registrations, updates, and unregistrations.
 * 
 * @param lwm2mH     Pointer to the LwM2M context.
 * @param clientID   ID of the client.
 * @param uriP       Pointer to the URI structure.
 * @param status     Status of the operation.
 * @param block_info Pointer to block information.
 * @param format     Media format of the data.
 * @param data       Pointer to the data.
 * @param dataLength Length of the data.
 * @param userData   User data (unused).
 */
static void prv_monitor_callback(lwm2m_context_t *lwm2mH, uint16_t clientID, lwm2m_uri_t *uriP, int status,
                                 block_info_t *block_info, lwm2m_media_type_t format, uint8_t *data, size_t dataLength,
                                 void *userData) {
    lwm2m_client_t *targetP;

    /* unused parameter */
    (void)userData;

    switch (status) {
    case COAP_201_CREATED:
        fprintf(stdout, "\r\nNew client #%d registered.\r\n", clientID);

        targetP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)lwm2mH->clientList, clientID);

        prv_dump_client(targetP);
        break;

    case COAP_202_DELETED:
        fprintf(stdout, "\r\nClient #%d unregistered.\r\n", clientID);
        break;

    case COAP_204_CHANGED:
        fprintf(stdout, "\r\nClient #%d updated.\r\n", clientID);

        targetP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)lwm2mH->clientList, clientID);

        prv_dump_client(targetP);
        break;

    default:
        fprintf(stdout, "\r\nMonitor callback called with an unknown status: %d.\r\n", status);
        break;
    }

    fprintf(stdout, "\r\n> ");
    fflush(stdout);
}

/**
 * @brief Executes a command on the LwM2M client.
 * 
 * @param lwm2mH    Pointer to the LwM2M context.
 * @param buffer    Data buffer containing command information.
 * @param user_data User data (unused).
 */
static void prv_exec_client(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
    uint16_t clientId;
    lwm2m_uri_t uri;
    char *end = NULL;
    int result;

    /* unused parameter */
    (void)user_data;

    result = prv_read_id(buffer, &clientId);
    if (result != 1)
        goto syntax_error;

    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0)
        goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0)
        goto syntax_error;

    buffer = get_next_arg(end, &end);

    if (buffer[0] == 0) {
        result = lwm2m_dm_execute(lwm2mH, clientId, &uri, 0, NULL, 0, prv_result_callback, NULL);
    } else {
        if (!check_end_of_args(end))
            goto syntax_error;

        result = lwm2m_dm_execute(lwm2mH, clientId, &uri, LWM2M_CONTENT_TEXT, (uint8_t *)buffer, end - buffer,
                                  prv_result_callback, NULL);
    }

    if (result == 0) {
        fprintf(stdout, "OK");
    } else {
        prv_print_error(result);
    }
    return;

syntax_error:
    fprintf(stdout, "Syntax error !");
}


/**
 * @brief Prints usage information for the server.
 */
void print_usage(void) {
    fprintf(stderr, "Usage: lwm2mserver [OPTION]\r\n");
    fprintf(stderr, "Launch a LWM2M server on localhost.\r\n\n");
    fprintf(stdout, "Options:\r\n");
    fprintf(stdout, "  -4\t\tUse IPv4 connection. Default: IPv6 connection\r\n");
    fprintf(stdout, "  -l PORT\tSet the local UDP port of the Server. Default: " LWM2M_STANDARD_PORT_STR "\r\n");
    fprintf(stdout, "  -S BYTES\tCoAP block size. Options: 16, 32, 64, 128, 256, 512, 1024. Default: %" PRIu16 "\r\n",
            (uint16_t)LWM2M_COAP_DEFAULT_BLOCK_SIZE);
    fprintf(stdout, "\r\n");
}

/**
 * @brief Array containing command descriptions.
 */
command_desc_t commands[] = {{"list", "List registered clients.", NULL, prv_output_clients, NULL},
                             {"read", "Read from a client.",
                              " read CLIENT# URI\r\n"
                              "   CLIENT#: client number as returned by command 'list'\r\n"
                              "   URI: uri to read such as /3, /3/0/2, /1024/11, /1024/0/1\r\n"
                              "Result will be displayed asynchronously.",
                              prv_read_client, NULL},
                             {"disc", "Discover resources of a client.",
                              " disc CLIENT# URI\r\n"
                              "   CLIENT#: client number as returned by command 'list'\r\n"
                              "   URI: uri to discover such as /3, /3/0/2, /1024/11, /1024/0/1\r\n"
                              "Result will be displayed asynchronously.",
                              prv_discover_client, NULL},
                             {"write", "Write to a client.",
                              " write CLIENT# URI DATA\r\n"
                              "   CLIENT#: client number as returned by command 'list'\r\n"
                              "   URI: uri to write to such as /3, /3/0/2, /1024/11, /1024/0/1\r\n"
                              "   DATA: data to write. Text or a supported JSON format.\r\n"
                              "Result will be displayed asynchronously.",
                              prv_write_client, NULL},
                             {"exec", "Execute a client resource.",
                              " exec CLIENT# URI\r\n"
                              "   CLIENT#: client number as returned by command 'list'\r\n"
                              "   URI: uri of the resource to execute such as /3/0/2\r\n"
                              "Result will be displayed asynchronously.",
                              prv_exec_client, NULL},
                             {"del", "Delete a client Object instance.",
                              " del CLIENT# URI\r\n"
                              "   CLIENT#: client number as returned by command 'list'\r\n"
                              "   URI: uri of the instance to delete such as /1024/11\r\n"
                              "Result will be displayed asynchronously.",
                              prv_delete_client, NULL},
                             {"q", "Quit the server.", NULL, prv_quit, NULL},
                             COMMAND_END_LIST};

void startCoapServer() {
    int sock;                                         // Socket file descriptor.
    const char *localPort = LWM2M_STANDARD_PORT_STR;  // Port number for CoAP server: 5683.
    int addressFamily = AF_INET6;                     // Address family for socket.
    lwm2m_context_t *lwm2mH = NULL;                   // LwM2M context pointer.
    connection_t *connList = NULL;                    // List of connections.
    struct timeval tv;                                // Timeout value for select().
    int result;                                       // Result of operations.
    fd_set readfds;                                   // Set of file descriptors for select().

    // Open socket for CoAP server.
    sock = create_socket(localPort, addressFamily);
    if (sock < 0) {
        fprintf(stderr, "Error opening socket: %d\r\n", errno);
        // return -1;
    }

    // Initialize LwM2M context.
    lwm2mH = lwm2m_init(NULL);
    if (NULL == lwm2mH) {
        fprintf(stderr, "lwm2m_init() failed\r\n");
        // return -1;
    }

    // Set signal handler for SIGINT (Ctrl+C).
    signal(SIGINT, handle_sigint);

    fprintf(stdout, "> ");
    fflush(stdout);

    // Set callback function monitoring of client registrations, updates, and unregistrations.
    lwm2m_set_monitoring_callback(lwm2mH, prv_monitor_callback, NULL);

    // Main loop, iterates until quit flag is set
    while (0 == g_quit) {
        FD_ZERO(&readfds);              // Initialize file descriptor set.
        FD_SET(sock, &readfds);         // Add socket to set.
        FD_SET(STDIN_FILENO, &readfds); // Add stdin to set.

        tv.tv_sec = 60;  // Timeout for select() (60 seconds).
        tv.tv_usec = 0;

        // Perform LwM2M processing step and adjust timeout to the max time interval to wait.
        // Returns 0 if everything is okay, other value if not
        result = lwm2m_step(lwm2mH, &(tv.tv_sec));
        // Check for error
        if (result != 0) {
            fprintf(stderr, "lwm2m_step() failed: 0x%X\r\n", result);
            // return -1;
        }

        // Wait for activity on sockets or stdin or timeout.
        // Defined in select.h, line: 102
        result = select(FD_SETSIZE, &readfds, 0, 0, &tv);
        // Check for error
        if (result < 0) {
            if (errno != EINTR) {
                fprintf(stderr, "Error in select(): %d\r\n", errno);
            }
        } else if (result > 0) {
            uint8_t buffer[MAX_PACKET_SIZE];
            ssize_t numBytes;
            // Check if data is available on socket.
            if (FD_ISSET(sock, &readfds)) {
                struct sockaddr_storage addr;
                socklen_t addrLen;

                addrLen = sizeof(addr);
                // Receive data from socket.
                numBytes = recvfrom(sock, buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&addr, &addrLen);

                // Check for error and does size of packet is bigger than max packet size - 2048 bytes
                if (numBytes == -1) {
                    fprintf(stderr, "Error in recvfrom(): %d\r\n", errno);
                } else if (numBytes >= MAX_PACKET_SIZE) {
                    fprintf(stderr, "Received packet >= MAX_PACKET_SIZE\r\n");
                } else {
                    // Packet received
                    char s[INET6_ADDRSTRLEN]; // Buffer to hold the IP address
                    in_port_t port;           // Variable to hold the port number
                    connection_t *connP;      // Pointer to a connection structure

                    s[0] = 0;                 // Initialize the string buffer
                    // Checks if address is IPv4 or IPv6
                    if (AF_INET == addr.ss_family) {
                        // Cast the generic sockaddr to sockaddr_in (IPv4)
                        struct sockaddr_in *saddr = (struct sockaddr_in *)&addr;
                        // Convert the network address to a presentation format
                        inet_ntop(saddr->sin_family, &saddr->sin_addr, s, INET6_ADDRSTRLEN);
                        port = saddr->sin_port; // Get the port number
                    } else if (AF_INET6 == addr.ss_family) {
                        // Cast the generic sockaddr to sockaddr_in6 (IPv6)
                        struct sockaddr_in6 *saddr = (struct sockaddr_in6 *)&addr;
                        // Convert the network address to a presentation format
                        inet_ntop(saddr->sin6_family, &saddr->sin6_addr, s, INET6_ADDRSTRLEN);
                        port = saddr->sin6_port;
                    }
                    // Print information about the received packet
                    fprintf(stderr, "%zd bytes received from [%s]:%hu\r\n", numBytes, s, ntohs(port));
                    output_buffer(stderr, buffer, (size_t)numBytes, 0);

                    // Find or create a connection structure associated with the sender
                    connP = connection_find(connList, &addr, addrLen);
                    if (connP == NULL) {
                        // If the connection does not exist, create a new one
                        connP = connection_new_incoming(connList, sock, (struct sockaddr *)&addr, addrLen);
                        if (connP != NULL) {
                            // If the connection creation is successful, update the connection list
                            connList = connP;
                        }
                    }
                    if (connP != NULL) {
                        // Valid connection found
                        // Dispatch the received packet using the LwM2M protocol
                        lwm2m_handle_packet(lwm2mH, buffer, (size_t)numBytes, connP);
                    }
                }
            // Since data is not available on socket, check if it is available on stdin.
            } else if (FD_ISSET(STDIN_FILENO, &readfds)) {
                char *line = NULL;
                size_t bufLen = 0;

                // Read command from stdin.
                numBytes = getline(&line, &bufLen, stdin);

                if (numBytes > 1) {
                    line[numBytes] = 0;
                    // Handle commands received in buffer.
                    // If command is unknown, print unknown cmd.
                    handle_command(lwm2mH, commands, line);
                    fprintf(stdout, "\r\n");
                }
                if (g_quit == 0) {
                    // if flag for quitting is not raised, print prompt
                    fprintf(stdout, "> ");
                    // Force the contents of the buffer to be written to the output device immediately.
                    fflush(stdout);
                } else {
                    fprintf(stdout, "\r\n");
                }
                // Free allocated memory for command line.
                lwm2m_free(line);
            }
        }
    }
    // Clean up resources.
    lwm2m_close(lwm2mH);       // Close LwM2M context
    close(sock);               // Close socket
    connection_free(connList); // Free list of connections
}


/**
 * @brief Main function.
 * 
 * @param argc Argument count - number of arguments.
 * @param argv Argument vector.
 * @return Execution status.
 */
int main(int argc, char *argv[]) {
    // int addressFamily = AF_INET6;
    int opt;
    // const char *localPort = LWM2M_STANDARD_PORT_STR;
    opt = 1;
    while (opt < argc) {
        if (argv[opt] == NULL || argv[opt][0] != '-' || argv[opt][2] != 0) {
            print_usage();
            return 0;
        }
        switch (argv[opt][1]) {
        case '4':
            // addressFamily = AF_INET;
            break;
        case 'l':
            opt++;
            if (opt >= argc) {
                print_usage();
                return 0;
            }
            // localPort = argv[opt];
            break;
        case 'S':
            opt++;
            if (opt >= argc) {
                print_usage();
                return 0;
            }
            uint16_t coap_block_size_arg;
            if (1 == sscanf(argv[opt], "%" SCNu16, &coap_block_size_arg) &&
                lwm2m_set_coap_block_size(coap_block_size_arg)) {
                break;
            } else {
                print_usage();
                return 0;
            }
        default:
            print_usage();
            return 0;
        }
        opt += 1;
    }
    startCoapServer();
    return 0;
}