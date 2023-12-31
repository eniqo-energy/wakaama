#include "func.h"
#include "lwm2mserver.h"
#include <assert.h>
#include <node_api.h>

napi_value Method(napi_env env, napi_callback_info args) {
    napi_value greeting;
    napi_status status;

    status = napi_create_string_utf8(env, "hello", NAPI_AUTO_LENGTH, &greeting);
    if (status != napi_ok)
        return NULL;
    return greeting;
}

napi_value Method2(napi_env env, napi_callback_info args) {
    napi_value greeting;
    napi_status status;

    status = napi_create_string_utf8(env, "hello2", NAPI_AUTO_LENGTH, &greeting);
    if (status != napi_ok)
        return NULL;
    return greeting;
}
napi_value random_header_wrapper(napi_env env, napi_callback_info args) {
    napi_value greeting;
    napi_status status;

    status = napi_create_string_utf8(env, c_hello(), NAPI_AUTO_LENGTH, &greeting);
    if (status != napi_ok)
        return NULL;
    return greeting;
}
napi_value wrapper(napi_env env, napi_callback_info args) {
    napi_value greeting;
    napi_status status;

    status = napi_create_string_utf8(env, testLink(), NAPI_AUTO_LENGTH, &greeting);
    if (status != napi_ok)
        return NULL;
    return greeting;
}
napi_value Init(napi_env env, napi_value exports) {
    napi_status status;
    napi_value fn;

    status = napi_create_function(env, NULL, 0, Method, NULL, &fn);
    if (status != napi_ok) {
        napi_throw_error(env, NULL, "Unable to wrap native function");
    }

    status = napi_set_named_property(env, exports, "hello", fn);
    if (status != napi_ok) {
        napi_throw_error(env, NULL, "Unable to populate exports");
    }

    status = napi_create_function(env, NULL, 0, wrapper, NULL, &fn);
    if (status != napi_ok) {
        napi_throw_error(env, NULL, "Unable to wrap native function");
    }

    status = napi_set_named_property(env, exports, "hello2", fn);
    if (status != napi_ok) {
        napi_throw_error(env, NULL, "Unable to populate exports");
    }

    status = napi_create_function(env, NULL, 0, random_header_wrapper, NULL, &fn);
    if (status != napi_ok) {
        napi_throw_error(env, NULL, "Unable to wrap random_header_wrapper native function");
    }

    status = napi_set_named_property(env, exports, "random", fn);
    if (status != napi_ok) {
        napi_throw_error(env, NULL, "Unable to populate exports");
    }
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)