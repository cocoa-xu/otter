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

static std::map<std::string, OtterHandle *> opened_handles;
static std::map<OtterHandle *, std::map<std::string, OtterSymbol *>> found_symbols;

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

        OtterHandle * handle = nullptr;
        if (opened_handles.find(path) != opened_handles.end()) {
            handle = opened_handles[path];
        } else {
            void * handle_dl = dlopen(c_path, mode);
            if (handle_dl != nullptr) {
                if (alloc_resource(&handle)) {
                    opened_handles[path] = handle;
                    handle->val = handle_dl;
                } else {
                    dlclose(handle_dl);
                    return erlang::nif::error(env, "cannot allocate memory for resource");
                }
            } else {
                return erlang::nif::error(env, dlerror());
            }
        }

        ERL_NIF_TERM ret = enif_make_resource(env, handle);
        return erlang::nif::ok(env, ret);
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
            auto entry = opened_handles.end();
            for (auto it = opened_handles.begin(); it != opened_handles.end(); ++it) {
                if (it->second->val == handle) {
                    entry = it;
                    break;
                }
            }

            if (entry != opened_handles.end()) {
                opened_handles.erase(entry);
            }

            // release all symbols
            auto &symbols = found_symbols[entry->second];
            for (auto& s : symbols) {
                enif_release_resource(s.second);
            }
            found_symbols[entry->second].clear();
            found_symbols.erase(entry->second);
            enif_release_resource(entry->second);

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
            OtterHandle * opened_res = nullptr;
            auto entry = opened_handles.end();
            for (auto it = opened_handles.begin(); it != opened_handles.end(); ++it) {
                if (it->second->val == handle) {
                    opened_res = it->second;
                    break;
                }
            }

            if (opened_res == nullptr) {
                opened_res = res;
            }

            OtterSymbol * symbol = nullptr;
            if (found_symbols.find(opened_res) != found_symbols.end()) {
                auto &symbols = found_symbols[opened_res];
                if (symbols.find(func_name) != symbols.end()) {
                    symbol = symbols[func_name];
                }
            }

            if (symbol == nullptr) {
                if (alloc_resource(&symbol)) {
                    void* symbol_dl = dlsym(handle, func_name.c_str());
                    if (symbol_dl == nullptr) {
                        return erlang::nif::error(env, dlerror());
                    }
                    symbol->val = symbol_dl;
                } else {
                    return erlang::nif::error(env, "cannot allocate memory for resource");
                }
            }

            found_symbols[opened_res][func_name] = symbol;
            ERL_NIF_TERM ret = enif_make_resource(env, symbol);

            return erlang::nif::ok(env, ret);
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

static std::map<std::string, ffi_type *> str2ffi_type = {
    {"u8", &ffi_type_uint8},
    {"bool", &ffi_type_uint8},
    {"u16", &ffi_type_uint16},
    {"u32", &ffi_type_uint32},
    {"u64", &ffi_type_uint64},
    {"s8", &ffi_type_sint8},
    {"s16", &ffi_type_sint16},
    {"s32", &ffi_type_sint32},
    {"s64", &ffi_type_sint64},
    {"f32", &ffi_type_float},
    {"f64", &ffi_type_double},
};

static bool get_struct_return_type(ErlNifEnv *env, ERL_NIF_TERM struct_return_type_term,
    ffi_type& ffi_struct_type,
    std::vector<ffi_type*>& struct_return_type_field_types
) {
    int arity = -1;
    const ERL_NIF_TERM * array;
    std::vector<std::pair<ERL_NIF_TERM, std::string>> args_with_type;
    bool is_size_2_tuple = enif_get_tuple(env, struct_return_type_term, &arity, &array) && arity == 2;
    std::string struct_atom;
    erlang::nif::get_atom(env, array[0], struct_atom);
    if (!is_size_2_tuple) {
        return false;
    }
    if (!(struct_atom == "struct")) {
        return false;
    }
    if (get_args_with_type(env, array[1], args_with_type)) {
        ffi_struct_type.size = args_with_type.size();
        ffi_struct_type.alignment = 0;
        ffi_struct_type.type = FFI_TYPE_STRUCT;
        struct_return_type_field_types.resize(args_with_type.size());
        ffi_struct_type.elements = struct_return_type_field_types.data();
        for (size_t i = 0; i < args_with_type.size(); ++i) {
            auto& p = args_with_type[i];
            if (p.second == "c_ptr") {
                struct_return_type_field_types[i] = &ffi_type_pointer;
            } else {
                struct_return_type_field_types[i] = str2ffi_type[p.second];
            }
        }
        return true;
    } else {
        return false;
    }
}

