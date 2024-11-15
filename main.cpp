#include "dictionary_codec.h"
#include "benchmark.h"
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <cstdlib>
#include <filesystem>

// Function to check file size
size_t getFileSize(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Unable to open file: " + filename);
    }
    return file.tellg();
}

// Function to validate the file
void validateFile(const std::string& filename) {
    size_t file_size = getFileSize(filename);
    std::cout << "File size: " << file_size / (1024 * 1024) << " MB\n";
    
    std::ifstream test_file(filename);
    if (!test_file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    
    std::string line;
    size_t line_count = 0;
    while (std::getline(test_file, line) && line_count < 5) {
        std::cout << "Sample line " << line_count + 1 << ": " << line << "\n";
        line_count++;
    }
    
    std::cout << "File validation successful\n";
}

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: " << argv[0] << " <input_file>\n";
            return 1;
        }

        const std::string input_filename = argv[1];
        std::cout << "Starting Dictionary Codec Benchmark Suite\n";
        std::cout << "=========================================\n";
        
        validateFile(input_filename);
        
        // Configure benchmarks
        BenchmarkConfig config;
        config.thread_counts = {1, 2, 4, 8, 16, 32};
        config.num_queries_per_test = 10000;
        config.num_warm_up_queries = 1000;
        
        DictionaryCodec codec;
        BenchmarkSuite benchmark(codec, config);
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::cout << "\nRunning benchmarks...\n";
        benchmark.runAllBenchmarks(input_filename);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            end_time - start_time).count();
        
        const auto& results = benchmark.getResults();
        
        // Print summary
        std::cout << "\nBenchmark Summary:\n";
        std::cout << "==================\n";
        std::cout << "Total runtime: " << duration << " seconds\n";
        std::cout << "Peak memory usage: " << results.peak_memory_usage_mb << " MB\n";
        std::cout << "Average compression ratio: " << results.compression_ratio << "x\n";
        
        // Print encoding performance summary
        std::cout << "\nEncoding Performance Summary:\n";
        for (const auto& encoding : results.encoding_results) {
            std::cout << "Threads: " << encoding.num_threads 
                      << ", Throughput: " << encoding.throughput_mbs << " MB/s\n";
        }
        
        // Print search performance summary
        std::cout << "\nSearch Performance Summary:\n";
        for (const auto& search : results.search_results) {
            if (search.test_name.find("SIMD") != std::string::npos) {
                std::cout << search.test_name << " - Avg Latency: " 
                          << search.avg_latency_us << "μs, Throughput: "
                          << search.throughput_qps << " QPS\n";
            }
        }
        
        // Create results directory and save results
        std::string results_dir = "results_" + 
                                std::filesystem::path(input_filename).stem().string();
        std::filesystem::create_directories(results_dir);
        
        // Save results
        benchmark.saveResultsToFile(results_dir);
        
        std::cout << "\nBenchmark completed successfully.\n";
        std::cout << "Results saved in: " << std::filesystem::absolute(results_dir) << "\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
