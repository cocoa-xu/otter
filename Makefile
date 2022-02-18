PRIV_DIR = $(MIX_APP_PATH)/priv
NIF_SO = $(PRIV_DIR)/otter_nif.so

C_SRC = $(shell pwd)/c_src
LIB_SRC = $(shell pwd)/lib
TEST_SRC = $(shell pwd)/test
TEST_SO = $(TEST_SRC)/test.so

LIBFFI_INCLUDE_DIR = "$(shell pkg-config --variable=includedir libffi)"
ifneq ("$(wildcard $(LIBFFI_INCLUDE_DIR)/ffi.h)","")
    # do nothing
else
ifneq ("$(wildcard $(LIBFFI_INCLUDE_DIR)/ffi/ffi.h)","")
    LIBFFI_INCLUDE_DIR = $(LIBFFI_INCLUDE_DIR)/ffi
else
    # on some linux pkg-config --variable=includedir libffi
    # returns /usr/include
    # while the ffi.h is actually in /usr/include/x86_64-linux-gnu
    # but we can compile the NIF library using #include <ffi.h>
    # so let's hope it works
endif
endif
LIBFFI_LIBS = $(shell pkg-config --libs libffi)
CPPFLAGS += $(CFLAGS) -std=c++17 -Wall -Wextra -pedantic -fPIC
LDFLAGS += -shared

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LDFLAGS += -undefined dynamic_lookup -flat_namespace -undefined suppress
endif

.DEFAULT_GLOBAL := build

build: clean $(NIF_SO) $(TEST_SO)

clean:
	@ if [ -z "${SKIP_COMPILE}" ]; then \
  		rm -f $(NIF_SO) $(TEST_SO) ; \
	fi

$(TEST_SO):
	@ if [ "${MIX_ENV}" = "test" ] && [ -z "${SKIP_COMPILE}" ]; then \
		$(CC) $(CPPFLAGS) $(LDFLAGS) $(TEST_SRC)/test.cpp -o $(TEST_SO) ; \
	fi


$(NIF_SO):
	@ mkdir -p $(PRIV_DIR)
	@ if [ -z "${SKIP_COMPILE}" ]; then \
		$(CC) $(CPPFLAGS) -I$(ERTS_INCLUDE_DIR) -I$(LIBFFI_INCLUDE_DIR) $(LDFLAGS) $(C_SRC)/otter_nif.cpp $(LIBFFI_LIBS) -o $(NIF_SO) ; \
	fi
