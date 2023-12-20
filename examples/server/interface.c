#include <assert.h>
#include <node_api.h>

static napi_value Init(napi_env env, napi_value exports) {
    napi_value new_exports;
    napi_status status = napi_create_function(env, "hello", NAPI_AUTO_LENGTH, CreateFunction, NULL, &new_exports);
    assert(status == napi_ok);
    return new_exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)