#!/bin/bash

# Function to display help
show_help() {
    echo "Usage: $0 [OPTIONS]
    
Options:
  -h, --help           Show this help message and exit
  -v, --version        Show the version and exit
  -d, --device DEVICE  Specify the framebuffer device (default: /dev/fb0)
  -o, --output DIR     Specify the output directory for screenshots (default: screenshots)
  -g, --gray           Capture screenshots in grayscale
  -c, --colored        Capture screenshots in color (default)
"
}

# Default values
DIR="screenshots"
DEVICE="/dev/fb0"
COLOR_MODE="--colored"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -v|--version)
            echo "fbo version: 1.0.0"  # Replace with actual version
            exit 0
            ;;
        -d|--device)
            DEVICE="$2"
            shift 2
            ;;
        -o|--output)
            DIR="$2"
            shift 2
            ;;
        -g|--gray)
            COLOR_MODE="--gray"
            shift
            ;;
        -c|--colored)
            COLOR_MODE="--colored"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Ensure the screenshot directory exists
mkdir -p "$DIR"

# Initialize counter
count=1

while true; do
    # Capture screenshot with fbo and save it with an incrementing filename
    ./fbo -d "$DEVICE" -o "$DIR/$count.netpbm" $COLOR_MODE
    
    # Increment the counter
    ((count++))
    
    # Sleep for 10 milliseconds
    sleep 0.01
done
