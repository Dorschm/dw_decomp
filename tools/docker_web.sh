#!/bin/sh

docker run --rm -v "$PWD":/src -w /src emscripten/emsdk:3.1.74 "$@"
