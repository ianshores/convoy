CXX ?= c++
CC ?= cc
PYTHON ?= python3
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra
CFLAGS ?= -O3 -std=c99 -Wall -Wextra
PFFFT_DIR = third_party/pffft
PFFFT_OBJS = build/pffft.o build/pffft_common.o
PFFFT_HEADERS = $(wildcard $(PFFFT_DIR)/*.h $(PFFFT_DIR)/simd/*.h)
HEADERS = $(wildcard include/*.hpp)
INCLUDES = -Iinclude -I$(PFFFT_DIR) -DPFFFT_STATIC_DEFINE
.PHONY: test benchmark timing pd check-export check-pd
build:
	mkdir -p build
build/pffft.o: $(PFFFT_DIR)/pffft.c $(PFFFT_HEADERS) | build
	$(CC) $(CFLAGS) -fPIC -DPFFFT_STATIC_DEFINE -I$(PFFFT_DIR) -c $< -o $@
build/pffft_common.o: $(PFFFT_DIR)/pffft_common.c $(PFFFT_HEADERS) | build
	$(CC) $(CFLAGS) -fPIC -DPFFFT_STATIC_DEFINE -I$(PFFFT_DIR) -c $< -o $@
build/heavyverb_test: tests/core.cpp $(HEADERS) $(PFFFT_OBJS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(PFFFT_OBJS) -o $@
build/heavyverb_test_portable: tests/core.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DHEAVYVERB_PORTABLE_FFT -Iinclude $< -o $@
ifeq ($(shell uname -s),Linux)
build/heavyverb_realtime: tests/realtime.cpp $(HEADERS) $(PFFFT_OBJS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(PFFFT_OBJS) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc -o $@
build/heavyverb_timing: tests/timing.cpp $(HEADERS) $(PFFFT_OBJS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(PFFFT_OBJS) -o $@
timing: build/heavyverb_timing
	./build/heavyverb_timing
REALTIME_TEST = build/heavyverb_realtime
endif
test: build/heavyverb_test build/heavyverb_test_portable $(REALTIME_TEST)
	./build/heavyverb_test
	./build/heavyverb_test_portable
	$(if $(REALTIME_TEST),./$(REALTIME_TEST),@true)
build/heavyverb_benchmark: tests/benchmark.cpp $(HEADERS) $(PFFFT_OBJS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(PFFFT_OBJS) -o $@
build/heavyverb_benchmark_uniform: tests/benchmark.cpp $(HEADERS) $(PFFFT_OBJS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DHEAVYVERB_UNIFORM_PARTITIONS $< $(PFFFT_OBJS) -o $@
build/heavyverb_benchmark_portable_uniform: tests/benchmark.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DHEAVYVERB_PORTABLE_FFT -DHEAVYVERB_UNIFORM_PARTITIONS -Iinclude $< -o $@
build/heavyverb_benchmark_portable: tests/benchmark.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DHEAVYVERB_PORTABLE_FFT -Iinclude $< -o $@
benchmark: build/heavyverb_benchmark
	./build/heavyverb_benchmark
pd: $(PFFFT_OBJS) | build
	@test -f "$(PD_INCLUDE_DIR)/m_pd.h" || (echo 'Set PD_INCLUDE_DIR to the directory containing m_pd.h'; exit 1)
	$(CXX) $(CXXFLAGS) -shared -fPIC $(INCLUDES) -I"$(PD_INCLUDE_DIR)" pd/heavyverb_tilde.cpp $(PFFFT_OBJS) -o 'build/heavyverb~.pd_linux'
check-export:
	$(PYTHON) tests/export.py
check-pd:
	$(PYTHON) tests/pd.py "$(PD)"
