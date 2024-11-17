#!/bin/bash

# Clean and build
make clean
make

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "Build successful. Running dictionary codec..."
    ./dictionary_codec Column.txt
else
    echo "Build failed."
    exit 1
fi
python3 plot_results.py
