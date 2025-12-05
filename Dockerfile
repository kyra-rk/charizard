# Build stage
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /build

# Copy source code
COPY . .

# Build the application (without MongoDB for simplicity)
RUN mkdir -p build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCHARIZARD_WITH_MONGO=OFF && \
    cmake --build . --target charizard_api

# Runtime stage
FROM ubuntu:22.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    ca-certificates \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# Copy the binary from builder
COPY --from=builder /build/build/charizard_api /usr/local/bin/charizard_api

# Expose port (Cloud Run will set PORT env var, which your code already reads)
EXPOSE 8080

# Run the application
# The PORT environment variable is already handled in main.cpp
CMD ["/usr/local/bin/charizard_api"]

