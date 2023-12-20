#include <assert.h>
#include <node_api.h>

static napi_value MyFunction(napi_env env, napi_callback_info info) {
    napi_status status;

    napi_value str;
    status = napi_create_string_utf8(env, "hello world from C!!!", NAPI_AUTO_LENGTH, &str);
    assert(status == napi_ok);

    return str;
}

static napi_value CreateFunction(napi_env env, napi_callback_info info) {
    napi_status status;

    napi_value fn;
    status = napi_create_function(env, "theFunction", NAPI_AUTO_LENGTH, MyFunction, NULL, &fn);
    assert(status == napi_ok);

    return fn;
}

static napi_value Init(napi_env env, napi_value exports) {
    napi_value new_exports;
    napi_status status = napi_create_function(env, "hello", NAPI_AUTO_LENGTH, MyFunction, NULL, &new_exports);
    assert(status == napi_ok);
    return new_exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)