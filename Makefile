CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra $(shell pkg-config --cflags openssl 2>/dev/null)
LDLIBS   ?= $(shell pkg-config --libs openssl 2>/dev/null || echo -lssl -lcrypto)

SRCS = main.cpp proxyserver.cpp Authenticator.cpp WebsiteFilter.cpp Logger.cpp LRUCache.cpp

proxy: $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) $(LDLIBS) -o proxy

test: tests/test_lru.cpp LRUCache.cpp
	$(CXX) $(CXXFLAGS) tests/test_lru.cpp LRUCache.cpp -o test_lru
	./test_lru

run: proxy
	./proxy

clean:
	rm -f proxy test_lru
