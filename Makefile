CXX ?= c++
CC ?= cc
PYTHON ?= python3
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra
CFLAGS ?= -O3 -std=c99 -Wall -Wextra
HEADERS = $(wildcard include/*.hpp)
INCLUDES = -Iinclude
.PHONY: test benchmark timing pd check-export check-pd
build:
	mkdir -p build
build/heavyverb_test: tests/core.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@
build/heavyverb_test_portable: tests/core.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DHEAVYVERB_PORTABLE_FFT -Iinclude $< -o $@
ifeq ($(shell uname -s),Linux)
build/heavyverb_realtime: tests/realtime.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc -o $@
build/heavyverb_timing: tests/timing.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@
timing: build/heavyverb_timing
	./build/heavyverb_timing
REALTIME_TEST = build/heavyverb_realtime
endif
build/heavyverb_fft_test: tests/fft.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -Iinclude $< -o $@
build/heavyverb_fft_test_scalar: tests/fft.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DHEAVYVERB_FFT_SCALAR -Iinclude $< -o $@
ifeq ($(shell uname -s),Linux)
build/heavyverb_realtime_portable: tests/realtime.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DHEAVYVERB_PORTABLE_FFT -Iinclude $< -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc -o $@
REALTIME_PORTABLE_TEST = build/heavyverb_realtime_portable
endif
# Exercise every public transform path with runtime AVX, forced SSE, and scalar code.
EXTRA_FFT_TESTS = build/heavyverb_fft_half build/heavyverb_fft_packed \
 build/heavyverb_fft_sse build/heavyverb_fft_half_sse build/heavyverb_fft_packed_sse \
 build/heavyverb_fft_half_scalar build/heavyverb_fft_packed_scalar
build/heavyverb_fft_half: EXTRA_FFT_DEFINES = -DHEAVYVERB_TEST_HALF_FORWARD
build/heavyverb_fft_packed: EXTRA_FFT_DEFINES = -DHEAVYVERB_TEST_PACKED -DHEAVYVERB_TEST_PACKED8
build/heavyverb_fft_sse: EXTRA_FFT_DEFINES = -DHEAVYVERB_DISABLE_AVX
build/heavyverb_fft_half_sse: EXTRA_FFT_DEFINES = -DHEAVYVERB_DISABLE_AVX -DHEAVYVERB_TEST_HALF_FORWARD
build/heavyverb_fft_packed_sse: EXTRA_FFT_DEFINES = -DHEAVYVERB_DISABLE_AVX -DHEAVYVERB_TEST_PACKED -DHEAVYVERB_TEST_PACKED8
build/heavyverb_fft_half_scalar: EXTRA_FFT_DEFINES = -DHEAVYVERB_FFT_SCALAR -DHEAVYVERB_TEST_HALF_FORWARD
build/heavyverb_fft_packed_scalar: EXTRA_FFT_DEFINES = -DHEAVYVERB_FFT_SCALAR -DHEAVYVERB_TEST_PACKED -DHEAVYVERB_TEST_PACKED8
$(EXTRA_FFT_TESTS): tests/fft.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(EXTRA_FFT_DEFINES) -Iinclude $< -o $@
build/heavyverb_test_portable_sse: tests/core.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DHEAVYVERB_PORTABLE_FFT -DHEAVYVERB_DISABLE_AVX -Iinclude $< -o $@
build/heavyverb_test_portable_scalar: tests/core.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DHEAVYVERB_PORTABLE_FFT -DHEAVYVERB_FFT_SCALAR -Iinclude $< -o $@
test: build/heavyverb_cutoff $(EXTRA_FFT_TESTS) build/heavyverb_test_portable_sse build/heavyverb_test_portable_scalar build/heavyverb_test build/heavyverb_test_portable build/heavyverb_fft_test build/heavyverb_fft_test_scalar $(REALTIME_TEST) $(REALTIME_PORTABLE_TEST)
	./build/heavyverb_cutoff
	./build/heavyverb_test
	./build/heavyverb_test_portable
	./build/heavyverb_fft_test
	./build/heavyverb_fft_test_scalar
	for check in $(EXTRA_FFT_TESTS) build/heavyverb_test_portable_sse build/heavyverb_test_portable_scalar; do ./$$check || exit $$?; done
	$(if $(REALTIME_TEST),./$(REALTIME_TEST),@true)
	$(if $(REALTIME_PORTABLE_TEST),./$(REALTIME_PORTABLE_TEST),@true)
build/heavyverb_benchmark: tests/benchmark.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@
build/heavyverb_benchmark_uniform: tests/benchmark.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DHEAVYVERB_UNIFORM_PARTITIONS $< -o $@
build/heavyverb_benchmark_portable_uniform: tests/benchmark.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DHEAVYVERB_PORTABLE_FFT -DHEAVYVERB_UNIFORM_PARTITIONS -Iinclude $< -o $@
build/heavyverb_benchmark_portable: tests/benchmark.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DHEAVYVERB_PORTABLE_FFT -Iinclude $< -o $@
benchmark: build/heavyverb_benchmark
	./build/heavyverb_benchmark
pd: | build
	@test -f "$(PD_INCLUDE_DIR)/m_pd.h" || (echo 'Set PD_INCLUDE_DIR to the directory containing m_pd.h'; exit 1)
	$(CXX) $(CXXFLAGS) -shared -fPIC $(INCLUDES) -I"$(PD_INCLUDE_DIR)" pd/heavyverb_tilde.cpp -o 'build/heavyverb~.pd_linux'
check-export:
	$(PYTHON) tests/export.py
check-pd:
	$(PYTHON) tests/pd.py "$(PD)"

build/heavyverb_quality: tests/quality.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@
build/heavyverb_quality_portable: tests/quality.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DHEAVYVERB_PORTABLE_FFT -Iinclude $< -o $@
.PHONY: quality
quality: build/heavyverb_quality build/heavyverb_quality_portable
	./build/heavyverb_quality
	./build/heavyverb_quality_portable

build/heavyverb_cutoff: tests/cutoff.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -Iinclude $< -o $@
