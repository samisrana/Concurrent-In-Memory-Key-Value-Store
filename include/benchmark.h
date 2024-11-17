#pragma once

#include "dictionary_codec.h"
#include <random>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <unordered_set>

// Benchmark configuration structure
struct BenchmarkConfig {
    std::vector<int> thread_counts{1, 2, 4, 8, 16};
    std::vector<size_t> value_sizes{8, 64, 256};  // in bytes
    std::vector<double> read_ratios{0.2, 0.5, 0.8};
    size_t num_queries_per_test = 100;
    size_t num_warm_up_queries = 10;
    std::vector<size_t> prefix_lengths{2, 4, 8};
};

// Detailed performance metrics
struct BenchmarkResult {
    // Encoding metrics
    struct EncodingMetrics {
        int num_threads;
        double duration_ms;
        double throughput_mbs;
        double compression_ratio;
        size_t memory_usage_mb;
    };
    std::vector<EncodingMetrics> encoding_results;

    // Search metrics
    struct SearchMetrics {
        std::string test_name;
        double avg_latency_us;
        double p50_latency_us;
        double p95_latency_us;
        double p99_latency_us;
        double throughput_qps;
        size_t total_matches;
        size_t cache_misses;
        size_t simd_operations;
    };
    std::vector<SearchMetrics> search_results;

    // Memory metrics
    size_t peak_memory_usage_mb;
    double avg_memory_usage_mb;
    double compression_ratio;

    void clear() {
        encoding_results.clear();
        search_results.clear();
        peak_memory_usage_mb = 0;
        avg_memory_usage_mb = 0;
        compression_ratio = 0;
    }
};

class BenchmarkSuite {
private:
    DictionaryCodec& codec;
    BenchmarkConfig config;
    BenchmarkResult results;

    // Helper methods
    std::vector<std::string> generateRandomPrefixes(size_t count, size_t length) const;
    void printResults() const;
    double measureMemoryUsage() const;
    void warmUp() const;
    
    // Benchmark specific operations
    void benchmarkEncoding(const std::string& filename);
    void benchmarkExactSearch();
    void benchmarkPrefixSearch();
    void benchmarkBatchOperations();
    void benchmarkWithDifferentSizes();

public:
    explicit BenchmarkSuite(DictionaryCodec& c, BenchmarkConfig cfg = BenchmarkConfig())
        : codec(c), config(cfg) {}

    // Main benchmark methods
    void runAllBenchmarks(const std::string& filename);
    
    // Individual benchmark runners
    void runEncodingBenchmark(const std::string& filename);
    void runSearchBenchmark();
    void runPrefixSearchBenchmark();
    void runMemoryBenchmark();
    void runScalabilityBenchmark();
    
    // Results and configuration access
    const BenchmarkResult& getResults() const { return results; }
    void setConfig(const BenchmarkConfig& new_config) { config = new_config; }
    const BenchmarkConfig& getConfig() const { return config; }
    
    std::vector<std::string> generateQueries(size_t count, size_t prefix_len = 0) const;
    // New method for saving results
    void saveResultsToFile(const std::string& results_dir);
};

// Utility functions for results analysis
namespace BenchmarkUtils {
    std::string formatDuration(double microseconds);
    std::string formatThroughput(double qps);
    std::string formatMemory(size_t bytes);
    double calculateSpeedup(double baseline, double improved);
    double calculateEfficiency(int num_threads, double speedup);
}