static ERL_NIF_TERM otter_invoke(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    if (argc != 3) return enif_make_badarg(env);

    OtterSymbol * symbol_res;

    bool has_struct_return_type = false;
    std::string return_type;
    ffi_type struct_return_type;
    std::vector<ffi_type*> struct_return_type_field_types;

    std::vector<std::pair<ERL_NIF_TERM, std::string>> args_with_type;
    if (erlang::nif::get_atom(env, argv[1], return_type)) {
        has_struct_return_type = false;
    }
    else if (get_struct_return_type(env, argv[1], struct_return_type, struct_return_type_field_types)) {
        has_struct_return_type = true;
    }
    else {
        return erlang::nif::error(env, "fail to get return_type");
    }
    if (enif_get_resource(env, argv[0], OtterSymbol::type, (void **)&symbol_res) &&
        get_args_with_type(env, argv[2], args_with_type)) {
        void * symbol = symbol_res->val;
        if (symbol != nullptr) {
            ffi_cif cif;
            ffi_type **args = (ffi_type **)enif_alloc(sizeof(ffi_type *) * args_with_type.size());
            void ** values = (void **)enif_alloc(sizeof(void *) * args_with_type.size());
            ffi_type * ffi_return_type;
            void * rc = malloc(sizeof(ffi_arg));
            void * null_ptr = nullptr;
            size_t ptrs_cap = 32;
            size_t ptrs_next = 0;
            void ** ptrs = (void **)malloc(sizeof(void *) * ptrs_cap);

            int ready = 1;
            for (size_t i = 0; i < args_with_type.size(); ++i) {
                auto& p = args_with_type[i];
                std::string null_c_ptr;
                if (p.second == "c_ptr") {
                    ErlNifBinary binary;
                    int64_t ptr;
                    if (enif_inspect_binary(env, p.first, &binary)) {
                        args[i] = &ffi_type_pointer;
                        values[i] = &binary.data;
                    } else if (erlang::nif::get_atom(env, p.first, null_c_ptr) && (null_c_ptr == "NULL" || null_c_ptr == "nil")) {
                        args[i] = &ffi_type_pointer;
                        values[i] = &null_ptr;
                    } else if (erlang::nif::get_sint64(env, p.first, &ptr)) {
                        args[i] = &ffi_type_pointer;
                        ptrs[ptrs_next] = (void *)(int64_t *)(ptr);
                        values[i] = &ptrs[ptrs_next];
                        ptrs_next += 1;
                        if (ptrs_next == ptrs_cap) {
                            // todo: resize
                        }
                    } else {
                        ready = 0;
                        break;
                    }
                } else if ((p.second == "s8") || (p.second == "s16") || (p.second == "s32")) {
                    int sint;
                    if (erlang::nif::get_sint(env, p.first, &sint)) {
                        args[i] = str2ffi_type[p.second];
                        values[i] = &sint;
                    } else {
                        ready = 0;
                        printf("[debug] cannot get value for %s\r\n", p.second.c_str());
                        break;
                    }
                } else if ((p.second == "u8") || (p.second == "u16") || (p.second == "u32")) {
                    unsigned int uint;
                    if (erlang::nif::get_uint(env, p.first, &uint)) {
                        args[i] = str2ffi_type[p.second];
                        values[i] = &uint;
                    } else {
                        ready = 0;
                        printf("[debug] cannot get value for %s\r\n", p.second.c_str());
                        break;
                    }
                } else if ((p.second == "f32") || (p.second == "f64")) {
                    double f64;
                    float f32;
                    if (erlang::nif::get_f64(env, p.first, &f64)) {
                        args[i] = str2ffi_type[p.second];
                        if (p.second == "f32") {
                            f32 = (float)f64;
                            values[i] = &f32;
                        } else {
                            values[i] = &f64;
                        }
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
            if (has_struct_return_type) {
                ffi_return_type = &struct_return_type;
            } else if (return_type == "void") {
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
            if (has_struct_return_type) {
                printf("[debug] todo: return struct\r\n");
                ret = erlang::nif::ok(env);
            }
            else if (return_type == "void") {
                ret = erlang::nif::ok(env);
            } else if (return_type == "u8") {
                ret = enif_make_uint(env, *(uint8_t *)rc);
            } else if (return_type == "s8") {
                ret = enif_make_int(env, *(int8_t *)rc);
            } else if (return_type == "u16") {
                ret = enif_make_uint(env, *(uint16_t *)rc);
            } else if (return_type == "s16") {
                ret = enif_make_int(env, *(int16_t *)rc);
            } else if (return_type == "u32") {
                ret = enif_make_uint(env, *(uint32_t *)rc);
            } else if (return_type == "s32") {
                ret = enif_make_int(env, *(int32_t *)rc);
            } else if (return_type == "u64") {
                ret = enif_make_uint64(env, *(uint64_t *)rc);
            } else if (return_type == "s64") {
                ret = enif_make_int64(env, *(int64_t *)rc);
            } else if (return_type == "f32") {
                ret = enif_make_double(env, *(float *)rc);
            } else if (return_type == "f64") {
                ret = enif_make_double(env, *(double *)rc);
            } else if (return_type == "c_ptr") {
                ret = enif_make_uint64(env, (uint64_t)(*(uint64_t *)rc));
            } else {
                printf("[debug] todo: return_type: %s\r\n", return_type.c_str());
                ret = erlang::nif::ok(env);
            }

            enif_free((void *)args);
            enif_free((void *)values);
            free((void *)rc);
            free((void *)ptrs);
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
