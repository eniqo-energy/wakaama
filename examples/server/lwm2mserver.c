/*******************************************************************************
 *
 * Copyright (c) 2013, 2014 Intel Corporation and others.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v20.html
 * The Eclipse Distribution License is available at
 *    http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    David Navarro, Intel Corporation - initial API and implementation
 *    domedambrosio - Please refer to git log
 *    Simon Bernard - Please refer to git log
 *    Toby Jaffey - Please refer to git log
 *    Julien Vermillard - Please refer to git log
 *    Bosch Software Innovations GmbH - Please refer to git log
 *    Christian Renz - Please refer to git log
 *    Scott Bertin, AMETEK, Inc. - Please refer to git log
 *
 *******************************************************************************/

/*
 Copyright (c) 2013, 2014 Intel Corporation

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

     * Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
     * Neither the name of Intel Corporation nor the names of its contributors
       may be used to endorse or promote products derived from this software
       without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 THE POSSIBILITY OF SUCH DAMAGE.

 David Navarro <david.navarro@intel.com>

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
 * @brief Returns a string representation of the provided LwM2M version.
 * 
 * @param version The LwM2M version.
 * @returns A string representing the LwM2M version.
 */
static const char *prv_dump_version(lwm2m_version_t version) {
    switch (version) {
    case VERSION_MISSING:
        return "Missing";
    case VERSION_UNRECOGNIZED:
        return "Unrecognized";
    case VERSION_1_0:
        return "1.0";
    case VERSION_1_1:
        return "1.1";
    default:
        return "";
    }
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
    fprintf(stdout, "\tversion: \"%s\"\r\n", prv_dump_version(targetP->version));
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
 * @brief Callback function to handle notifications.
 * 
 * @param contextP    Pointer to the LwM2M context.
 * @param clientID    ID of the client.
 * @param uriP        Pointer to the URI structure.
 * @param count       Notification count.
 * @param block_info  Pointer to block information.
 * @param format      Media type format.
 * @param data        Pointer to data.
 * @param dataLength: Length of the data.
 * @param userData:   User data (unused).
 */
static void prv_notify_callback(lwm2m_context_t *contextP, uint16_t clientID, lwm2m_uri_t *uriP, int count,
                                block_info_t *block_info, lwm2m_media_type_t format, uint8_t *data, size_t dataLength,
                                void *userData) {
    /* unused parameters */
    (void)contextP;
    (void)userData;

    fprintf(stdout, "\r\nNotify from client #%d ", clientID);
    prv_printUri(uriP);
    fprintf(stdout, " number %d\r\n", count);

    output_data(stdout, block_info, format, data, dataLength, 1);

    fprintf(stdout, "\r\n> ");
    fflush(stdout);
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
 * @brief Wrapper for prv_update_client method.
 * 
 * @param lwm2mH    Pointer to the LwM2M context.
 * @param buffer    Data buffer containing information to be updated.
 * @param user_data User data (unused).
 */
static void prv_update_client(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
    /* unused parameter */
    (void)user_data;

    prv_do_write_client(buffer, lwm2mH, true);
}

/**
 * @brief Sets time-related attributes for the LwM2M client.
 * 
 * @param lwm2mH    Pointer to the LwM2M context.
 * @param buffer    Data buffer containing attribute information.
 * @param user_data User data (unused).
 */
static void prv_time_client(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
    uint16_t clientId;
    lwm2m_uri_t uri;
    char *end = NULL;
    int result;
    lwm2m_attributes_t attr; // Defined in liblwm2m.h, line: 676
    int nb;
    int value;

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

    memset(&attr, 0, sizeof(lwm2m_attributes_t));
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD | LWM2M_ATTR_FLAG_MAX_PERIOD;

    buffer = get_next_arg(end, &end);
    if (buffer[0] == 0)
        goto syntax_error;

    nb = sscanf(buffer, "%d", &value);
    if (nb != 1)
        goto syntax_error;
    if (value < 0)
        goto syntax_error;
    attr.minPeriod = value;

    buffer = get_next_arg(end, &end);
    if (buffer[0] == 0)
        goto syntax_error;

    nb = sscanf(buffer, "%d", &value);
    if (nb != 1)
        goto syntax_error;
    if (value < 0)
        goto syntax_error;
    attr.maxPeriod = value;

    if (!check_end_of_args(end))
        goto syntax_error;

    result = lwm2m_dm_write_attributes(lwm2mH, clientId, &uri, &attr, prv_result_callback, NULL);

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
 * @brief Sets value-related attributes for the LwM2M client.
 * 
 * @param lwm2mH    Pointer to the LwM2M context.
 * @param buffer    Data buffer containing attribute information.
 * @param user_data User data (unused).
 */
static void prv_attr_client(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
    uint16_t clientId;
    lwm2m_uri_t uri;
    char *end = NULL;
    int result;
    lwm2m_attributes_t attr; // Defined in liblwm2m.h, line: 676
    int nb;
    float value;

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

    memset(&attr, 0, sizeof(lwm2m_attributes_t));
    attr.toSet = LWM2M_ATTR_FLAG_LESS_THAN | LWM2M_ATTR_FLAG_GREATER_THAN;

    buffer = get_next_arg(end, &end);
    if (buffer[0] == 0)
        goto syntax_error;

    nb = sscanf(buffer, "%f", &value);
    if (nb != 1)
        goto syntax_error;
    attr.lessThan = value;

    buffer = get_next_arg(end, &end);
    if (buffer[0] == 0)
        goto syntax_error;

    nb = sscanf(buffer, "%f", &value);
    if (nb != 1)
        goto syntax_error;
    attr.greaterThan = value;

    buffer = get_next_arg(end, &end);
    if (buffer[0] != 0) {
        nb = sscanf(buffer, "%f", &value);
        if (nb != 1)
            goto syntax_error;
        attr.step = value;

        attr.toSet |= LWM2M_ATTR_FLAG_STEP;
    }

    if (!check_end_of_args(end))
        goto syntax_error;

    result = lwm2m_dm_write_attributes(lwm2mH, clientId, &uri, &attr, prv_result_callback, NULL);

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
 * @brief Clears attributes for the LwM2M client.
 * 
 * @param lwm2mH    Pointer to the LwM2M context.
 * @param buffer    Data buffer containing attribute information.
 * @param user_data User data (unused).
 */
static void prv_clear_client(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
    uint16_t clientId;
    lwm2m_uri_t uri;
    char *end = NULL;
    int result;
    lwm2m_attributes_t attr;

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

    memset(&attr, 0, sizeof(lwm2m_attributes_t));
    attr.toClear = LWM2M_ATTR_FLAG_LESS_THAN | LWM2M_ATTR_FLAG_GREATER_THAN | LWM2M_ATTR_FLAG_STEP |
                   LWM2M_ATTR_FLAG_MIN_PERIOD | LWM2M_ATTR_FLAG_MAX_PERIOD;

    buffer = get_next_arg(end, &end);
    if (!check_end_of_args(end))
        goto syntax_error;

    result = lwm2m_dm_write_attributes(lwm2mH, clientId, &uri, &attr, prv_result_callback, NULL);

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
 * @brief Creates a new object instance on the LwM2M client.
 * 
 * @param lwm2mH    Pointer to the LwM2M context.
 * @param buffer    Data buffer containing object instance information.
 * @param user_data User data (unused).
 */
static void prv_create_client(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
    uint16_t clientId;
    lwm2m_uri_t uri;
    char *end = NULL;
    int result;
    int64_t value;
    lwm2m_data_t *dataP = NULL;
    int size = 0;

    /* unused parameter */
    (void)user_data;

    // Get Client ID
    result = prv_read_id(buffer, &clientId);
    if (result != 1)
        goto syntax_error;

    // Get Uri
    buffer = get_next_arg(buffer, &end);
    if (buffer[0] == 0)
        goto syntax_error;

    result = lwm2m_stringToUri(buffer, end - buffer, &uri);
    if (result == 0)
        goto syntax_error;
    if (LWM2M_URI_IS_SET_RESOURCE(&uri))
        goto syntax_error;

    // Get Data to Post
    buffer = get_next_arg(end, &end);
    if (buffer[0] == 0)
        goto syntax_error;

    if (!check_end_of_args(end))
        goto syntax_error;

        // TLV

#ifdef LWM2M_SUPPORT_SENML_JSON
    if (size <= 0) {
        size = lwm2m_data_parse(&uri, (uint8_t *)buffer, end - buffer, LWM2M_CONTENT_SENML_JSON, &dataP);
    }
#endif
#ifdef LWM2M_SUPPORT_JSON
    if (size <= 0) {
        size = lwm2m_data_parse(&uri, (uint8_t *)buffer, end - buffer, LWM2M_CONTENT_JSON, &dataP);
    }
#endif
    /* Client dependent part   */

    if (size <= 0 && uri.objectId == 31024) {
        if (1 != sscanf(buffer, "%" PRId64, &value)) {
            fprintf(stdout, "Invalid value !");
            return;
        }

        size = 1;
        dataP = lwm2m_data_new(size);
        if (dataP == NULL) {
            fprintf(stdout, "Allocation error !");
            return;
        }
        lwm2m_data_encode_int(value, dataP);
        dataP->id = 1;
    }
    /* End Client dependent part*/

    if (size <= 0) {
        goto syntax_error;
    }

    if (LWM2M_URI_IS_SET_INSTANCE(&uri)) {
        /* URI is only allowed to have the object ID. Wrap the instance in an
         * object instance to get it to the client. */
        int count = size;
        lwm2m_data_t *subDataP = dataP;
        size = 1;
        dataP = lwm2m_data_new(size);
        if (dataP == NULL) {
            fprintf(stdout, "Allocation error !");
            lwm2m_data_free(count, subDataP);
            return;
        }
        lwm2m_data_include(subDataP, count, dataP);
        dataP->type = LWM2M_TYPE_OBJECT_INSTANCE;
        dataP->id = uri.instanceId;
        uri.instanceId = LWM2M_MAX_ID;
    }

    // Create
    result = lwm2m_dm_create(lwm2mH, clientId, &uri, size, dataP, prv_result_callback, NULL);
    lwm2m_data_free(size, dataP);

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
 * @brief Observes a resource on the LwM2M client.
 * 
 * @param lwm2mH    Pointer to the LwM2M context.
 * @param buffer    Data buffer containing observation information.
 * @param user_data User data (unused).
 */
static void prv_observe_client(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
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

    result = lwm2m_observe(lwm2mH, clientId, &uri, prv_notify_callback, NULL);

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
 * @brief Cancels an observation on the LwM2M client.
 * 
 * @param lwm2mH    Pointer to the LwM2M context.
 * @param buffer    Data buffer containing observation cancellation information.
 * @param user_data User data (unused).
 */
static void prv_cancel_client(lwm2m_context_t *lwm2mH, char *buffer, void *user_data) {
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

    result = lwm2m_observe_cancel(lwm2mH, clientId, &uri, prv_result_callback, NULL);

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
 * @brief Handles the SIGINT signal to quit the server.
 * 
 * @param signum Signal number.
 */
void handle_sigint(int signum) { g_quit = 2; }

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
                             {"update", "Write to a client with partial update.",
                              " update CLIENT# URI DATA\r\n"
                              "   CLIENT#: client number as returned by command 'list'\r\n"
                              "   URI: uri to write to such as /3, /3/0/2, /1024/11, /1024/0/1\r\n"
                              "   DATA: data to write. Must be a supported JSON format.\r\n"
                              "Result will be displayed asynchronously.",
                              prv_update_client, NULL},
                             {"time", "Write time-related attributes to a client.",
                              " time CLIENT# URI PMIN PMAX\r\n"
                              "   CLIENT#: client number as returned by command 'list'\r\n"
                              "   URI: uri to write attributes to such as /3, /3/0/2, /1024/11, /1024/0/1\r\n"
                              "   PMIN: Minimum period\r\n"
                              "   PMAX: Maximum period\r\n"
                              "Result will be displayed asynchronously.",
                              prv_time_client, NULL},
                             {"attr", "Write value-related attributes to a client.",
                              " attr CLIENT# URI LT GT [STEP]\r\n"
                              "   CLIENT#: client number as returned by command 'list'\r\n"
                              "   URI: uri to write attributes to such as /3/0/2, /1024/0/1\r\n"
                              "   LT: \"Less than\" value\r\n"
                              "   GT: \"Greater than\" value\r\n"
                              "   STEP: \"Step\" value\r\n"
                              "Result will be displayed asynchronously.",
                              prv_attr_client, NULL},
                             {"clear", "Clear attributes of a client.",
                              " clear CLIENT# URI\r\n"
                              "   CLIENT#: client number as returned by command 'list'\r\n"
                              "   URI: uri to clear attributes of such as /3, /3/0/2, /1024/11, /1024/0/1\r\n"
                              "Result will be displayed asynchronously.",
                              prv_clear_client, NULL},
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
                             {"create", "Create an Object instance.",
                              " create CLIENT# URI DATA\r\n"
                              "   CLIENT#: client number as returned by command 'list'\r\n"
                              "   URI: uri to which create the Object Instance such as /1024, /1024/45 \r\n"
                              "   DATA: data to initialize the new Object Instance (0-255 for object 31024 or any "
                              "supported JSON format) \r\n"
                              "Result will be displayed asynchronously.",
                              prv_create_client, NULL},
                             {"observe", "Observe from a client.",
                              " observe CLIENT# URI\r\n"
                              "   CLIENT#: client number as returned by command 'list'\r\n"
                              "   URI: uri to observe such as /3, /3/0/2, /1024/11\r\n"
                              "Result will be displayed asynchronously.",
                              prv_observe_client, NULL},
                             {"cancel", "Cancel an observe.",
                              " cancel CLIENT# URI\r\n"
                              "   CLIENT#: client number as returned by command 'list'\r\n"
                              "   URI: uri on which to cancel an observe such as /3, /3/0/2, /1024/11\r\n"
                              "Result will be displayed asynchronously.",
                              prv_cancel_client, NULL},

                             {"q", "Quit the server.", NULL, prv_quit, NULL},

                             COMMAND_END_LIST};

/**
 * @brief Starts the CoAP server.
 */
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
 * @brief Returns a test link.
 * 
 * @return Test link string.
 */
const char *testLink() {
    const char *mystr = "Hello from  Wakaama server source !!!";
    return mystr;
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
