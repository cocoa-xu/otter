#include <dlfcn.h>
#include <erl_nif.h>
#include <ffi.h>
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
using OtterSymbol = erlang_nif_res<void *>;
static void destruct_otter_handle(ErlNifEnv *env, void *args) {
}

static ERL_NIF_TERM otter_dlopen(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    if (argc != 2) return enif_make_badarg(env);

    int mode = 0;
    if (erlang::nif::get(env, argv[1], &mode)) {
        std::string path;
        const char * c_path = nullptr;
        if (erlang::nif::get(env, argv[0], path)) {
            if (path == "RTLD_SELF") {
                c_path = nullptr;
            } else {
                c_path = path.c_str();
            }
        } else {
            return enif_make_badarg(env);
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

static ERL_NIF_TERM otter_dlsym(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    if (argc != 2) return enif_make_badarg(env);

    OtterHandle * res;
    std::string func_name;
    if (enif_get_resource(env, argv[0], OtterHandle::type, (void **)&res) &&
        erlang::nif::get(env, argv[1], func_name)) {
        void * handle = res->val;
        if (handle != nullptr) {
            void* symbol = dlsym(handle, func_name.c_str());
            if (symbol == nullptr) {
                return erlang::nif::error(env, dlerror());
            } else {
                OtterSymbol * symbol_res;
                if (alloc_resource(&symbol_res)) {
                    symbol_res->val = symbol;
                    ERL_NIF_TERM ret = enif_make_resource(env, symbol_res);
                    enif_release_resource(res);
                    return erlang::nif::ok(env, ret);
                } else {
                    return erlang::nif::error(env, "cannot allocate memory for resource");
                }
            }
        } else {
            return erlang::nif::error(env, "resource has an invalid image handle");
        }
    } else {
        return erlang::nif::error(env, "cannot get image handle");
    }
}

static bool get_args_with_type(ErlNifEnv *env, ERL_NIF_TERM arg_types_term, std::vector<std::pair<ERL_NIF_TERM, std::string>> &args_with_type) {
    if (!enif_is_list(env, arg_types_term)) return 0;

    unsigned int length;
    if (!enif_get_list_length(env, arg_types_term, &length)) return 0;
    args_with_type.reserve(length);
    ERL_NIF_TERM head, tail;

    while (enif_get_list_cell(env, arg_types_term, &head, &tail)) {
        if (enif_is_tuple(env, head)) {
            int arity;
            const ERL_NIF_TERM * array;
            if (enif_get_tuple(env, head, &arity, &array) && arity == 2) {
                std::string arg_type;
                if (erlang::nif::get_atom(env, array[1], arg_type) || erlang::nif::get(env, array[1], arg_type)) {
                    args_with_type.push_back(std::make_pair(array[0], arg_type));
                    arg_types_term = tail;
                } else {
                    return 0;
                }
            } else {
                return 0;
            }
        } else {
            return 0;
        }
    }
    return 1;
}

static ERL_NIF_TERM otter_invoke(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    if (argc != 3) return enif_make_badarg(env);

    OtterSymbol * symbol_res;
    std::string return_type;
    std::vector<std::pair<ERL_NIF_TERM, std::string>> args_with_type;

    if (enif_get_resource(env, argv[0], OtterSymbol::type, (void **)&symbol_res) &&
        erlang::nif::get_atom(env, argv[1], return_type) &&
        get_args_with_type(env, argv[2], args_with_type)) {
        void * symbol = symbol_res->val;
        if (symbol != nullptr) {
            ffi_cif cif;
            ffi_type **args = (ffi_type **)enif_alloc(sizeof(ffi_type *) * args_with_type.size());
            void ** values = (void **)enif_alloc(sizeof(void *) * args_with_type.size());
            ffi_type * ffi_return_type;
            void * rc = malloc(sizeof(ffi_arg));
            void * null_ptr = nullptr;

            int ready = 1;
            for (size_t i = 0; i < args_with_type.size(); ++i) {
                auto& p = args_with_type[i];
                if (p.second == "c_ptr") {
                    ErlNifBinary binary;
                    if (enif_inspect_binary(env, p.first, &binary)) {
                        args[i] = &ffi_type_pointer;
                        values[i] = &binary.data;
                    } else {
                        ready = 0;
                        break;
                    }
                } else if (p.second == "i32") {
                    int i32;
                    if (erlang::nif::get(env, p.first, &i32)) {
                        args[i] = &ffi_type_sint32;
                        values[i] = &i32;
                    } else {
                        ready = 0;
                        break;
                    }
                } else if (p.second == "f64") {
                    double f64;
                    if (erlang::nif::get(env, p.first, &f64)) {
                        args[i] = &ffi_type_double;
                        values[i] = &f64;
                    } else {
                        ready = 0;
                        break;
                    }
                }  else if (p.second == "va_args") {
                    // todo: handle va_args
                    args[i] = &ffi_type_pointer;
                    values[i] = &null_ptr;
                } else {
                    // todo: other types
                    printf("[debug] todo: arg%zu, type: %s\r\n", i, p.second.c_str());
                }
            }

            if (return_type == "void") {
                ffi_return_type = &ffi_type_void;
            } else if (return_type == "u8") {
                ffi_return_type = &ffi_type_uint8;
            } else if (return_type == "s8") {
                ffi_return_type = &ffi_type_sint8;
            } else if (return_type == "u16") {
                ffi_return_type = &ffi_type_uint16;
            } else if (return_type == "s16") {
                ffi_return_type = &ffi_type_sint16;
            } else if (return_type == "u32") {
                ffi_return_type = &ffi_type_uint32;
            } else if (return_type == "s32") {
                ffi_return_type = &ffi_type_sint32;
            } else if (return_type == "u64") {
                ffi_return_type = &ffi_type_uint64;
            } else if (return_type == "s64") {
                ffi_return_type = &ffi_type_sint64;
            } else if (return_type == "f32") {
                ffi_return_type = &ffi_type_float;
            } else if (return_type == "f64") {
                ffi_return_type = &ffi_type_double;
            } else if (return_type == "long_double") {
                ffi_return_type = &ffi_type_longdouble;
            } else if (return_type == "c_ptr") {
                ffi_return_type = &ffi_type_pointer;
            } else {
                printf("[debug] todo: return_type: %s\r\n", return_type.c_str());
                ready = 0;
            }

            if (!ready) {
                return erlang::nif::error(env, "failed to get some input arguments");
            }

            if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, args_with_type.size(), ffi_return_type, args) == FFI_OK) {
                ffi_call(&cif, (void (*)())symbol, rc, values);
            } else {
                return erlang::nif::error(env, "ffi_prep_cif failed");
            }

            ERL_NIF_TERM ret;
            if (return_type == "void") {
                ret = erlang::nif::ok(env);
            } else if (return_type == "u8") {
                ret = erlang::nif::ok(env, enif_make_uint(env, *(uint8_t *)rc));
            } else if (return_type == "s8") {
                ret = erlang::nif::ok(env, enif_make_int(env, *(int8_t *)rc));
            } else if (return_type == "u16") {
                ret = erlang::nif::ok(env, enif_make_uint(env, *(uint16_t *)rc));
            } else if (return_type == "s16") {
                ret = erlang::nif::ok(env, enif_make_int(env, *(int16_t *)rc));
            } else if (return_type == "u32") {
                ret = erlang::nif::ok(env, enif_make_uint(env, *(uint32_t *)rc));
            } else if (return_type == "s32") {
                ret = erlang::nif::ok(env, enif_make_int(env, *(int32_t *)rc));
            } else if (return_type == "u64") {
                ret = erlang::nif::ok(env, enif_make_uint64(env, *(uint64_t *)rc));
            } else if (return_type == "s64") {
                ret = erlang::nif::ok(env, enif_make_int64(env, *(int64_t *)rc));
            } else if (return_type == "f32") {
                ret = erlang::nif::ok(env, enif_make_double(env, *(float *)rc));
            } else if (return_type == "f64") {
                ret = erlang::nif::ok(env, enif_make_double(env, *(double *)rc));
            } else if (return_type == "c_ptr") {
                ret = erlang::nif::ok(env, enif_make_uint64(env, (uint64_t)(*(uint64_t **)rc)));
            } else {
                printf("[debug] todo: return_type: %s\r\n", return_type.c_str());
                ret = erlang::nif::ok(env);
            }

            enif_free((void *)args);
            enif_free((void *)values);
            free((void *)rc);
            return ret;
        } else {
            return erlang::nif::error(env, "resource has an invalid handle");
        }
    } else {
        return erlang::nif::error(env, "cannot get symbol");
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
    {"dlsym", 2, otter_dlsym, 0},
    {"invoke", 3, otter_invoke, 0},
};

ERL_NIF_INIT(Elixir.Otter.Nif, nif_functions, on_load, on_reload, on_upgrade, NULL);

#if defined(__GNUC__)
#  pragma GCC visibility push(default)
#endif
