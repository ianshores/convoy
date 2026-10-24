CXX ?= c++
CC ?= cc
PYTHON ?= python3
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra
CFLAGS ?= -O3 -std=c99 -Wall -Wextra
HEADERS = $(wildcard include/*.hpp) vendor/dsp/include/dsp/fft.hpp
INCLUDES = -Iinclude -Ivendor/dsp/include
.PHONY: test benchmark timing
build:
	mkdir -p build
build/convoy_test: tests/core.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@
build/convoy_test_portable: tests/core.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DCONVOY_PORTABLE_FFT $(INCLUDES) $< -o $@
ifeq ($(shell uname -s),Linux)
build/convoy_realtime: tests/realtime.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc -o $@
build/convoy_timing: tests/timing.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@
timing: build/convoy_timing
	./build/convoy_timing
REALTIME_TEST = build/convoy_realtime
endif
build/convoy_fft_test: tests/fft.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@
build/convoy_fft_test_scalar: tests/fft.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DCONVOY_FFT_SCALAR $(INCLUDES) $< -o $@
ifeq ($(shell uname -s),Linux)
build/convoy_realtime_portable: tests/realtime.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DCONVOY_PORTABLE_FFT $(INCLUDES) $< -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc -o $@
REALTIME_PORTABLE_TEST = build/convoy_realtime_portable
endif
# Exercise every public transform path with runtime AVX, forced SSE, and scalar code.
EXTRA_FFT_TESTS = build/convoy_fft_half build/convoy_fft_packed \
 build/convoy_fft_sse build/convoy_fft_half_sse build/convoy_fft_packed_sse \
 build/convoy_fft_half_scalar build/convoy_fft_packed_scalar
build/convoy_fft_half: EXTRA_FFT_DEFINES = -DCONVOY_TEST_HALF_FORWARD
build/convoy_fft_packed: EXTRA_FFT_DEFINES = -DCONVOY_TEST_PACKED -DCONVOY_TEST_PACKED8
build/convoy_fft_sse: EXTRA_FFT_DEFINES = -DCONVOY_DISABLE_AVX
build/convoy_fft_half_sse: EXTRA_FFT_DEFINES = -DCONVOY_DISABLE_AVX -DCONVOY_TEST_HALF_FORWARD
build/convoy_fft_packed_sse: EXTRA_FFT_DEFINES = -DCONVOY_DISABLE_AVX -DCONVOY_TEST_PACKED -DCONVOY_TEST_PACKED8
build/convoy_fft_half_scalar: EXTRA_FFT_DEFINES = -DCONVOY_FFT_SCALAR -DCONVOY_TEST_HALF_FORWARD
build/convoy_fft_packed_scalar: EXTRA_FFT_DEFINES = -DCONVOY_FFT_SCALAR -DCONVOY_TEST_PACKED -DCONVOY_TEST_PACKED8
$(EXTRA_FFT_TESTS): tests/fft.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(EXTRA_FFT_DEFINES) $(INCLUDES) $< -o $@
build/convoy_test_portable_sse: tests/core.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DCONVOY_PORTABLE_FFT -DCONVOY_DISABLE_AVX $(INCLUDES) $< -o $@
build/convoy_test_portable_scalar: tests/core.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DCONVOY_PORTABLE_FFT -DCONVOY_FFT_SCALAR $(INCLUDES) $< -o $@
test: build/convoy_cutoff $(EXTRA_FFT_TESTS) build/convoy_test_portable_sse build/convoy_test_portable_scalar build/convoy_test build/convoy_test_portable build/convoy_fft_test build/convoy_fft_test_scalar $(REALTIME_TEST) $(REALTIME_PORTABLE_TEST)
	./build/convoy_cutoff
	./build/convoy_test
	./build/convoy_test_portable
	./build/convoy_fft_test
	./build/convoy_fft_test_scalar
	for check in $(EXTRA_FFT_TESTS) build/convoy_test_portable_sse build/convoy_test_portable_scalar; do ./$$check || exit $$?; done
	$(if $(REALTIME_TEST),./$(REALTIME_TEST),@true)
	$(if $(REALTIME_PORTABLE_TEST),./$(REALTIME_PORTABLE_TEST),@true)
build/convoy_benchmark: tests/benchmark.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@
build/convoy_benchmark_uniform: tests/benchmark.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DCONVOY_UNIFORM_PARTITIONS $< -o $@
build/convoy_benchmark_portable_uniform: tests/benchmark.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DCONVOY_PORTABLE_FFT -DCONVOY_UNIFORM_PARTITIONS $(INCLUDES) $< -o $@
build/convoy_benchmark_portable: tests/benchmark.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DCONVOY_PORTABLE_FFT $(INCLUDES) $< -o $@
benchmark: build/convoy_benchmark
	./build/convoy_benchmark
build/convoy_quality: tests/quality.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@
build/convoy_quality_portable: tests/quality.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DCONVOY_PORTABLE_FFT $(INCLUDES) $< -o $@
.PHONY: quality
quality: build/convoy_quality build/convoy_quality_portable
	./build/convoy_quality
	./build/convoy_quality_portable

build/convoy_cutoff: tests/cutoff.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@
