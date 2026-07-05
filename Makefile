CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pthread $(shell pkg-config --cflags openssl 2>/dev/null)
LDLIBS   ?= $(shell pkg-config --libs openssl 2>/dev/null || echo -lssl -lcrypto)

SRCS = main.cpp proxyserver.cpp Authenticator.cpp WebsiteFilter.cpp Logger.cpp LRUCache.cpp

proxy: $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) $(LDLIBS) -o proxy

test: test_lru test_auth test_filter test_dns test_http test_connect test_ddos
	./test_lru
	./test_auth
	./test_filter
	./test_dns
	./test_http
	./test_connect
	./test_ddos

test_lru: tests/test_lru.cpp LRUCache.cpp
	$(CXX) $(CXXFLAGS) tests/test_lru.cpp LRUCache.cpp -o test_lru

test_auth: tests/test_authenticator.cpp Authenticator.cpp
	$(CXX) $(CXXFLAGS) tests/test_authenticator.cpp Authenticator.cpp $(LDLIBS) -o test_auth

test_filter: tests/test_websitefilter.cpp WebsiteFilter.cpp
	$(CXX) $(CXXFLAGS) tests/test_websitefilter.cpp WebsiteFilter.cpp -o test_filter

test_dns: tests/test_dns_cache.cpp
	$(CXX) $(CXXFLAGS) tests/test_dns_cache.cpp -o test_dns

test_http: tests/test_http_proxy.cpp
	$(CXX) $(CXXFLAGS) tests/test_http_proxy.cpp -o test_http

test_connect: tests/test_connect_tunnel.cpp
	$(CXX) $(CXXFLAGS) tests/test_connect_tunnel.cpp -o test_connect

test_ddos: tests/test_ddos_protection.cpp
	$(CXX) $(CXXFLAGS) tests/test_ddos_protection.cpp -o test_ddos

run: proxy
	./proxy

clean:
	rm -f proxy test_lru test_auth test_filter test_dns test_http test_connect test_ddos
