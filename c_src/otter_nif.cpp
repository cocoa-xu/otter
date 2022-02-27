#include <dlfcn.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>

#include <erl_nif.h>
#include <ffi.h>

#include <iostream>
#include <mutex>
#include <memory>

#include "nif_utils.hpp"

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
    *res = (erlang_nif_res<R> *)enif_alloc_resource(erlang_nif_res<R>::type, sizeof(erlang_nif_res<R>));
    return (*res != nullptr);
}

using OtterHandle = erlang_nif_res<void *>;
using OtterSymbol = erlang_nif_res<void *>;

// key: shared library name/path
// value: handle returned by dlopen
static std::map<std::string, OtterHandle *> opened_handles;
// key: handle returned by dlopen
// value:
//   key: function name
//   value: function address
static std::map<OtterHandle *, std::map<std::string, OtterSymbol *>> found_symbols;
// key: basic type
// value: ffi_type
static std::map<std::string, ffi_type *> str2ffi_type = {
    {"u8", &ffi_type_uint8},   {"u16", &ffi_type_uint16},
    {"u32", &ffi_type_uint32}, {"u64", &ffi_type_uint64},
    {"s8", &ffi_type_sint8},   {"s16", &ffi_type_sint16},
    {"s32", &ffi_type_sint32}, {"s64", &ffi_type_sint64},
    {"f32", &ffi_type_float},  {"f64", &ffi_type_double},
    {"void", &ffi_type_void},
};
// global nullptr so that we can directly set
// ffi_type.elements to an array [null_ptr_g]
static const void * null_ptr_g = nullptr;
static thread_local jmp_buf jmp_buf_g;

static void resource_dtor(ErlNifEnv *env, void *) {}

// Helper function for FFIResource
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

