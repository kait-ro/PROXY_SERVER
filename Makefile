proxy: main.cpp proxyServer.cpp Authenticator.cpp WebsiteFilter.cpp Logger.cpp
	g++ main.cpp ProxyServer.cpp Authenticator.cpp WebsiteFilter.cpp Logger.cpp -o proxy
run: proxy
	./proxy

clean:
	rm -f proxy
kill-port:
	@echo "Killing process on port $(PORT)..."
	@lsof -ti :$(PORT) | xargs kill -9 2>/dev/null || true