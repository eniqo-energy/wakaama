# Communication Flow between Mocked LwM2M Client and Server

## Introduction

This document describes the flow of communication between a mocked LwM2M
client and a server using the wakaama project. The client and server
communicate using the LwM2M protocol, which is designed for lightweight
machine-to-machine (M2M) communication.

## Client and Server Setup

### Client Setup

1.  Clone the wakaama eniqo repository
2.  Navigate to the mocked client directory - currently: cd wakaama/examples/client
3.  Generate build files using CMake: cmake . or cmake client
4.  Build the client: make
5.  Run the client: ./lwm2mclient

### Server Setup

1.  Clone the wakaama eniqo repository
2.  Navigate to the server directory - currently: cd wakaama/examples/server
3.  Generate build files using CMake: cmake .
4.  Build the server: make
5.  Run the server: ./lwm2mserver

## Communication Flow

### Read Operation

1.  The server sends a read request to the client for a specific
    resource.
2.  The client handles the read request using the `lwm2m_read_handler`
    function.
3.  The client returns the resource value to the server.

### Write Operation

1.  The server sends a write request to the client for a specific
    resource.
2.  The client handles the write request using the `lwm2m_write_handler`
    function.
3.  The client acknowledges the write request.

### Discover Operation

1.  The server sends a discover request to the client.
2.  The client handles the discover request using the
    `lwm2m_discover_handler` function.
3.  The client returns the discovery information to the server.

### Execute Operation

1.  The server sends an execute request to the client for a specific
    resource.
2.  The client handles the execute request using the
    `lwm2m_exec_handler` function.
3.  The client acknowledges the execute request.

## Error Handling and Debugging

### Common Issues

1. Missing include files: Ensure all necessary headers are included in the source files.
2. Linker errors: Verify that all symbols are defined and linked correctly.

### Resolving Errors

1. Implicit Declaration: Ensure function prototypes are declared before usage.
2. Undeclared Identifier: Check that all variables and functions are correctly declared and defined.
3. Linker Errors: Ensure all necessary object files are included in the linking stage.

## Conclusion

By following these steps and explanations, you can set up, run, and
understand the flow of communication between the mocked LwM2M client and
the server.

As the progress continues, this file will be updated
