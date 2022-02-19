#include "nif_utils.hpp"
#include <dlfcn.h>
#include <erl_nif.h>
#include <ffi.h>
#include <iostream>
#include <mutex>
#include <memory>
#include <stdlib.h>

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

template <typename R> struct erlang_nif_res {
    R val;
    static ErlNifResourceType *type;
};
template <typename R> ErlNifResourceType *erlang_nif_res<R>::type = nullptr;

template <typename R> int alloc_resource(erlang_nif_res<R> **res) {
    *res = (erlang_nif_res<R> *)enif_alloc_resource(erlang_nif_res<R>::type,
                                                    sizeof(erlang_nif_res<R>));
    return (*res != nullptr);
}

using OtterHandle = erlang_nif_res<void *>;
using OtterSymbol = erlang_nif_res<void *>;
static void destruct_otter_handle(ErlNifEnv *env, void *args) {}

static std::map<std::string, OtterHandle *> opened_handles;
static std::map<OtterHandle *, std::map<std::string, OtterSymbol *>> found_symbols;

static ERL_NIF_TERM otter_dlopen(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    if (argc != 2) {
        return enif_make_badarg(env);
    }
    ERL_NIF_TERM path_term = argv[0];
    ERL_NIF_TERM mode_term = argv[1];

    int mode = 0;
    if (erlang::nif::get(env, mode_term, &mode)) {
        std::string path;
        const char *c_path = nullptr;
        if (erlang::nif::get(env, path_term, path) && !path.empty()) {
            if (path == "RTLD_SELF") {
                c_path = nullptr;
            } else {
                c_path = path.c_str();
            }
        } else {
            return enif_make_badarg(env);
        }

        OtterHandle *handle = nullptr;
        if (opened_handles.find(path) != opened_handles.end()) {
            handle = opened_handles[path];
        } else {
            void *handle_dl = dlopen(c_path, mode);
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
    if (argc != 1) {
        return enif_make_badarg(env);
    }

    OtterHandle *res = nullptr;
    if (enif_get_resource(env, argv[0], OtterHandle::type, (void **)&res) && res) {
        void *handle = res->val;
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
            for (auto &s : symbols) {
                enif_release_resource(s.second);
            }
            // remove image entry in found_symbols
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

static ERL_NIF_TERM otter_dlsym(ErlNifEnv *env, int argc,
                                const ERL_NIF_TERM argv[]) {
  if (argc != 2) {
      return enif_make_badarg(env);
  }

  OtterHandle *res = nullptr;
  std::string func_name;
  if (enif_get_resource(env, argv[0], OtterHandle::type, (void **)&res) &&
      erlang::nif::get(env, argv[1], func_name) && res && !func_name.empty()) {
    void *handle = res->val;
    if (handle != nullptr) {
      OtterHandle *opened_res = nullptr;
      for (auto it = opened_handles.begin(); it != opened_handles.end(); ++it) {
        if (it->second->val == handle) {
          opened_res = it->second;
          break;
        }
      }

      if (opened_res == nullptr) {
        opened_res = res;
      }

      OtterSymbol *symbol = nullptr;
      if (found_symbols.find(opened_res) != found_symbols.end()) {
        auto &symbols = found_symbols[opened_res];
        if (symbols.find(func_name) != symbols.end()) {
          symbol = symbols[func_name];
        }
      }

      if (symbol == nullptr) {
        if (alloc_resource(&symbol)) {
          void *symbol_dl = dlsym(handle, func_name.c_str());
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

static std::map<std::string, ffi_type *> str2ffi_type = {
    {"u8", &ffi_type_uint8},   {"u16", &ffi_type_uint16},
    {"u32", &ffi_type_uint32}, {"u64", &ffi_type_uint64},
    {"s8", &ffi_type_sint8},   {"s16", &ffi_type_sint16},
    {"s32", &ffi_type_sint32}, {"s64", &ffi_type_sint64},
    {"f32", &ffi_type_float},  {"f64", &ffi_type_double},
    {"void", &ffi_type_void},
};

static void resource_dtor(ErlNifEnv *, void *obj) {}

// NOTE: the basic idea here we register a resource type for each struct type,
// identified by struct_id.
static ErlNifResourceType *
register_ffi_struct_resource_type(ErlNifEnv *env, std::string &struct_id) {
    auto resource_type = enif_open_resource_type(
      env, "Elixir.Otter.Nif", ("OTTER_STRUCT_" + struct_id).data(), resource_dtor,
      ERL_NIF_RT_CREATE, nullptr);
    return resource_type;
}

static std::map<std::string, ErlNifResourceType *>
    struct_resource_type_registry{};
static std::mutex struct_resource_type_registry_lock;

static ErlNifResourceType *
get_ffi_struct_resource_type(ErlNifEnv *env, std::string &struct_id) {
  std::lock_guard<std::mutex> g(struct_resource_type_registry_lock);
  auto it = struct_resource_type_registry.find(struct_id);
  if (it == struct_resource_type_registry.end()) {
    auto t = register_ffi_struct_resource_type(env, struct_id);
    struct_resource_type_registry[struct_id] = t;
    return t;
  } else {
    return it->second;
  }
}

static ERL_NIF_TERM make_ffi_struct_resource(ErlNifEnv *env,
                                             size_t return_object_size,
                                             ErlNifResourceType *resource_type,
                                             void *result, ERL_NIF_TERM &ret) {
    if (resource_type && return_object_size && result) {
        auto resource = enif_alloc_resource(resource_type, return_object_size);
        if (resource) {
            memcpy(resource, (void *)result, return_object_size);
            ret = enif_make_resource(env, resource);
            enif_release_resource(resource);
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

class FFIStructTypeWrapper {
public:
    FFIStructTypeWrapper(size_t field_size) {
      ffi_struct_type.size = 0;      // set by libffi, initialize it to zero
      ffi_struct_type.alignment = 0; // set by libffi, initialize it to zero
      ffi_struct_type.type = FFI_TYPE_STRUCT;
      ffi_struct_type.elements = (decltype(ffi_struct_type.elements))malloc(sizeof(void *) * (field_size + 1));
      memset(ffi_struct_type.elements, 0, sizeof(void *) * (field_size + 1));
    }

    ~FFIStructTypeWrapper() {
        if (ffi_struct_type.elements) {
            free((void *)ffi_struct_type.elements);
            ffi_struct_type.elements = nullptr;
        }
    }
      
    FFIStructTypeWrapper(FFIStructTypeWrapper &&other) {
        // take over elements from `other`
        this->ffi_struct_type = other.ffi_struct_type;
        other.ffi_struct_type.elements = nullptr;

        this->resource_type = other.resource_type;
        this->struct_id = other.struct_id;
        this->field_types = other.field_types;
    }

    FFIStructTypeWrapper(const FFIStructTypeWrapper &) = delete;
    FFIStructTypeWrapper &operator=(FFIStructTypeWrapper &) = delete;
    FFIStructTypeWrapper &operator=(const FFIStructTypeWrapper &) = delete;
    static std::shared_ptr<FFIStructTypeWrapper>
    create_from_tuple(ErlNifEnv *env, ERL_NIF_TERM struct_return_type_term, std::vector<std::shared_ptr<FFIStructTypeWrapper>> &wrappers);

    ffi_type ffi_struct_type;
    ErlNifResourceType *resource_type;
    std::string struct_id;
    std::vector<std::shared_ptr<ffi_type>> field_types;
};

enum arg_passing_type {
    VALUE,
    ADDR,
    REF,
};

class arg_type {
public:
    arg_type(ERL_NIF_TERM term_, ERL_NIF_TERM type_term_, const std::string type_, uint64_t size_, ERL_NIF_TERM info) :
        term(term_), type_term(type_term_), type(type_), size(size_), extra_info(info) {
        pass_by = VALUE;
        is_out = false;
        value_slot = 0;
        value_slot_original = 0;
        is_va_args = false;
        is_struct_instance = false;
        struct_data = nullptr;
    }

    ERL_NIF_TERM term;
    ERL_NIF_TERM type_term;
    std::string type;

    // size > 0: nd array
    uint64_t size;
    // erlang map
    ERL_NIF_TERM extra_info;

    // ffi type
    std::shared_ptr<ffi_type> ffi_arg_type;
    std::shared_ptr<ffi_type> ffi_arg_type_original;

    size_t value_slot;
    size_t value_slot_original;

    // if pass_by == VALUE or pass_by == REF
    //  then we have ffi_arg_type == ffi_arg_type_original
    // if pass_by == ADDR
    //  then ffi_arg_type_original will record the original type
    //       and ffi_arg_type will be the corresponding pointer type
    arg_passing_type pass_by;
    bool is_out;
    bool is_va_args;

    bool is_struct_instance;
    // borrowed data from erlang vm
    // do not free struct_data
    void * struct_data;
};

static void * null_ptr_g = nullptr;

static void copy_ffi_type(std::shared_ptr<ffi_type> &shared_ffi_type, ffi_type &copy_from) {
    shared_ffi_type->size = copy_from.size;
    shared_ffi_type->alignment = copy_from.alignment;
    shared_ffi_type->type = copy_from.type;
    shared_ffi_type->elements = nullptr;
}

// NOTE: [{value, type}], if type is a struct tuple {:struct, id, fields}, in
// this func we convert it to id
static bool get_args_with_type(ErlNifEnv *env, ERL_NIF_TERM arg_types_term, std::vector<arg_type> &args_with_type, size_t prev_size, std::vector<std::shared_ptr<FFIStructTypeWrapper>> &wrappers) {
    if (!enif_is_list(env, arg_types_term)) {
        return 0;
    }

    unsigned int length;
    if (!enif_get_list_length(env, arg_types_term, &length)) {
        return 0;
    }

    args_with_type.reserve(prev_size + length);
    ERL_NIF_TERM head, tail;

    while (enif_get_list_cell(env, arg_types_term, &head, &tail)) {
        if (enif_is_tuple(env, head)) {
            int arity;
            const ERL_NIF_TERM *array;
            if (enif_get_tuple(env, head, &arity, &array) && arity == 2) {
                std::string arg_type_str;
                ERL_NIF_TERM arg_value = array[0];
                ERL_NIF_TERM arg_type_info = array[1];

                ERL_NIF_TERM type_term;
                enif_get_map_value(env, arg_type_info, enif_make_atom(env, "type"), &type_term);
                if (erlang::nif::get_atom(env, type_term, arg_type_str) ||
                    erlang::nif::get(env, type_term, arg_type_str)) {
                    // {arg_value, %{type: type_term}}
                    // {arg_value, %{type: type_term, size: size_term}}

                    // ignore error as `size` will stay 0 if key `size` is not in the map
                    uint64_t size = 0;
                    ERL_NIF_TERM size_term;
                    if (enif_get_map_value(env, arg_type_info, enif_make_atom(env, "size"), &size_term)) {
                        erlang::nif::get_uint64(env, size_term, &size);
                    }
                    args_with_type.emplace_back(arg_value, type_term, arg_type_str, size, arg_type_info);
                    auto &arg_with_type = args_with_type[args_with_type.size() - 1];

                    bool pass_by_addr = false;
                    ERL_NIF_TERM addr_term;
                    if (enif_get_map_value(env, arg_type_info, enif_make_atom(env, "addr"), &addr_term)) {
                        arg_with_type.pass_by = ADDR;
                    }

                    bool is_out = false;
                    ERL_NIF_TERM out_term;
                    if (enif_get_map_value(env, arg_type_info, enif_make_atom(env, "out"), &out_term)) {
                        arg_with_type.is_out = true;
                    }

                    if (arg_with_type.type == "c_ptr") {
                        arg_with_type.ffi_arg_type = std::make_shared<ffi_type>();
                        copy_ffi_type(arg_with_type.ffi_arg_type, ffi_type_pointer);
                    } else if (arg_with_type.type == "s8") {
                        arg_with_type.ffi_arg_type = std::make_shared<ffi_type>();
                        copy_ffi_type(arg_with_type.ffi_arg_type, ffi_type_sint8);
                    } else if (arg_with_type.type == "s16") {
                        arg_with_type.ffi_arg_type = std::make_shared<ffi_type>();
                        copy_ffi_type(arg_with_type.ffi_arg_type, ffi_type_sint16);
                    } else if (arg_with_type.type == "s32") {
                        arg_with_type.ffi_arg_type = std::make_shared<ffi_type>();
                        copy_ffi_type(arg_with_type.ffi_arg_type, ffi_type_sint32);
                    } else if (arg_with_type.type == "s64") {
                        arg_with_type.ffi_arg_type = std::make_shared<ffi_type>();
                        copy_ffi_type(arg_with_type.ffi_arg_type, ffi_type_sint64);
                    } else if (arg_with_type.type == "u8") {
                        arg_with_type.ffi_arg_type = std::make_shared<ffi_type>();
                        copy_ffi_type(arg_with_type.ffi_arg_type, ffi_type_uint8);
                    } else if (arg_with_type.type == "u16") {
                        arg_with_type.ffi_arg_type = std::make_shared<ffi_type>();
                        copy_ffi_type(arg_with_type.ffi_arg_type, ffi_type_uint16);
                    } else if (arg_with_type.type == "u32") {
                        arg_with_type.ffi_arg_type = std::make_shared<ffi_type>();
                        copy_ffi_type(arg_with_type.ffi_arg_type, ffi_type_uint32);
                    } else if (arg_with_type.type == "u64") {
                        arg_with_type.ffi_arg_type = std::make_shared<ffi_type>();
                        copy_ffi_type(arg_with_type.ffi_arg_type, ffi_type_uint64);
                    } else if (arg_with_type.type == "f32") {
                        arg_with_type.ffi_arg_type = std::make_shared<ffi_type>();
                        copy_ffi_type(arg_with_type.ffi_arg_type, ffi_type_float);
                    } else if (arg_with_type.type == "f64") {
                        arg_with_type.ffi_arg_type = std::make_shared<ffi_type>();
                        copy_ffi_type(arg_with_type.ffi_arg_type, ffi_type_double);
                    } else if (arg_with_type.type == "va_args") {
                        arg_with_type.ffi_arg_type = std::make_shared<ffi_type>();
                        arg_with_type.is_va_args = true;
                    }

                    if (arg_with_type.size > 0) {
                        ffi_type ffi_type_array;
                        ffi_type_array.size = arg_with_type.ffi_arg_type->size * arg_with_type.size;
                        ffi_type_array.alignment = arg_with_type.ffi_arg_type->alignment;
                        ffi_type_array.type = FFI_TYPE_STRUCT;
                        
                        copy_ffi_type(arg_with_type.ffi_arg_type, ffi_type_array);
                        arg_with_type.ffi_arg_type->elements = (ffi_type **)&null_ptr_g;
                    }

                    arg_types_term = tail;
                } else if (enif_is_tuple(env, type_term)) {
                    auto struct_type = FFIStructTypeWrapper::create_from_tuple(env, type_term, wrappers);
                    if (struct_type) {
                        args_with_type.emplace_back(arg_value, type_term, struct_type->struct_id, 0, enif_make_atom(env, "nil"));
                        wrappers.push_back(struct_type);
                        arg_types_term = tail;
                    } else {
                        // other tuples?
                        printf("[debug] tuple but not struct\r\n");
                        return 0;
                    }
                } else {
                    auto struct_type = FFIStructTypeWrapper::create_from_tuple(env, arg_type_info, wrappers);
                    if (struct_type) {
                        args_with_type.emplace_back(arg_value, arg_type_info, struct_type->struct_id, 0, enif_make_atom(env, "nil"));
                        wrappers.push_back(struct_type);
                        arg_types_term = tail;
                    } else {
                        return 0;
                    }
                }
            } else {
                return 0;
            }
        } else {
            // just values
            return 0;
        }
    }
    return (args_with_type.size() - prev_size) == length;
}

std::shared_ptr<FFIStructTypeWrapper>
FFIStructTypeWrapper::create_from_tuple(ErlNifEnv *env,
                                        ERL_NIF_TERM struct_return_type_term,
                                        std::vector<std::shared_ptr<FFIStructTypeWrapper>> &wrappers) {
    int arity = -1;
    const ERL_NIF_TERM *array;
    std::vector<arg_type> args_with_type;
    bool is_size_correct_tuple =
            enif_get_tuple(env, struct_return_type_term, &arity, &array) &&
            arity == 3;
    std::string struct_atom;
    std::string struct_id;
    erlang::nif::get_atom(env, array[0], struct_atom);
    erlang::nif::get_atom(env, array[1], struct_id);
    if (!is_size_correct_tuple) {
        return nullptr;
    }

    if (struct_atom != "struct") {
        return nullptr;
    }

    if (get_args_with_type(env, array[2], args_with_type, 0, wrappers)) {
        auto wrapper = std::make_shared<FFIStructTypeWrapper>(args_with_type.size() + 1);

        wrapper->struct_id = struct_id;
        wrapper->resource_type = get_ffi_struct_resource_type(env, struct_id);

        // note: wrapper will be added to `wrappers` after it is returned from this function
        if (wrapper->resource_type) {
            for (size_t i = 0; i < args_with_type.size(); ++i) {
                auto &p = args_with_type[i];
                wrapper->field_types.push_back(p.ffi_arg_type);
                wrapper->ffi_struct_type.elements[i] = wrapper->field_types[i].get();
            }
            wrapper->ffi_struct_type.elements[args_with_type.size()] = nullptr;
            return wrapper;
        } else {
            wrapper.reset();
            return nullptr;
        }
    }
    return nullptr;
}

static ERL_NIF_TERM otter_symbol_to_address(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    if (argc != 1) return enif_make_badarg(env);

    OtterSymbol *symbol_res = nullptr;
    if (enif_get_resource(env, argv[0], OtterSymbol::type, (void **)&symbol_res)) {
        void *symbol = symbol_res->val;
        // if it is nullptr, then the return value will be 0
        // which I'd like to keep it the same as what would have expected to be
        // if (symbol != nullptr)
        return erlang::nif::ok(env, enif_make_uint64(env, (uint64_t)((uint64_t *)symbol)));
    } else {
        return erlang::nif::error(env, "cannot get symbol resource");
    }
}

static ERL_NIF_TERM otter_address_to_symbol(ErlNifEnv *env, int argc,
                                            const ERL_NIF_TERM argv[]) {
    if (argc != 1) return enif_make_badarg(env);

    OtterSymbol *symbol_res = nullptr;
    uint64_t address;
    if (erlang::nif::get_uint64(env, argv[0], &address)) {
        if (alloc_resource(&symbol_res)) {
            symbol_res->val = (void *)(uint64_t *)address;
            ERL_NIF_TERM res = enif_make_resource(env, symbol_res);
            enif_release_resource(symbol_res);
            return erlang::nif::ok(env, res);
        } else {
            return erlang::nif::error(env, "cannot allocate memory for resource");
        }
    } else {
        return erlang::nif::error(env, "cannot get address");
    }
}

static ERL_NIF_TERM otter_erl_nif_env(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    return erlang::nif::ok(env, enif_make_uint64(env, (uint64_t)(*(uint64_t *)env)));
}

template<typename T>
class ffi_resources {
public:
    ffi_resources(size_t default_cap=32) : invalid(false), increase_by(default_cap), cap(0), next(0) {
    }
    ~ffi_resources() {
        if (resources) {
            free((void *)resources);
            resources = nullptr;
        }
        invalid = true;
    }

    bool get(size_t slot, void *&addr) {
        if (invalid || slot >= next) return false;
        addr = &resources[slot];
        return true;
    }

    bool get_value(size_t slot, T& value) {
        if (invalid || slot >= next) return false;
        value = resources[slot];
        return true;
    }

    bool set(T val, size_t &occupy) {
        if (invalid) return false;
        if (next == cap) {
            size_t new_cap = cap + increase_by;
            T * new_res = (T *)realloc(this->resources, sizeof(T) * new_cap);
            if (new_res != nullptr) {
                cap = new_cap;
                this->resources = new_res;
            } else {
                free((void *)resources);
                resources = nullptr;
                invalid = true;
                return false;
            }
        }
        this->resources[next] = val;
        occupy = next;
        next++;
        return true;
    }

    bool invalid;
    T * resources = nullptr;
    size_t increase_by;
    size_t cap;
    size_t next;
};

template <typename T>
static auto get_ffi_res(std::map<uint64_t, void *> &ffi_res, uint64_t type, bool allow_new) -> ffi_resources<T> * {
    if (ffi_res.find(type) != ffi_res.end()) {
        return (ffi_resources<T> *)ffi_res[type];
    } else if (allow_new) {
        ffi_res[type] = new ffi_resources<T>();
        return (ffi_resources<T> *)ffi_res[type];
    } else {
        return nullptr;
    }
}

template <typename T>
static void free_ffi_res(std::map<uint64_t, void *> &ffi_res, uint64_t type) {
    if (ffi_res.find(type) != ffi_res.end()) {
        auto p = (ffi_resources<T> *)ffi_res[type];
        if (p) delete p;
    }
}

template <typename T>
static ffi_type * get_default_ffi_type(T val=0) {
    return nullptr;
}

template <>
ffi_type * get_default_ffi_type(void *) {
    return &ffi_type_pointer;
}

template <>
ffi_type * get_default_ffi_type(uint8_t) {
    return &ffi_type_uint8;
}

template <>
ffi_type * get_default_ffi_type(uint16_t) {
    return &ffi_type_uint16;
}

template <>
ffi_type * get_default_ffi_type(uint32_t) {
    return &ffi_type_uint32;
}

template <>
ffi_type * get_default_ffi_type(uint64_t) {
    return &ffi_type_uint64;
}

template <>
ffi_type * get_default_ffi_type(int8_t) {
    return &ffi_type_sint8;
}

template <>
ffi_type * get_default_ffi_type(int16_t) {
    return &ffi_type_sint16;
}

template <>
ffi_type * get_default_ffi_type(int32_t) {
    return &ffi_type_sint32;
}

template <>
ffi_type * get_default_ffi_type(int64_t) {
    return &ffi_type_sint64;
}

template <>
ffi_type * get_default_ffi_type(float) {
    return &ffi_type_float;
}

template <>
ffi_type * get_default_ffi_type(double) {
    return &ffi_type_double;
}

template <typename T>
static bool handle_pass_by_addr(
    ffi_resources<T> * ffi_arg_res,
    size_t value_slot,
    arg_type &p,
    ffi_type **&args,
    size_t arg_index,
    std::map<uint64_t, void *> &ffi_res,
    std::map<uint64_t, std::map<size_t, size_t>> &type_index_resindex)
{
    p.ffi_arg_type_original = p.ffi_arg_type;
    p.value_slot_original = value_slot;

    if (p.pass_by == ADDR) {
        args[arg_index] = get_default_ffi_type<void *>();
        auto ffi_arg_by_addr_res = get_ffi_res<void *>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<void *>(), true);
        void * res_address = nullptr;
        if (ffi_arg_res && ffi_arg_by_addr_res &&
            ffi_arg_res->get(value_slot, res_address) &&
            ffi_arg_by_addr_res->set(res_address, value_slot)) {

            p.ffi_arg_type = std::make_shared<ffi_type>();
            copy_ffi_type(p.ffi_arg_type, ffi_type_pointer);
            p.value_slot = value_slot;

            type_index_resindex[(uint64_t)(uint64_t *)args[arg_index]][arg_index] = value_slot;
            return true;
        } else {
            return false;
        }
    } else {
        p.value_slot = value_slot;
    }
    return true;
}

template <typename T, typename ERL_API_T=T, typename get_nif_term_func = int(*)(ErlNifEnv *, ERL_NIF_TERM, ERL_API_T*)>
static bool handle_arg(
        ErlNifEnv *env,
        get_nif_term_func get_nif_term_value,
        arg_type &p,
        ffi_type **&args,
        size_t arg_index,
        std::map<uint64_t, void *> &ffi_res,
        std::map<uint64_t, std::map<size_t, size_t>> &type_index_resindex, T _unused=0)
{
    ERL_API_T value;
    if (args != nullptr && get_nif_term_value(env, p.term, &value)) {
        args[arg_index] = get_default_ffi_type<T>();
        auto ffi_arg_res = get_ffi_res<T>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<T>(), true);
        size_t value_slot = 0;
        if (ffi_arg_res && ffi_arg_res->set((T)value, value_slot)) {
            type_index_resindex[(uint64_t)(uint64_t *)args[arg_index]][arg_index] = value_slot;
            return handle_pass_by_addr<T>(ffi_arg_res, value_slot, p, args, arg_index, ffi_res, type_index_resindex);
        } else {
            printf("[debug] cannot save value for %s to ffi_arg_res\r\n", p.type.c_str());
            return false;
        }
    } else {
        printf("[debug] cannot get value for %s\r\n", p.type.c_str());
        return false;
    }
}

static bool handle_c_ptr_arg(
        ErlNifEnv *env,
        int(*get_nif_term_value)(ErlNifEnv *, ERL_NIF_TERM, int64_t *),
        arg_type &p,
        ffi_type **&args,
        size_t arg_index,
        std::map<uint64_t, void *> &ffi_res,
        std::map<uint64_t, std::map<size_t, size_t>> &type_index_resindex)
{
    auto ffi_arg_res = get_ffi_res<void *>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<void *>(), true);
    // if enif_inspect_binary succeeded,
    // `binary.data` will live until we return to erlang
    ErlNifBinary binary;
    uint64_t ptr;
    size_t value_slot = 0;
    std::string null_c_ptr;
    OtterSymbol *symbol_res = nullptr;
    if (args == nullptr) {
        return false;
    }

    if (enif_get_resource(env, p.term, OtterSymbol::type, (void **)&symbol_res) && symbol_res) {
        void *symbol = symbol_res->val;
        args[arg_index] = &ffi_type_pointer;
        if (ffi_arg_res == nullptr || !ffi_arg_res->set(symbol, value_slot)) {
            return false;
        }
        type_index_resindex[(uint64_t)(uint64_t *)args[arg_index]][arg_index] = value_slot;
    } else if (enif_inspect_binary(env, p.term, &binary)) {
        args[arg_index] = &ffi_type_pointer;
        if (ffi_arg_res == nullptr || !ffi_arg_res->set(binary.data, value_slot)) {
            return false;
        }
        type_index_resindex[(uint64_t)(uint64_t *)args[arg_index]][arg_index] = value_slot;
    } else if (erlang::nif::get_atom(env, p.term, null_c_ptr) &&
               (null_c_ptr == "NULL" || null_c_ptr == "nil")) {
        args[arg_index] = &ffi_type_pointer;
        if (ffi_arg_res == nullptr || !ffi_arg_res->set(nullptr, value_slot)) {
            return false;
        }
        type_index_resindex[(uint64_t)(uint64_t *)args[arg_index]][arg_index] = value_slot;
    } else if (erlang::nif::get_uint64(env, p.term, &ptr)) {
        args[arg_index] = &ffi_type_pointer;
        if (ffi_arg_res == nullptr || !ffi_arg_res->set((void *)(int64_t *)(ptr), value_slot)) {
            return false;
        }
        type_index_resindex[(uint64_t)(uint64_t *)args[arg_index]][arg_index] = value_slot;
    } else {
        return false;
    }
    return handle_pass_by_addr<void *>(ffi_arg_res, value_slot, p, args, arg_index, ffi_res, type_index_resindex);
}

template <typename T, typename ERL_API_T=T, typename make_nif_term_func = ERL_NIF_TERM(*)(ErlNifEnv *, ERL_API_T)>
static bool handle_out_values(
    ErlNifEnv *env,
    make_nif_term_func make_term,
    arg_type &p,
    std::map<uint64_t, void *> &ffi_res,
    ERL_NIF_TERM &out_term,
    std::string &out_error)
{
    auto ffi_arg_res = get_ffi_res<T>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<T>(), true);
    T value;
    if (ffi_arg_res && ffi_arg_res->get_value(p.value_slot_original, value)) {
        out_term = make_term(env, value);
        return true;
    } else {
        out_error = "cannot get original argument storage";
        return false;
    }
}

static bool handle_out_values(ErlNifEnv *env, arg_type &p, std::map<uint64_t, void *> &ffi_res, ERL_NIF_TERM &out_term, std::string &out_error) {
    bool ok = false;
    out_error = "not implemented for type " + p.type;
    if (p.type == "c_ptr") {
        // need to handle pointers with care
        // copy data, but how many bytes should we copy?
    } else if (p.type == "s8") {
        ok = handle_out_values<int8_t, int32_t>(env, enif_make_int, p, ffi_res, out_term, out_error);
    } else if (p.type == "s16") {
        ok = handle_out_values<int16_t, int32_t>(env, enif_make_int, p, ffi_res, out_term, out_error);
    } else if (p.type == "s32") {
        ok = handle_out_values<int32_t, int32_t>(env, enif_make_int, p, ffi_res, out_term, out_error);
    } else if (p.type == "s64") {
        ok = handle_out_values<int64_t, int64_t>(env, enif_make_long, p, ffi_res, out_term, out_error);
    } else if (p.type == "u8") {
        ok = handle_out_values<uint8_t, uint32_t>(env, enif_make_uint, p, ffi_res, out_term, out_error);
    } else if (p.type == "u16") {
        ok = handle_out_values<uint16_t, uint32_t>(env, enif_make_uint, p, ffi_res, out_term, out_error);
    } else if (p.type == "u32") {
        ok = handle_out_values<uint32_t, uint32_t>(env, enif_make_uint, p, ffi_res, out_term, out_error);
    } else if (p.type == "u64") {
        ok = handle_out_values<uint64_t, uint64_t>(env, enif_make_ulong, p, ffi_res, out_term, out_error);
    } else if (p.type == "f32") {
        ok = handle_out_values<float, double>(env, enif_make_double, p, ffi_res, out_term, out_error);
    } else if (p.type == "f64") {
        ok = handle_out_values<double, double>(env, enif_make_double, p, ffi_res, out_term, out_error);
    } else if (p.type == "va_args") {
        // todo: handle va_args
    } else {
        bool is_struct = false;
        if (is_struct) {
            // todo: struct
        } else {
            // todo: other types
            printf("[debug] type: %s\r\n", p.type.c_str());
        }
        ok = false;
    }
    return ok;
}

static bool handle_va_args(
        ErlNifEnv *env,
        std::vector<arg_type> &var_args_with_type,
        std::vector<size_t> &out_value_indexes,
        arg_type &va_args,
        ffi_type **&args,
        size_t va_arg_index,
        std::map<uint64_t, void *> &ffi_res,
        std::map<uint64_t, std::map<size_t, size_t>> &type_index_resindex,
        std::vector<std::shared_ptr<FFIStructTypeWrapper>> &wrappers,
        std::string &error_msg);

static bool process_args_with_type(
    ErlNifEnv *env,
    std::vector<arg_type> &args_with_type,
    size_t &num_fixed_args,
    std::vector<size_t> &out_value_indexes,
    std::vector<std::shared_ptr<FFIStructTypeWrapper>> &wrappers,
    ffi_type **&args,
    size_t arg_offset,
    std::map<uint64_t, void *> &ffi_res,
    std::map<uint64_t, std::map<size_t, size_t>> &type_index_resindex,
    std::string &error_msg,
    bool allow_va_args)
{
    // :va_args can only appear in the last position
    // note that :va_args is a pseudo-type in Otter
    // where :va_list is the real underlying type defined in stdarg.h
    // and :va_list is the one you can pass around in C
    bool ok = true;
    bool last_is_va_args = false;
    size_t arg_failed = 0;

    size_t total_size = args_with_type.size();
    for (size_t i = arg_offset; i < total_size; ++i) {
        if (args == nullptr) {
            error_msg = "cannot allocate memory for ffi args";
            ok = false;
            break;
        }

        if (last_is_va_args) {
            error_msg = "va_args can only appear at the end of the input argument list";
            ok = false;
            break;
        }

        num_fixed_args += 1;
        arg_failed = i;
        auto &p = args_with_type[i];
        if (p.is_out) {
            out_value_indexes.push_back(i);
        }

        if (p.type == "c_ptr") {
            if (!handle_c_ptr_arg(env, erlang::nif::get_sint64, p, args, i, ffi_res, type_index_resindex)) {
                ok = false;
                break;
            }
        } else if (p.type == "s8") {
            if (!handle_arg<int8_t, int>(env, erlang::nif::get_sint, p, args, i, ffi_res, type_index_resindex)) {
                ok = false;
                break;
            }
        } else if (p.type == "s16") {
            if (!handle_arg<int16_t, int>(env, erlang::nif::get_sint, p, args, i, ffi_res, type_index_resindex)) {
                ok = false;
                break;
            }
        } else if (p.type == "s32") {
            if (!handle_arg<int32_t, int>(env, erlang::nif::get_sint, p, args, i, ffi_res, type_index_resindex)) {
                ok = false;
                break;
            }
        } else if (p.type == "s64") {
            if (!handle_arg<int64_t, int64_t>(env, erlang::nif::get_sint64, p, args, i, ffi_res, type_index_resindex)) {
                ok = false;
                break;
            }
        } else if (p.type == "u8") {
            if (!handle_arg<uint8_t, unsigned int>(env, erlang::nif::get_uint, p, args, i, ffi_res, type_index_resindex)) {
                ok = false;
                break;
            }
        } else if (p.type == "u16") {
            if (!handle_arg<uint16_t, unsigned int>(env, erlang::nif::get_uint, p, args, i, ffi_res, type_index_resindex)) {
                ok = false;
                break;
            }
        } else if (p.type == "u32") {
            if (!handle_arg<uint32_t, unsigned int>(env, erlang::nif::get_uint, p, args, i, ffi_res, type_index_resindex)) {
                ok = false;
                break;
            }
        } else if (p.type == "u64") {
            if (!handle_arg<uint64_t, uint64_t>(env, erlang::nif::get_uint64, p, args, i, ffi_res, type_index_resindex)) {
                ok = false;
                break;
            }
        } else if (p.type == "f32") {
            if (!handle_arg<float, double>(env, erlang::nif::get_f64, p, args, i, ffi_res, type_index_resindex)) {
                ok = false;
                break;
            }
        } else if (p.type == "f64") {
            if (!handle_arg<double, double>(env, erlang::nif::get_f64, p, args, i, ffi_res, type_index_resindex)) {
                ok = false;
                break;
            }
        } else if (p.type == "va_args") {
            if (allow_va_args) {
                // notes:
                //  - `error_msg` will be set in handle_va_args
                //  - `args` will be realloc'ed in handle_va_args
                if (!handle_va_args(env, args_with_type, out_value_indexes, p, args, i, ffi_res, type_index_resindex, wrappers, error_msg)) {
                    ok = false;
                    break;
                } else {
                    last_is_va_args = true;
                    num_fixed_args -= 1;
                }
            } else {
                error_msg = "type :va_args only allow to appear once as the type of the last argument";
                ok = false;
                break;
            }
        } else {
            auto wrapper_it = FFIStructTypeWrapper::create_from_tuple(env, p.type_term, wrappers);
            if (wrapper_it != nullptr) {
                args[i] = &wrapper_it->ffi_struct_type;
                // https://www.erlang.org/doc/man/erl_nif.html#enif_get_resource
                // enif_get_resource does not add a reference to the resource object.
                // However, the pointer received in *objp is guaranteed to be valid
                // at least as long as the resource handle term is valid.
                void *resource_obj_ptr = nullptr;
                if (!enif_get_resource(env, p.term, wrapper_it->resource_type, &resource_obj_ptr)) {
                    error_msg = "failed to get resource for struct: " + wrapper_it->struct_id;
                    ok = false;
                    break;
                }
                p.is_struct_instance = true;
                p.struct_data = resource_obj_ptr;

                wrappers.push_back(wrapper_it);
            } else {
                // todo: other types
                printf("[debug] todo: arg%zu, type: %s\r\n", i, p.type.c_str());
                error_msg = "process_args_with_type =: not implemented for type" + p.type;
                ok = false;
                break;
            }
        }
    }

    return ok;
}

bool handle_va_args(
        ErlNifEnv *env,
        std::vector<arg_type> &args_with_type,
        std::vector<size_t> &out_value_indexes,
        arg_type &va_args,
        ffi_type **&args,
        size_t va_arg_index,
        std::map<uint64_t, void *> &ffi_res,
        std::map<uint64_t, std::map<size_t, size_t>> &type_index_resindex,
        std::vector<std::shared_ptr<FFIStructTypeWrapper>> &wrappers,
        std::string &error_msg)
{
    // va_args.term should be a list with elements like
    //   1. just a value
    //   2. {value, :type}
    //   3. {value, %{type: TYPE, extra: EXTRA}}

    // call pop_back because the last one is :va_args
    args_with_type.pop_back();
    if (get_args_with_type(env, va_args.term, args_with_type, va_arg_index, wrappers)) {
        size_t total_args = args_with_type.size();
        void * new_args = realloc((void *)args, sizeof(ffi_type *) * total_args);
        if (new_args == nullptr) {
            free((void *)args);
            args = nullptr;
            error_msg = "cannot allocate enough memory for function arguments";
            return false;
        }
        args = (ffi_type **)new_args;

        size_t num_var_args = 0;
        return process_args_with_type(env, args_with_type, num_var_args, out_value_indexes, wrappers, args, va_arg_index, ffi_res, type_index_resindex, error_msg, false);
    } else {
        printf("cannot get va_args!!!!\r\n");
        return false;
    }
}

static ERL_NIF_TERM otter_invoke(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    if (argc != 3) {
        return enif_make_badarg(env);
    }

    std::vector<std::shared_ptr<FFIStructTypeWrapper>> wrappers;
    OtterSymbol *symbol_res = nullptr;

    std::string return_type;
    std::shared_ptr<FFIStructTypeWrapper> struct_return_type = nullptr;

    std::vector<arg_type> args_with_type;
    size_t num_fixed_args = 0;
    size_t num_variadic_args = 0;
    if (erlang::nif::get_atom(env, argv[1], return_type) && !return_type.empty()) {
        // Do nothing
    } else {
        struct_return_type = FFIStructTypeWrapper::create_from_tuple(env, argv[1], wrappers);
        if (struct_return_type == nullptr) {
            return erlang::nif::error(env, "fail to create_from_tuple");
        }
        wrappers.push_back(struct_return_type);
    }

    if (enif_get_resource(env, argv[0], OtterSymbol::type, (void **)&symbol_res) && symbol_res) {
        if (!get_args_with_type(env, argv[2], args_with_type, 0, wrappers)) {
            return erlang::nif::error(env, "fail to get args_with_type");
        }

        void *symbol = symbol_res->val;
        if (symbol != nullptr) {
            ffi_cif cif;
            std::vector<size_t> out_value_indexes;

            ffi_type ** args = nullptr;
            void **values = nullptr;
            if (args_with_type.size() > 0) {
                args = (ffi_type **)malloc(sizeof(ffi_type *) * args_with_type.size());
                if (args == nullptr) {
                    return erlang::nif::error(env, "fail to allocate memory for ffi args");
                }
                memset((void*)args, 0, sizeof(ffi_type *) * args_with_type.size());
            }
            
            ffi_type * ffi_return_type = nullptr;
            size_t return_object_size = sizeof(void *);
            void *rc = nullptr;
            std::string error_msg;

            // type_index_resindex (resource pool)
            //   key: type
            //   value:
            //     key: index in the above variable values
            //     value: index in ffi_resources
            std::map<uint64_t, std::map<size_t, size_t>> type_index_resindex;
            std::map<uint64_t, void *> ffi_res;
            get_ffi_res<void *>(ffi_res, (uint64_t)(uint64_t *)&ffi_type_pointer, true);

            int ready = 1;
            if ((ready = process_args_with_type(env, args_with_type, num_fixed_args, out_value_indexes, wrappers, args, 0, ffi_res, type_index_resindex, error_msg, true))) {
                num_variadic_args = args_with_type.size() - num_fixed_args;
            }

            if (ready) {
                if (struct_return_type) {
                    ffi_return_type = &struct_return_type->ffi_struct_type;
                } else if (str2ffi_type.find(return_type) != str2ffi_type.end()) {
                    ffi_return_type = str2ffi_type[return_type];
                } else {
                    ready = 0;
                    error_msg = "failed to get return_type: " + return_type;
                }
            }

            // verify ffi type info for args
            for (size_t i = 0; i < num_fixed_args + num_variadic_args; i++) {
                if (args == nullptr || args[i] == nullptr) {
                    ready = 0;
                    error_msg = "input argument type missing for arg at index " + std::to_string(i);
                    break;
                }
            }

            if (ready && ffi_return_type == nullptr) {
                ready = 0;
                error_msg = "ffi return type is not initialised properly";
            }

            ERL_NIF_TERM ret;
            ffi_status prep_status;
            if (ready) {
                if (num_variadic_args > 0) {
                    prep_status = ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI, num_fixed_args,
                                                   num_fixed_args + num_variadic_args, ffi_return_type, args);
                } else {
                    prep_status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, num_fixed_args, ffi_return_type, args);
                }
                if (prep_status != FFI_OK) {
                    ready = 0;
                    // overwrite previous error message if there wasn't one
                    error_msg = "ffi_prep_cif failed";
                }
            }

            ERL_NIF_TERM out_list;
            if (ready) {
                // size here gets updated by ffi_prep_cif
                return_object_size = ffi_return_type->size;
                if (return_object_size > 0) {
                    // based on libffi docs
                    // rc should be at least as large as sizeof(ffi_arg *)
                    size_t rc_size = return_object_size;
                    if (rc_size < sizeof(ffi_arg *)) {
                        rc_size = sizeof(ffi_arg *);
                    }
                    rc = malloc(rc_size);
                    if (rc == nullptr) {
                        ready = 0;
                        error_msg = "cannot allocate memory for ffi return value";
                    }
                }

                if (ready) {
                    values = (void **)malloc(sizeof(void *) * (num_fixed_args + num_variadic_args));
                    if (values == nullptr) {
                        if (args) {
                            free((void *)args);
                        }

                        for (size_t i = 0; i < wrappers.size(); ++i) {
                            auto wrapper_it = wrappers[i];
                            wrapper_it.reset();
                        }
                        wrappers.clear();
                        return erlang::nif::error(env, "fail to allocate memory for ffi values");
                    }
                    memset((void*)values, 0, sizeof(void *) * (num_fixed_args + num_variadic_args));

                    // fill values after ffi_prep_cif succeeded
                    for (size_t i = 0; ready && i < (num_fixed_args + num_variadic_args); i++) {
                        if (args == nullptr) {
                            ready = 0;
                            error_msg = "input argument type info missing for arg at index " + std::to_string(i);
                            break;
                        }

                        if (type_index_resindex.find((uint64_t)(uint64_t *)args[i]) != type_index_resindex.end()) {
                            auto &index_resindex = type_index_resindex[(uint64_t)(uint64_t *)args[i]];
                            auto slot = index_resindex[i];
                            if (args[i] == &ffi_type_pointer) {
                                auto ffi_arg_res = get_ffi_res<void *>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<void *>(), true);
                                if (!(ready = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                                    error_msg = "invalid ffi resource for a void * arg";
                                    break;
                                }
                            } else if (args[i] == &ffi_type_uint8) {
                                auto ffi_arg_res = get_ffi_res<uint8_t>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<uint8_t>(), true);
                                if (!(ready = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                                    error_msg = "invalid ffi resource for a uint8_t arg";
                                    break;
                                }
                            } else if (args[i] == &ffi_type_uint16) {
                                auto ffi_arg_res = get_ffi_res<uint16_t>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<uint16_t>(), true);
                                if (!(ready = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                                    error_msg = "invalid ffi resource for a uint16_t arg";
                                    break;
                                }
                            } else if (args[i] == &ffi_type_uint32) {
                                auto ffi_arg_res = get_ffi_res<uint32_t>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<uint32_t>(), true);
                                if (!(ready = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                                    error_msg = "invalid ffi resource for a uint32_t arg";
                                    break;
                                }
                            } else if (args[i] == &ffi_type_uint64) {
                                auto ffi_arg_res = get_ffi_res<uint64_t>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<uint64_t>(), true);
                                if (!(ready = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                                    error_msg = "invalid ffi resource for a uint64_t arg";
                                    break;
                                }
                            } else if (args[i] == &ffi_type_sint8) {
                                auto ffi_arg_res = get_ffi_res<int8_t>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<int8_t>(), true);
                                if (!(ready = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                                    error_msg = "invalid ffi resource for an int8_t arg";
                                    break;
                                }
                            } else if (args[i] == &ffi_type_sint16) {
                                auto ffi_arg_res = get_ffi_res<int16_t>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<int16_t>(), true);
                                if (!(ready = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                                    error_msg = "invalid ffi resource for an int16_t arg";
                                    break;
                                }
                            } else if (args[i] == &ffi_type_sint32) {
                                auto ffi_arg_res = get_ffi_res<int32_t>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<int32_t>(), true);
                                if (!(ready = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                                    error_msg = "invalid ffi resource for an int32_t arg";
                                    break;
                                }
                            } else if (args[i] == &ffi_type_sint64) {
                                auto ffi_arg_res = get_ffi_res<int64_t>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<int64_t>(), true);
                                if (!(ready = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                                    error_msg = "invalid ffi resource for an int64_t arg";
                                    break;
                                }
                            } else if (args[i] == &ffi_type_float) {
                                auto ffi_arg_res = get_ffi_res<float>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<float>(), true);
                                if (!(ready = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                                    error_msg = "invalid ffi resource for a float arg";
                                    break;
                                }
                            } else if (args[i] == &ffi_type_double) {
                                auto ffi_arg_res = get_ffi_res<double>(ffi_res, (uint64_t)(uint64_t *)get_default_ffi_type<double>(), true);
                                if (!(ready = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                                    error_msg = "invalid ffi resource for a double arg";
                                    break;
                                }
                            }
                        } else {
                            auto &p = args_with_type[i];
                            if (p.is_struct_instance) {
                                if (p.struct_data) {
                                    values[i] = p.struct_data;
                                } else {
                                    ready = 0;
                                    break;
                                }
                            }
                        }
                    }

                    for (size_t i = 0; ready && i < (num_fixed_args + num_variadic_args); i++) {
                        if (values == nullptr || values[i] == nullptr) {
                            ready = 0;
                            error_msg = "input argument value missing for arg at index " + std::to_string(i);
                            break;
                        }
                    }
                    
                    if (ready) {
                        ffi_call(&cif, (void (*)())symbol, rc, values);
                    }

                    // has out values, copy them to erlang
                    if (out_value_indexes.size() > 0) {
                        std::vector<ERL_NIF_TERM> out_terms;
                        for (auto &index : out_value_indexes) {
                            auto &p = args_with_type[index];
                            ERL_NIF_TERM out;
                            std::string out_error;
                            if (!handle_out_values(env, p, ffi_res, out, out_error)) {
                                out = erlang::nif::error(env, out_error.c_str());
                            }
                            out_terms.push_back(out);
                        }
                        out_list = enif_make_list_from_array(env, (const ERL_NIF_TERM *)out_terms.data(), (unsigned)out_value_indexes.size());
                    }

                    // clean up everything used as input arguments for ffi_call
                    free_ffi_res<void *>(ffi_res, (uint64_t)(uint64_t *)&ffi_type_pointer);
                    free_ffi_res<uint8_t>(ffi_res, (uint64_t)(uint64_t *)&ffi_type_uint8);
                    free_ffi_res<uint16_t>(ffi_res, (uint64_t)(uint64_t *)&ffi_type_uint16);
                    free_ffi_res<uint32_t>(ffi_res, (uint64_t)(uint64_t *)&ffi_type_uint32);
                    free_ffi_res<uint64_t>(ffi_res, (uint64_t)(uint64_t *)&ffi_type_uint64);
                    free_ffi_res<int8_t>(ffi_res, (uint64_t)(uint64_t *)&ffi_type_sint8);
                    free_ffi_res<int16_t>(ffi_res, (uint64_t)(uint64_t *)&ffi_type_sint16);
                    free_ffi_res<int32_t>(ffi_res, (uint64_t)(uint64_t *)&ffi_type_sint32);
                    free_ffi_res<int64_t>(ffi_res, (uint64_t)(uint64_t *)&ffi_type_sint64);
                    free_ffi_res<float>(ffi_res, (uint64_t)(uint64_t *)&ffi_type_float);
                    free_ffi_res<double>(ffi_res, (uint64_t)(uint64_t *)&ffi_type_double);
                    for (size_t i = 0; i < wrappers.size(); ++i) {
                        auto wrapper_it = wrappers[i];
                        if (wrapper_it.get() != struct_return_type.get()) {
                            wrapper_it.reset();
                        }
                    }
                    
                    if (args) {
                        free((void *)args);
                        args = nullptr;
                    }
                    
                    if (values) {
                        free((void *)values);
                        values = nullptr;
                    }
                }
            }

            if (!error_msg.empty()) {
                struct_return_type.reset();
                return erlang::nif::error(env, error_msg.c_str());
            }

            if (return_object_size && rc) {
                if (struct_return_type) {
                    if (!make_ffi_struct_resource(env, return_object_size, struct_return_type->resource_type, rc, ret)) {
                        if (rc) {
                            free(rc);
                            rc = nullptr;
                        }
                        struct_return_type.reset();
                        return erlang::nif::error(env, "cannot make_ffi_struct_resource");
                    }
                } else if (return_type == "void") {
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
                    if (rc) {
                        free(rc);
                        rc = nullptr;
                    }
                    error_msg = "return_type " + return_type + " is not implemented yet";
                    return erlang::nif::error(env, error_msg.c_str());
                }
            } else {
                ret = erlang::nif::ok(env);
            }

            if (rc) {
                free(rc);
                rc = nullptr;
            }

            if (out_value_indexes.size() > 0) {
                return erlang::nif::ok(env, enif_make_tuple2(env, ret, out_list));
            } else {
                return erlang::nif::ok(env, ret);
            }
        } else {
            return erlang::nif::error(env, "resource has an invalid handle");
        }
    } else {
        return erlang::nif::error(env, "cannot get symbol");
    }
}

static int on_load(ErlNifEnv *env, void **, ERL_NIF_TERM) {
    ErlNifResourceType *rt;
    rt = enif_open_resource_type(env, "Elixir.Otter.Nif", "OtterHandle", destruct_otter_handle, ERL_NIF_RT_CREATE, NULL);
    if (!rt) {
        return -1;
    }
    erlang_nif_res<void *>::type = rt;
    return 0;
}

static int on_reload(ErlNifEnv *, void **, ERL_NIF_TERM) { return 0; }

static int on_upgrade(ErlNifEnv *, void **, void **, ERL_NIF_TERM) { return 0; }

static ErlNifFunc nif_functions[] = {
    {"dlopen", 2, otter_dlopen, 0},
    {"dlclose", 1, otter_dlclose, 0},
    {"dlsym", 2, otter_dlsym, 0},
    {"symbol_to_address", 1, otter_symbol_to_address, 0},
    {"address_to_symbol", 1, otter_address_to_symbol, 0},
    {"erl_nif_env", 0, otter_erl_nif_env, 0},
    {"invoke", 3, otter_invoke, 0},
};

ERL_NIF_INIT(Elixir.Otter.Nif, nif_functions, on_load, on_reload, on_upgrade, NULL)

#if defined(__GNUC__)
#pragma GCC visibility push(default)
#endif
