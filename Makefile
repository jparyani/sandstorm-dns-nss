# Sandstorm - Personal Cloud Sandbox
# Copyright (c) 2014 Sandstorm Development Group, Inc. and contributors
# All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# You may override the following vars on the command line to suit
# your config.
CXX=clang++
CXXFLAGS=-O2 -Wall

# You generally should not modify these.
CXXFLAGS2=-std=c++1y -Isrc -Itmp $(CXXFLAGS)

SANDSTORM_DIR=/home/jason/workspace/sandstorm/src
# SANDSTORM_DIR=/opt/sandstorm/latest/usr/include

.PHONY: all clean

MODULE = libnss_sandstormdns.so.2

all: lib/$(MODULE)

clean:
	rm -rf bin tmp

lib/$(MODULE): tmp/genfiles src/sandstorm/nss-sandstormdns.c++
# lib/$(MODULE): src/sandstorm/nss-sandstormdns.c++
	@echo "building lib/$(MODULE)..."
	@mkdir -p lib
	@$(CXX)  -Wl,-h,$(MODULE) src/sandstorm/nss-sandstormdns.c++ tmp/sandstorm/*.capnp.c++ -o $@ -shared -fPIC $(CXXFLAGS2) -pthread -lpthread -I/usr/local/include -L/usr/local/lib -lcapnp-rpc -lkj-async -lcapnp -lkj

tmp/genfiles: $(SANDSTORM_DIR)/sandstorm/*.capnp
	@echo "generating capnp files..." $(SANDSTORM_DIR)
	@mkdir -p tmp
	@capnp compile --src-prefix=$(SANDSTORM_DIR) -oc++:tmp  $(SANDSTORM_DIR)/sandstorm/*.capnp
	@touch tmp/genfiles

install: lib/$(MODULE)
	mkdir -p /usr/lib
	install -m 0644 lib/$(MODULE) /lib/x86_64-linux-gnu/$(MODULE)
	ln -s /lib/x86_64-linux-gnu/$(MODULE) /usr/lib/x86_64-linux-gnu/libnss_sandstormdns.so
