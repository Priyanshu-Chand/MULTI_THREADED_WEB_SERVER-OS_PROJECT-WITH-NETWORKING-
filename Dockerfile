# --- Stage 1: The "Builder" ---
# We use a base image that has the g++ compiler
FROM gcc:12 AS builder

# Set the working directory inside the container
WORKDIR /app

# Copy all your project files (code, www folder, etc.) into the container
COPY . .

# Run the LINUX compile command
# This creates the executable file named 'server'
RUN g++ WindowServer.cpp -o server -pthread -std=c++17 -static-libstdc++


# --- Stage 2: The "Final" Image ---
# We use a minimal, secure image for the final product
FROM debian:bookworm-slim

# Set the working directory
WORKDIR /app

# Copy *only* the compiled 'server' from the 'builder' stage
COPY --from=builder /app/server .

# Copy *only* the 'www' folder from the 'builder' stage
# This is VITAL because your code looks for "www/index.html", etc.
COPY --from=builder /app/www ./www

# Tell Render that your application listens on port 8080
EXPOSE 8080

# The final command to run when the container starts
CMD ["./server"]