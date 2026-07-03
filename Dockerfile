FROM gcc:13
WORKDIR /app
RUN apt-get update && apt-get install -y libssl-dev pkg-config && rm -rf /var/lib/apt/lists/*
COPY . .
RUN make proxy
# Interactive CLI proxy; run with:  docker run -it <image>
CMD ["./proxy"]
