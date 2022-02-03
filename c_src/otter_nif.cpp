#include <dlfcn.h>
#include <erl_nif.h>
#include "nif_utils.hpp"

#ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wunused-variable"
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif

template<typename R>
struct erlang_nif_res {
    R val;
    static ErlNifResourceType * type;
};
template<typename R> ErlNifResourceType * erlang_nif_res<R>::type = nullptr;

template<typename R>
int alloc_resource(erlang_nif_res<R> **res) {
    *res = (erlang_nif_res<R> *)enif_alloc_resource(erlang_nif_res<R>::type, sizeof(erlang_nif_res<R>));
    return (*res != nullptr);
}

using OtterHandle = erlang_nif_res<void *>;
static void destruct_otter_handle(ErlNifEnv *env, void *args) {
}

static ERL_NIF_TERM otter_dlopen(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    if (argc != 2) return enif_make_badarg(env);

    int mode = 0;
    if (erlang::nif::get(env, argv[1], &mode)) {
        std::string path;
        const char * c_path = nullptr;
        if (erlang::nif::get(env, argv[0], path)) {
            c_path = path.c_str();
        }
        void * handle = dlopen(c_path, mode);
        if (handle != nullptr) {
            OtterHandle * res;
            if (alloc_resource(&res)) {
                res->val = handle;
                ERL_NIF_TERM ret = enif_make_resource(env, res);
                enif_release_resource(res);
                return erlang::nif::ok(env, ret);
            } else {
                return erlang::nif::error(env, "cannot allocate memory for resource");
            }
        } else {
            return erlang::nif::error(env, dlerror());
        }
    } else {
        return erlang::nif::error(env, "cannot get mode");
    }
}

static ERL_NIF_TERM otter_dlclose(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    if (argc != 1) return enif_make_badarg(env);

    OtterHandle * res;
    if (enif_get_resource(env, argv[0], OtterHandle::type, (void **)&res)) {
        void * handle = res->val;
        if (handle != nullptr) {
            int ret = dlclose(handle);
            if (ret == 0) {
                return erlang::nif::ok(env);
            } else {
                return erlang::nif::error(env, dlerror());
            }
        } else {
            return erlang::nif::error(env, "resource has an invalid handle");
        }
    } else {
        return erlang::nif::error(env, "cannot get mode");
    }
}

static int on_load(ErlNifEnv* env, void**, ERL_NIF_TERM)
{
    ErlNifResourceType *rt;
    rt = enif_open_resource_type(env, "Elixir.Otter.Nif", "OtterHandle", destruct_otter_handle, ERL_NIF_RT_CREATE, NULL);                                                             \
    if (!rt) return -1;
    erlang_nif_res<void *>::type = rt;
    return 0;
}

static int on_reload(ErlNifEnv*, void**, ERL_NIF_TERM)
{
    return 0;
}

static int on_upgrade(ErlNifEnv*, void**, void**, ERL_NIF_TERM)
{
    return 0;
}

static ErlNifFunc nif_functions[] = {
    {"dlopen", 2, otter_dlopen, 0},
    {"dlclose", 1, otter_dlclose, 0},
};

ERL_NIF_INIT(Elixir.Otter.Nif, nif_functions, on_load, on_reload, on_upgrade, NULL);

#if defined(__GNUC__)
#  pragma GCC visibility push(default)
#endif
