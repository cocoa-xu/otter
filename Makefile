PRIV_DIR = $(MIX_APP_PATH)/priv
NIF_SO = $(PRIV_DIR)/otter_nif.so

C_SRC = $(shell pwd)/c_src
LIB_SRC = $(shell pwd)/lib
LIBFFI_CFLAGS = "$(shell pkg-config --cflags libffi)"
LIBFFI_LIBS = "$(shell pkg-config --libs libffi)"
CPPFLAGS += -shared -std=c++14 -O3 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -fPIC
CPPFLAGS += -I$(ERTS_INCLUDE_DIR) $(LIBFFI_CFLAGS)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	CPPFLAGS += -undefined dynamic_lookup -flat_namespace -undefined suppress
endif

.DEFAULT_GLOBAL := build

build: $(NIF_SO)

$(NIF_SO):
	@mkdir -p $(PRIV_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(C_SRC)/otter_nif.cpp $(LIBFFI_LIBS) -o $(NIF_SO)