class FFIStructTypeWrapper {
public:
    /// Constructor
    /// @param field_size number of fields in the struct
    FFIStructTypeWrapper(size_t field_size) {
        // size and alignment will be set by libffi
        // initialize them to zero
        ffi_struct_type.size = 0;
        ffi_struct_type.alignment = 0;
        ffi_struct_type.type = FFI_TYPE_STRUCT;
        // field_size + 1: elements has to be a null-terminated array
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

    static std::map<std::string, ErlNifResourceType *> struct_resource_type_registry;
    static std::mutex struct_resource_type_registry_lock;

    // NOTE: the basic idea here we register a resource type for each struct type,
    // identified by struct_id.
    static ErlNifResourceType * register_ffi_struct_resource_type(ErlNifEnv *env, std::string &struct_id) {
        auto resource_type = enif_open_resource_type(
          env, "Elixir.Otter.Nif", ("OTTER_STRUCT_" + struct_id).data(), resource_dtor,
          ERL_NIF_RT_CREATE, nullptr);
        return resource_type;
    }

    static bool make_ffi_struct_resource(
        ErlNifEnv *env,
        size_t return_object_size,
        ErlNifResourceType *resource_type,
        void *result,
        ERL_NIF_TERM &ret)
    {
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

    static ErlNifResourceType * get_ffi_struct_resource_type(ErlNifEnv *env, std::string &struct_id) {
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

    ffi_type ffi_struct_type;
    ErlNifResourceType *resource_type;
    std::string struct_id;
    std::vector<std::shared_ptr<ffi_type>> field_types;
};

std::mutex FFIStructTypeWrapper::struct_resource_type_registry_lock;
std::map<std::string, ErlNifResourceType *> FFIStructTypeWrapper::struct_resource_type_registry;

class FFIArgType {
public:
    enum FFIArgPassingType {
        VALUE,
        ADDR,
        REF,
    };

    FFIArgType(ERL_NIF_TERM term_, ERL_NIF_TERM type_term_, const std::string type_, uint64_t size_, ERL_NIF_TERM info) :
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

    // if size > 0,
    // then the value is a nd-array
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
    FFIArgPassingType pass_by;
    bool is_out;
    bool is_va_args;

    bool is_struct_instance;
    // borrowed data from erlang vm
    // do not free struct_data
    void * struct_data;
};

template<typename T>
class FFIResource {
public:
    FFIResource(size_t default_cap=32) : invalid(false), increase_by(default_cap), cap(0), next(0) {
    }

    ~FFIResource() {
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

/// Wrap everything we need to make an FFI call
class FFICall {
public:
    /// Constructor
    /// @param env Erlang Nif environment
    /// @param symbol_term Contains the address of the symbol (function)
    /// @param return_type_term Function return type
    /// @param arg_types_term A list of 2-tuple {arg_value, arg_type_info}
    ///        `arg_value` should be basic types
    FFICall(ErlNifEnv *env, ERL_NIF_TERM symbol, ERL_NIF_TERM return_type, ERL_NIF_TERM arg_types_term) noexcept {
        this->env_ = env;
        this->symbol_ = symbol;
        this->return_type_ = return_type;
        this->arg_types_term_ = arg_types_term;
    }

    ~FFICall() {
        // clean up everything used as input arguments for ffi_call
        for (auto &iter : ffi_res) {
            auto p = (FFIResource<void *> *)iter.second;
            if (p) delete p;
        }
        struct_wrapper.clear();

        if (args) {
            free((void *)args);
            args = nullptr;
        }
        if (values) {
            free((void *)values);
            values = nullptr;
        }
        if (rc) {
            free(rc);
            rc = nullptr;
        }
    }

    /// Invoke function with input arguments
    /// @param return_value out. The return value of the function invoked.
    /// @param out_values out. New values of the arguments that are passed by reference or marked as output
    /// @param error_msg out. Error message if encountered error
    /// @return true if successfully invoked the function
    ///         Otherwise, false. And error message will be in `error_msg`.
    bool call(ERL_NIF_TERM &return_value, ERL_NIF_TERM &out_values, std::string &error_msg) noexcept {
        if (env_ == nullptr) {
            error_msg = "ErlNifEnv variable is nullptr";
            return false;
        }

        // get the symbol
        OtterSymbol *symbol_res = nullptr;
        if (!enif_get_resource(env_, symbol_, OtterSymbol::type, (void **)&symbol_res) && symbol_res && symbol_res->val) {
            error_msg = "invalid symbol";
            return false;
        }

        // get return type
        if (!prepare_ffi_return_type(error_msg)) {
            return false;
        }

        // first pass of parsing the arg_types_term_
        if (!_get_args_with_type(arg_types_term_, 0, args_with_type_, error_msg)) {
            error_msg = "failed to parse the args_with_type list";
            return false;
        }

        // allocate memory for args
        // args will be used in `_process_args_with_type`
        if (args_with_type_.size() > 0) {
            size_t args_size = sizeof(ffi_type *) * args_with_type_.size();
            void * new_args = (ffi_type **)realloc(args, args_size);
            if (new_args == nullptr) {
                error_msg = "fail to allocate memory for ffi args";
                return false;
            }
            args = (ffi_type **)new_args;
            memset((void *)args, 0, args_size);
        }

        // process parsed args_with_type_ array
        // va_args will be processed in `_process_args_with_type`
        if (!_process_args_with_type(num_fixed_args, 0, error_msg, true)) {
            return false;
        }

        // args_with_type_ will now be a "flatten" list of arguments
        // get number of variadic arguments
        num_variadic_args = args_with_type_.size() - num_fixed_args;

        // verify ffi type info for args
        bool ready = true;
        for (size_t i = 0; i < num_fixed_args + num_variadic_args; i++) {
            if (args == nullptr || args[i] == nullptr) {
                ready = 0;
                error_msg = "input argument type missing for arg at index " + std::to_string(i);
                break;
            }
        }

        // prepare ffi call
        ffi_status prep_status;
        if (ready) {
            if (num_variadic_args > 0) {
                prep_status = ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI, num_fixed_args,
                                               num_fixed_args + num_variadic_args, ffi_return_type, args);
            } else {
                prep_status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, num_fixed_args, ffi_return_type, args);
            }

            if (prep_status != FFI_OK) {
                ready = false;
                error_msg = "ffi_prep_cif failed";
            }
        }

        // fill values after ffi_prep_cif succeeded
        if (ready) {
            ready = fill_values(error_msg);

            // verify ffi type info for values
            for (size_t i = 0; ready && i < (num_fixed_args + num_variadic_args); i++) {
                if (values == nullptr || values[i] == nullptr) {
                    ready = false;
                    error_msg = "input argument value missing for arg at index " + std::to_string(i);
                    break;
                }
            }
        }

        if (!ready) {
            return ready;
        }

        // size here gets updated by ffi_prep_cif
        size_t return_object_size = ffi_return_type->size;
        if (return_object_size > 0) {
            // based on libffi docs
            // rc should be at least as large as sizeof(ffi_arg *)
            size_t rc_size = return_object_size;
            if (rc_size < sizeof(ffi_arg *)) {
                rc_size = sizeof(ffi_arg *);
            }
            void * new_rc = realloc(rc, rc_size);
            if (new_rc == nullptr) {
                ready = false;
                error_msg = "cannot allocate memory for ffi return value";
            } else {
                rc = new_rc;
            }
        }

        if (ready) {
            ffi_call(&cif, (void (*)())symbol_res->val, rc, values);
        }

        // has out values, copy them to erlang
        if (out_value_indexes.size() > 0) {
            std::vector<ERL_NIF_TERM> out_terms;
            for (auto &index : out_value_indexes) {
                auto &p = args_with_type_[index];
                ERL_NIF_TERM out;
                std::string out_error;
                if (!handle_out_values(p, out, out_error)) {
                    out = erlang::nif::error(env_, out_error.c_str());
                }
                out_terms.push_back(out);
            }
            out_values = enif_make_list_from_array(env_, (const ERL_NIF_TERM *)out_terms.data(), (unsigned)out_value_indexes.size());
        }

        if (return_object_size && rc) {
            if (struct_return_type) {
                if (!FFIStructTypeWrapper::make_ffi_struct_resource(env_, return_object_size, struct_return_type->resource_type, rc, return_value)) {
                    struct_return_type.reset();
                    ready = false;
                    error_msg = "cannot make_ffi_struct_resource";
                }
            } else if (return_type == "void") {
                return_value = erlang::nif::ok(env_);
            } else if (return_type == "u8") {
                return_value = enif_make_uint(env_, *(uint8_t *)rc);
            } else if (return_type == "s8") {
                return_value = enif_make_int(env_, *(int8_t *)rc);
            } else if (return_type == "u16") {
                return_value = enif_make_uint(env_, *(uint16_t *)rc);
            } else if (return_type == "s16") {
                return_value = enif_make_int(env_, *(int16_t *)rc);
            } else if (return_type == "u32") {
                return_value = enif_make_uint(env_, *(uint32_t *)rc);
            } else if (return_type == "s32") {
                return_value = enif_make_int(env_, *(int32_t *)rc);
            } else if (return_type == "u64") {
                return_value = enif_make_uint64(env_, *(uint64_t *)rc);
            } else if (return_type == "s64") {
                return_value = enif_make_int64(env_, *(int64_t *)rc);
            } else if (return_type == "f32") {
                return_value = enif_make_double(env_, *(float *)rc);
            } else if (return_type == "f64") {
                return_value = enif_make_double(env_, *(double *)rc);
            } else if (return_type == "c_ptr") {
                return_value = enif_make_uint64(env_, (uint64_t)(*(uint64_t *)rc));
            } else {
                printf("[debug] todo: return_type: %s\r\n", return_type.c_str());
                error_msg = "return_type " + return_type + " is not implemented yet";
                ready = false;
            }
        } else {
            return_value = erlang::nif::ok(env_);
        }

        return ready;
    }

    bool prepare_ffi_return_type(std::string &error_msg) {
        if (erlang::nif::get_atom(env_, return_type_, return_type) && !return_type.empty()) {
            // Do nothing
        } else {
            struct_return_type = create_from_tuple(return_type_, error_msg);
            if (struct_return_type == nullptr) {
                error_msg = "fail to create struct wrapper";
                return false;
            }
            struct_wrapper.push_back(struct_return_type);
        }

        if (struct_return_type) {
            ffi_return_type = &struct_return_type->ffi_struct_type;
        } else if (str2ffi_type.find(return_type) != str2ffi_type.end()) {
            ffi_return_type = str2ffi_type[return_type];
        } else {
            error_msg = "failed to create return type for ffi_call";
            return false;
        }

        if (ffi_return_type == nullptr) {
            error_msg = "ffi return type is not initialised properly";
            return false;
        }

        return true;
    }

    bool fill_values(std::string &error_msg) {
        bool ok = true;

        void * new_values = realloc(values, sizeof(void *) * (num_fixed_args + num_variadic_args));
        if (new_values == nullptr) {
            error_msg = "fail to allocate memory for ffi values";
            return false;
        }
        values = (void **)new_values;
        memset((void *)values, 0, sizeof(void *) * (num_fixed_args + num_variadic_args));

        for (size_t i = 0; ok && i < (num_fixed_args + num_variadic_args); i++) {
            if (args == nullptr) {
                ok = 0;
                error_msg = "input argument type info missing for arg at index " + std::to_string(i);
                break;
            }

            if (type_index_resindex.find((uint64_t)(uint64_t *)args[i]) != type_index_resindex.end()) {
                auto &index_resindex = type_index_resindex[(uint64_t)(uint64_t *)args[i]];
                auto slot = index_resindex[i];
                if (args[i] == &ffi_type_pointer) {
                    auto ffi_arg_res = get_ffi_res<void *>();
                    if (!(ok = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                        error_msg = "invalid ffi resource for a void * arg";
                        break;
                    }
                } else if (args[i] == &ffi_type_uint8) {
                    auto ffi_arg_res = get_ffi_res<uint8_t>();
                    if (!(ok = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                        error_msg = "invalid ffi resource for a uint8_t arg";
                        break;
                    }
                } else if (args[i] == &ffi_type_uint16) {
                    auto ffi_arg_res = get_ffi_res<uint16_t>();
                    if (!(ok = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                        error_msg = "invalid ffi resource for a uint16_t arg";
                        break;
                    }
                } else if (args[i] == &ffi_type_uint32) {
                    auto ffi_arg_res = get_ffi_res<uint32_t>();
                    if (!(ok = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                        error_msg = "invalid ffi resource for a uint32_t arg";
                        break;
                    }
                } else if (args[i] == &ffi_type_uint64) {
                    auto ffi_arg_res = get_ffi_res<uint64_t>();
                    if (!(ok = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                        error_msg = "invalid ffi resource for a uint64_t arg";
                        break;
                    }
                } else if (args[i] == &ffi_type_sint8) {
                    auto ffi_arg_res = get_ffi_res<int8_t>();
                    if (!(ok = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                        error_msg = "invalid ffi resource for an int8_t arg";
                        break;
                    }
                } else if (args[i] == &ffi_type_sint16) {
                    auto ffi_arg_res = get_ffi_res<int16_t>();
                    if (!(ok = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                        error_msg = "invalid ffi resource for an int16_t arg";
                        break;
                    }
                } else if (args[i] == &ffi_type_sint32) {
                    auto ffi_arg_res = get_ffi_res<int32_t>();
                    if (!(ok = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                        error_msg = "invalid ffi resource for an int32_t arg";
                        break;
                    }
                } else if (args[i] == &ffi_type_sint64) {
                    auto ffi_arg_res = get_ffi_res<int64_t>();
                    if (!(ok = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                        error_msg = "invalid ffi resource for an int64_t arg";
                        break;
                    }
                } else if (args[i] == &ffi_type_float) {
                    auto ffi_arg_res = get_ffi_res<float>();
                    if (!(ok = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                        error_msg = "invalid ffi resource for a float arg";
                        break;
                    }
                } else if (args[i] == &ffi_type_double) {
                    auto ffi_arg_res = get_ffi_res<double>();
                    if (!(ok = (ffi_arg_res && ffi_arg_res->get(slot, values[i])))) {
                        error_msg = "invalid ffi resource for a double arg";
                        break;
                    }
                }
            } else {
                auto &p = args_with_type_[i];
                if (p->is_struct_instance) {
                    if (p->struct_data) {
                        values[i] = p->struct_data;
                    } else {
                        ok = 0;
                        error_msg = "struct data is nullptr";
                        break;
                    }
                }
            }
        }
        return ok;
    }

    static void copy_ffi_type(std::shared_ptr<ffi_type> &shared_ffi_type, ffi_type &copy_from) {
        shared_ffi_type->size = copy_from.size;
        shared_ffi_type->alignment = copy_from.alignment;
        shared_ffi_type->type = copy_from.type;
        shared_ffi_type->elements = nullptr;
    }

    /// Transform the args_with_type list to vector<arg_type>
    /// @param arg_types_term The `args_with_type` list (in Elixir). [{arg_value, type_info}, ...]
    /// @param prev_size Number of processed arguments in `args_with_type` array (C++).
    /// @param args_with_type out. The `args_with_type` array that stores transformed function input arguments.
    /// @param error_msg out. Error message if encountered error
    bool _get_args_with_type(ERL_NIF_TERM arg_types_term, size_t prev_size, std::vector<std::shared_ptr<FFIArgType>> &args_with_type, std::string &error_msg) noexcept {
        // `arg_types_term` shoud be a list in all cases
        // including for C function that takes no input arguments (i.e., will be [])
        if (!enif_is_list(env_, arg_types_term)) {
            // this is unlikely to happen unless the user directly
            // calls functions in Otter.Nif instead of using the
            // wrappers provided by Otter
            error_msg = "args_with_type is expected to be a list";
            return false;
        }

        // we should have no issues getting the length of a list
        unsigned int length;
        if (!enif_get_list_length(env_, arg_types_term, &length)) {
            error_msg = "enif_get_list_length: cannot get the length of args_with_type";
            return false;
        }

        // note that we might be inside a _get_args_with_type call
        // because we need to process va_args
        // if we were in a recursive call, then
        //   prev_size indicates the number of arguments we have already processed
        //     and we just need to reserve some more space for the ones in va_args
        // otherwise
        //   prev_size should be zero
        args_with_type.reserve(prev_size + length);

        // iterate through the list, arg_types_term
        ERL_NIF_TERM head, tail;
        while (enif_get_list_cell(env_, arg_types_term, &head, &tail)) {
            // each element in this list should be a 2-tuple
            int arity;
            const ERL_NIF_TERM *array;
            if (enif_is_tuple(env_, head) && enif_get_tuple(env_, head, &arity, &array) && arity == 2) {
                ERL_NIF_TERM arg_value = array[0];
                ERL_NIF_TERM type_info = array[1];

                // for all data types, type_info should be a map
                //   and a `type` key must be set in type_info.
                ERL_NIF_TERM type_term;
                enif_get_map_value(env_, type_info, enif_make_atom(env_, "type"), &type_term);

                std::string arg_type_str;
                if (erlang::nif::get_atom(env_, type_term, arg_type_str)
                    || erlang::nif::get(env_, type_term, arg_type_str)) {
                    // when `type_term` is either a binary (i.e., String.t()) or an atom
                    // for example,
                    //   most basic types:
                    //     {arg_value, %{type: type_term}}
                    //   nd-array types:
                    //     {arg_value, %{type: type_term, size: size_term}}

                    // ignore error as `size` will stay 0
                    // if key `size` is not in the map type_info
                    // (which means this arg_value is not a nd-array)
                    uint64_t size = 0;
                    ERL_NIF_TERM size_term;
                    if (enif_get_map_value(env_, type_info, enif_make_atom(env_, "size"), &size_term)) {
                        erlang::nif::get_uint64(env_, size_term, &size);
                    }

                    // minimum type info is obtained
                    // append it to the array
                    args_with_type.emplace_back(std::make_shared<FFIArgType>(arg_value, type_term, arg_type_str, size, type_info));
                    auto &arg_with_type = args_with_type[args_with_type.size() - 1];

                    // check if we need to pass it by address
                    //   for exmaple,
                    //     {arg_value, %{type: type_term, addr: true}}
                    // by default, the implementation in Otter.Nif will remove the `addr` entry
                    //   if an argument is set to passing by ref or by value.
                    //   therefore, if the `addr` entry appeared in the type_info map
                    //     the value must be `true`.
                    bool pass_by_addr = false;
                    ERL_NIF_TERM addr_term;
                    if (enif_get_map_value(env_, type_info, enif_make_atom(env_, "addr"), &addr_term)) {
                        arg_with_type->pass_by = FFIArgType::ADDR;
                    }

                    // check if we should fetch the new value of the current argument
                    //   for exmaple,
                    //     {arg_value, %{type: type_term, addr: true, out: true}}
                    //     {arg_value, %{type: type_term, ref: true, out: true}}
                    // same as the `addr` above, if `out` was presented in the map
                    // it must be `true`
                    bool is_out = false;
                    ERL_NIF_TERM out_term;
                    if (enif_get_map_value(env_, type_info, enif_make_atom(env_, "out"), &out_term)) {
                        arg_with_type->is_out = true;
                    }

                    // copy ffi_type to arg_with_type.ffi_arg_type
                    arg_with_type->ffi_arg_type = std::make_shared<ffi_type>();
                    if (arg_with_type->type == "c_ptr") {
                        FFICall::copy_ffi_type(arg_with_type->ffi_arg_type, ffi_type_pointer);
                    } else if (arg_with_type->type == "s8") {
                        FFICall::copy_ffi_type(arg_with_type->ffi_arg_type, ffi_type_sint8);
                    } else if (arg_with_type->type == "s16") {
                        FFICall::copy_ffi_type(arg_with_type->ffi_arg_type, ffi_type_sint16);
                    } else if (arg_with_type->type == "s32") {
                        FFICall::copy_ffi_type(arg_with_type->ffi_arg_type, ffi_type_sint32);
                    } else if (arg_with_type->type == "s64") {
                        FFICall::copy_ffi_type(arg_with_type->ffi_arg_type, ffi_type_sint64);
                    } else if (arg_with_type->type == "u8") {
                        FFICall::copy_ffi_type(arg_with_type->ffi_arg_type, ffi_type_uint8);
                    } else if (arg_with_type->type == "u16") {
                        FFICall::copy_ffi_type(arg_with_type->ffi_arg_type, ffi_type_uint16);
                    } else if (arg_with_type->type == "u32") {
                        FFICall::copy_ffi_type(arg_with_type->ffi_arg_type, ffi_type_uint32);
                    } else if (arg_with_type->type == "u64") {
                        FFICall::copy_ffi_type(arg_with_type->ffi_arg_type, ffi_type_uint64);
                    } else if (arg_with_type->type == "f32") {
                        FFICall::copy_ffi_type(arg_with_type->ffi_arg_type, ffi_type_float);
                    } else if (arg_with_type->type == "f64") {
                        FFICall::copy_ffi_type(arg_with_type->ffi_arg_type, ffi_type_double);
                    } else if (arg_with_type->type == "va_args") {
                        arg_with_type->is_va_args = true;
                    }

                    // nd-array
                    if (arg_with_type->size > 0) {
                        // note that nd-array uses a continous (virtual) memory region
                        // therefore, we can pretend it is a struct
                        ffi_type ffi_type_array;
                        // ffi_type_array.size = sizeof(T) * count, where
                        //   sizeof(T): arg_with_type->ffi_arg_type->size
                        //   count: arg_with_type->size
                        ffi_type_array.size = arg_with_type->ffi_arg_type->size * arg_with_type->size;
                        ffi_type_array.alignment = arg_with_type->ffi_arg_type->alignment;
                        ffi_type_array.type = FFI_TYPE_STRUCT;

                        FFICall::copy_ffi_type(arg_with_type->ffi_arg_type, ffi_type_array);
                        arg_with_type->ffi_arg_type->elements = (ffi_type **)&null_ptr_g;
                    }

                    arg_types_term = tail;
                } else if (enif_is_tuple(env_, type_term)) {
                    auto struct_type = create_from_tuple(type_term, error_msg);
                    if (struct_type) {
                        args_with_type.emplace_back(std::make_shared<FFIArgType>(arg_value, type_term, struct_type->struct_id, 0, enif_make_atom(env_, "nil")));
                        struct_wrapper.push_back(struct_type);
                        arg_types_term = tail;
                    } else {
                        error_msg = "cannot parse type";
                        return false;
                    }
                } else {
                    auto struct_type = create_from_tuple(type_info, error_msg);
                    if (struct_type) {
                        args_with_type.emplace_back(std::make_shared<FFIArgType>(arg_value, type_info, struct_type->struct_id, 0, enif_make_atom(env_, "nil")));
                        struct_wrapper.push_back(struct_type);
                        arg_types_term = tail;
                    } else {
                        error_msg = "cannot parse type";
                        return false;
                    }
                }
            } else {
                error_msg = "each element in args_with_type should be a tuple: {arg_value, type_info}";
                return false;
            }
        }

        // verify that we do have `length` new arguments parsed
        if ((args_with_type.size() - prev_size) == length) {
            return true;
        } else {
            error_msg = std::string(__PRETTY_FUNCTION__) + ": expected " + std::to_string(length) + " arguments to be parsed, however, only " + std::to_string(args_with_type.size() - prev_size) + " arguments were successfully parsed";
            return false;
        }
    }

    bool _process_args_with_type(size_t &num_args_processed, size_t arg_offset, std::string &error_msg, bool allow_va_args) {
        // `va_args` can only appear once in the last position
        // note that `va_args` is a pseudo-type in Otter
        //   where `va_list` is the real underlying type defined in stdarg.h
        //   and `va_list` is the one you can pass around in C
        bool ok = true;
        bool last_is_va_args = false;
        size_t arg_failed = 0;

        size_t total_size = args_with_type_.size();
        for (size_t i = arg_offset; i < total_size; ++i) {
            if (last_is_va_args) {
                error_msg = "va_args can only appear at the end of the input argument list";
                ok = false;
                break;
            }

            num_args_processed += 1;
            arg_failed = i;
            auto &p = args_with_type_[i];
            if (p->is_out) {
                out_value_indexes.push_back(i);
            }

            if (p->type == "c_ptr") {
                if (!handle_c_ptr_arg(erlang::nif::get_sint64, p, i)) {
                    ok = false;
                    break;
                }
            } else if (p->type == "s8") {
                if (!handle_arg<int8_t, int>(erlang::nif::get_sint, p, i)) {
                    ok = false;
                    break;
                }
            } else if (p->type == "s16") {
                if (!handle_arg<int16_t, int>(erlang::nif::get_sint, p, i)) {
                    ok = false;
                    break;
                }
            } else if (p->type == "s32") {
                if (!handle_arg<int32_t, int>(erlang::nif::get_sint, p, i)) {
                    ok = false;
                    break;
                }
            } else if (p->type == "s64") {
                if (!handle_arg<int64_t, int64_t>(erlang::nif::get_sint64, p, i)) {
                    ok = false;
                    break;
                }
            } else if (p->type == "u8") {
                if (!handle_arg<uint8_t, unsigned int>(erlang::nif::get_uint, p, i)) {
                    ok = false;
                    break;
                }
            } else if (p->type == "u16") {
                if (!handle_arg<uint16_t, unsigned int>(erlang::nif::get_uint, p, i)) {
                    ok = false;
                    break;
                }
            } else if (p->type == "u32") {
                if (!handle_arg<uint32_t, unsigned int>(erlang::nif::get_uint, p, i)) {
                    ok = false;
                    break;
                }
            } else if (p->type == "u64") {
                if (!handle_arg<uint64_t, uint64_t>(erlang::nif::get_uint64, p, i)) {
                    ok = false;
                    break;
                }
            } else if (p->type == "f32") {
                if (!handle_arg<float, double>(erlang::nif::get_f64, p, i)) {
                    ok = false;
                    break;
                }
            } else if (p->type == "f64") {
                if (!handle_arg<double, double>(erlang::nif::get_f64, p, i)) {
                    ok = false;
                    break;
                }
            } else if (p->type == "va_args") {
                if (allow_va_args) {
                    // notes:
                    //  - `error_msg` will be set in handle_va_args
                    //  - `args` will be realloc'ed in handle_va_args
                    if (!handle_va_args(p, i, error_msg)) {
                        ok = false;
                        break;
                    } else {
                        last_is_va_args = true;
                        num_args_processed -= 1;
                    }
                } else {
                    error_msg = "type :va_args only allow to appear once as the type of the last argument";
                    ok = false;
                    break;
                }
            } else {
                auto wrapper_it = create_from_tuple(p->type_term, error_msg);
                if (wrapper_it != nullptr) {
                    args[i] = &wrapper_it->ffi_struct_type;
                    // https://www.erlang.org/doc/man/erl_nif.html#enif_get_resource
                    // enif_get_resource does not add a reference to the resource object.
                    // However, the pointer received in *objp is guaranteed to be valid
                    // at least as long as the resource handle term is valid.
                    void *resource_obj_ptr = nullptr;
                    if (!enif_get_resource(env_, p->term, wrapper_it->resource_type, &resource_obj_ptr)) {
                        error_msg = "failed to get resource for struct: " + wrapper_it->struct_id;
                        ok = false;
                        break;
                    }
                    p->is_struct_instance = true;
                    p->struct_data = resource_obj_ptr;

                    struct_wrapper.push_back(wrapper_it);
                } else {
                    // todo: other types
                    error_msg = std::string(__PRETTY_FUNCTION__) + ": not implemented for type: " + p->type;
                    ok = false;
                    break;
                }
            }
        }

        if (!ok) {
            if (error_msg.empty()) {
                error_msg = "failed to process argument at index " + std::to_string(arg_failed);
            }
        }
        return ok;
    }

    bool handle_c_ptr_arg(int(*get_nif_term_value)(ErlNifEnv *, ERL_NIF_TERM, int64_t *), std::shared_ptr<FFIArgType> &p, size_t arg_index) {
        auto ffi_arg_res = get_ffi_res<void *>();
        // if enif_inspect_binary succeeded,
        // `binary.data` will live until we return to erlang
        ErlNifBinary binary;
        uint64_t ptr;
        size_t value_slot = 0;
        std::string null_c_ptr;

        // it could be a function pointer
        OtterSymbol * symbol_res = nullptr;
        if (enif_get_resource(env_, p->term, OtterSymbol::type, (void **)&symbol_res) && symbol_res) {
            // do not check if the symbol is a nullptr
            // because it might be intended value for the function to be called
            void * func_ptr = symbol_res->val;
            args[arg_index] = &ffi_type_pointer;
            if (ffi_arg_res == nullptr || !ffi_arg_res->set(func_ptr, value_slot)) {
                return false;
            }
            type_index_resindex[(uint64_t)(uint64_t *)args[arg_index]][arg_index] = value_slot;
        } else if (enif_inspect_binary(env_, p->term, &binary)) {
            args[arg_index] = &ffi_type_pointer;
            if (ffi_arg_res == nullptr || !ffi_arg_res->set(binary.data, value_slot)) {
                return false;
            }
            type_index_resindex[(uint64_t)(uint64_t *)args[arg_index]][arg_index] = value_slot;
        } else if (erlang::nif::get_atom(env_, p->term, null_c_ptr) &&
                   (null_c_ptr == "NULL" || null_c_ptr == "nil")) {
            args[arg_index] = &ffi_type_pointer;
            if (ffi_arg_res == nullptr || !ffi_arg_res->set(nullptr, value_slot)) {
                return false;
            }
            type_index_resindex[(uint64_t)(uint64_t *)args[arg_index]][arg_index] = value_slot;
        } else if (erlang::nif::get_uint64(env_, p->term, &ptr)) {
            args[arg_index] = &ffi_type_pointer;
            if (ffi_arg_res == nullptr || !ffi_arg_res->set((void *)(uint64_t *)(ptr), value_slot)) {
                return false;
            }
            type_index_resindex[(uint64_t)(uint64_t *)args[arg_index]][arg_index] = value_slot;
        } else {
            return false;
        }
        return handle_pass_by_addr<void *>(ffi_arg_res, value_slot, p, arg_index);
    }

    template <
        typename T,
        typename ERL_API_T=T,
        typename get_nif_term_func = int(*)(ErlNifEnv *, ERL_NIF_TERM, ERL_API_T*)>
    bool handle_arg(
        get_nif_term_func get_nif_term_value,
        std::shared_ptr<FFIArgType> &p,
        size_t arg_index,
        T _unused=0) {
        ERL_API_T value;
        if (args != nullptr && get_nif_term_value(env_, p->term, &value)) {
            args[arg_index] = get_default_ffi_type<T>();
            auto ffi_arg_res = get_ffi_res<T>();
            size_t value_slot = 0;
            if (ffi_arg_res && ffi_arg_res->set((T)value, value_slot)) {
                type_index_resindex[(uint64_t)(uint64_t *)args[arg_index]][arg_index] = value_slot;
                return handle_pass_by_addr<T>(ffi_arg_res, value_slot, p, arg_index);
            } else {
                printf("[debug] cannot save value for %s to ffi_arg_res\r\n", p->type.c_str());
                return false;
            }
        } else {
            printf("[debug] cannot get value for %s\r\n", p->type.c_str());
            return false;
        }
    }

    bool handle_va_args(std::shared_ptr<FFIArgType> &va_args, size_t va_arg_index, std::string &error_msg) {
        // va_args.term should be a list of 2-tuples like
        //   {value, %{type: TYPE, extra: EXTRA}}

        // call pop_back because the last one is :va_args
        std::shared_ptr<FFIArgType> this_va_args = va_args;
        args_with_type_.pop_back();
        if (_get_args_with_type(va_args->term, va_arg_index, args_with_type_, error_msg)) {
            size_t total_args = args_with_type_.size();
            void * new_args = realloc((void *)args, sizeof(ffi_type *) * total_args);
            if (new_args == nullptr) {
                error_msg = "cannot allocate enough memory for function arguments";
                return false;
            }
            args = (ffi_type **)new_args;

            size_t num_var_args = 0;

            return _process_args_with_type(num_var_args, va_arg_index, error_msg, false);
        } else {
            error_msg = "failed to parse argument types in va_args";
            return false;
        }
    }

    template <typename T>
    bool handle_pass_by_addr(
        FFIResource<T> * ffi_arg_res,
        size_t value_slot,
        std::shared_ptr<FFIArgType> &p,
        size_t arg_index)
    {
        p->ffi_arg_type_original = p->ffi_arg_type;
        p->value_slot_original = value_slot;

        if (p->pass_by == FFIArgType::ADDR) {
            args[arg_index] = get_default_ffi_type<void *>();
            auto ffi_arg_by_addr_res = get_ffi_res<void *>();
            void * res_address = nullptr;
            if (ffi_arg_res && ffi_arg_by_addr_res &&
                ffi_arg_res->get(value_slot, res_address) &&
                ffi_arg_by_addr_res->set(res_address, value_slot)) {

                p->ffi_arg_type = std::make_shared<ffi_type>();
                copy_ffi_type(p->ffi_arg_type, ffi_type_pointer);
                p->value_slot = value_slot;

                type_index_resindex[(uint64_t)(uint64_t *)args[arg_index]][arg_index] = value_slot;
                return true;
            } else {
                return false;
            }
        } else {
            p->value_slot = value_slot;
        }
        return true;
    }

    bool handle_out_values(std::shared_ptr<FFIArgType> &p, ERL_NIF_TERM &out_term, std::string &out_error) {
        bool ok = false;
        out_error = "not implemented for type " + p->type;
        if (p->type == "c_ptr") {
            // need to handle pointers with care
            // copy data, but how many bytes should we copy?
        } else if (p->type == "s8") {
            ok = handle_out_values<int8_t, int32_t>(enif_make_int, p, out_term, out_error);
        } else if (p->type == "s16") {
            ok = handle_out_values<int16_t, int32_t>(enif_make_int, p, out_term, out_error);
        } else if (p->type == "s32") {
            ok = handle_out_values<int32_t, int32_t>(enif_make_int, p, out_term, out_error);
        } else if (p->type == "s64") {
            ok = handle_out_values<int64_t, int64_t>(enif_make_long, p, out_term, out_error);
        } else if (p->type == "u8") {
            ok = handle_out_values<uint8_t, uint32_t>(enif_make_uint, p, out_term, out_error);
        } else if (p->type == "u16") {
            ok = handle_out_values<uint16_t, uint32_t>(enif_make_uint, p, out_term, out_error);
        } else if (p->type == "u32") {
            ok = handle_out_values<uint32_t, uint32_t>(enif_make_uint, p, out_term, out_error);
        } else if (p->type == "u64") {
            ok = handle_out_values<uint64_t, uint64_t>(enif_make_ulong, p, out_term, out_error);
        } else if (p->type == "f32") {
            ok = handle_out_values<float, double>(enif_make_double, p, out_term, out_error);
        } else if (p->type == "f64") {
            ok = handle_out_values<double, double>(enif_make_double, p, out_term, out_error);
        } else {
            bool is_struct = false;
            if (is_struct) {
                // todo: struct
            } else {
                // todo: other types
                printf("[debug] type: %s\r\n", p->type.c_str());
            }
            ok = false;
        }
        return ok;
    }

    template <typename T, typename ERL_API_T=T, typename make_nif_term_func = ERL_NIF_TERM(*)(ErlNifEnv *, ERL_API_T)>
    bool handle_out_values(
        make_nif_term_func make_term,
        std::shared_ptr<FFIArgType> &p,
        ERL_NIF_TERM &out_term,
        std::string &out_error)
    {
        auto ffi_arg_res = get_ffi_res<T>();
        T value;
        if (ffi_arg_res && ffi_arg_res->get_value(p->value_slot_original, value)) {
            out_term = make_term(env_, value);
            return true;
        } else {
            out_error = "cannot get original argument storage";
            return false;
        }
    }

    std::shared_ptr<FFIStructTypeWrapper> create_from_tuple(
        ERL_NIF_TERM struct_return_type_term,
        std::string &error_msg)
    {
        int arity = -1;
        const ERL_NIF_TERM *array;
        std::vector<std::shared_ptr<FFIArgType>> args_with_type;
        bool is_size_correct_tuple =
                enif_get_tuple(env_, struct_return_type_term, &arity, &array) &&
                arity == 3;
        std::string struct_atom;
        std::string struct_id;
        erlang::nif::get_atom(env_, array[0], struct_atom);
        erlang::nif::get_atom(env_, array[1], struct_id);
        if (!is_size_correct_tuple) {
            return nullptr;
        }

        if (struct_atom != "struct") {
            return nullptr;
        }

        if (_get_args_with_type(array[2], 0, args_with_type, error_msg)) {
            auto wrapper = std::make_shared<FFIStructTypeWrapper>(args_with_type.size() + 1);

            wrapper->struct_id = struct_id;
            wrapper->resource_type = FFIStructTypeWrapper::get_ffi_struct_resource_type(env_, struct_id);

            // note: wrapper will be added to `wrappers` after it is returned from this function
            if (wrapper->resource_type) {
                for (size_t i = 0; i < args_with_type.size(); ++i) {
                    auto &p = args_with_type[i];
                    wrapper->field_types.push_back(p->ffi_arg_type);
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

    template <typename T>
    auto get_ffi_res() -> FFIResource<T> * {
        auto u64_type = (uint64_t)(uint64_t *)get_default_ffi_type<T>();
        if (ffi_res.find(u64_type) != ffi_res.end()) {
            return (FFIResource<T> *)ffi_res[u64_type];
        } else {
            ffi_res[u64_type] = new FFIResource<T>();
            return (FFIResource<T> *)ffi_res[u64_type];
        }
    }

    ErlNifEnv * env_;
    ERL_NIF_TERM symbol_;
    ERL_NIF_TERM return_type_;
    ERL_NIF_TERM arg_types_term_;

    std::vector<std::shared_ptr<FFIArgType>> args_with_type_;
    std::vector<std::shared_ptr<FFIStructTypeWrapper>> struct_wrapper;

    std::string return_type;
    std::shared_ptr<FFIStructTypeWrapper> struct_return_type = nullptr;
    size_t num_fixed_args = 0;
    size_t num_variadic_args = 0;

    // type_index_resindex (resource pool)
    //   key: type
    //   value:
    //     key: index in the above variable values
    //     value: index in ffi_resources
    std::map<uint64_t, std::map<size_t, size_t>> type_index_resindex;
    std::map<uint64_t, void *> ffi_res;
    std::vector<size_t> out_value_indexes;

    ffi_cif cif;
    ffi_type ** args = nullptr;
    void ** values = nullptr;
    ffi_type * ffi_return_type = nullptr;
    void * rc = nullptr;
};

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
        return erlang::nif::error(env, "cannot get dlopen mode");
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

static ERL_NIF_TERM otter_dlsym(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
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
    return erlang::nif::ok(env, enif_make_uint64(env, (uint64_t)((uint64_t *)env)));
}

static ERL_NIF_TERM otter_stdin(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    return erlang::nif::ok(env, enif_make_uint64(env, (uint64_t)((uint64_t *)stdin)));
}

static ERL_NIF_TERM otter_stdout(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    return erlang::nif::ok(env, enif_make_uint64(env, (uint64_t)((uint64_t *)stdout)));
}

static ERL_NIF_TERM otter_stderr(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    return erlang::nif::ok(env, enif_make_uint64(env, (uint64_t)((uint64_t *)stderr)));
}

static void otter_segfault_catcher(int sig) {
    switch(sig) {
        case SIGSEGV:
            longjmp(jmp_buf_g, 1);
            break;
    }
}

static ERL_NIF_TERM otter_invoke(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
    if (argc != 3) {
        return enif_make_badarg(env);
    }
    ERL_NIF_TERM symbol_term = argv[0];
    ERL_NIF_TERM return_type_term = argv[1];
    ERL_NIF_TERM args_with_type_term = argv[2];

    std::string error_msg;
    ERL_NIF_TERM return_value, out_values;
    ERL_NIF_TERM ret;

    struct sigaction oldact;
    sigaction(SIGSEGV, NULL, &oldact);

    signal(SIGSEGV, otter_segfault_catcher);
    if (!setjmp(jmp_buf_g)) {
        auto ffi_call_wrapper = std::make_shared<FFICall>(env, symbol_term, return_type_term, args_with_type_term);
        if (ffi_call_wrapper->call(return_value, out_values, error_msg)) {
            if (ffi_call_wrapper->out_value_indexes.size() > 0) {
                ret = erlang::nif::ok(env, enif_make_tuple2(env, return_value, out_values));
            } else {
                ret =  erlang::nif::ok(env, return_value);
            }
        } else {
            ret =  erlang::nif::error(env, error_msg.c_str());
        }
    } else {
        ret =  erlang::nif::error(env, "segmentation fault");
    }

    signal(SIGSEGV, oldact.sa_handler);
    return ret;
}

static int on_load(ErlNifEnv *env, void **, ERL_NIF_TERM) {
    ErlNifResourceType *rt;
    rt = enif_open_resource_type(env, "Elixir.Otter.Nif", "OtterHandle", resource_dtor, ERL_NIF_RT_CREATE, nullptr);
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
    {"stdin", 0, otter_stdin, 0},
    {"stdout", 0, otter_stdout, 0},
    {"stderr", 0, otter_stderr, 0},
    {"invoke", 3, otter_invoke, 0},
};

ERL_NIF_INIT(Elixir.Otter.Nif, nif_functions, on_load, on_reload, on_upgrade, NULL)

#if defined(__GNUC__)
#pragma GCC visibility push(default)
#endif
