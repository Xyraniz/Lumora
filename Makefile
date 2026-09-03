.PHONY: all build test clean

all: build

build:
	cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
	cmake --build build --parallel

test: build
	ctest --test-dir build --output-on-failure

clean:
	rm -rf build bin/luau-vm
