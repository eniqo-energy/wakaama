#include "func.h"
#include <assert.h>
#include <node_api.h>

// char *c_hello() {
//     char *mystr = "Hello World!\n";
//     return mystr;
// }

// static napi_value MyFunction(napi_env env, napi_callback_info info) {
//     napi_status status;

//     napi_value str;
//     status = napi_create_string_utf8(env, c_hello(), NAPI_AUTO_LENGTH, &str);
//     assert(status == napi_ok);

//     return str;
// }

// static napi_value Init(napi_env env, napi_value exports) {
//     napi_value new_exports;
//     napi_status status = napi_create_function(env, "f1", NAPI_AUTO_LENGTH, MyFunction, NULL, &new_exports);
//     assert(status == napi_ok);
//     return new_exports;
// }

static napi_value MyFunction2(napi_env env, napi_callback_info info) {
    napi_status status;

    napi_value str;
    status = napi_create_string_utf8(env, c_hello2(), NAPI_AUTO_LENGTH, &str);
    assert(status == napi_ok);

    return str;
}

static napi_value Init(napi_env env, napi_value exports) {
    napi_value new_exports;
    napi_status status = napi_create_function(env, "f1", NAPI_AUTO_LENGTH, MyFunction2, NULL, &new_exports);
    assert(status == napi_ok);
    return new_exports;
}
NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)