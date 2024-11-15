#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <immintrin.h>
#include <thread>
#include <chrono>
#include <filesystem>

struct QueryMetrics {
    double avg_latency_us;
    double p95_latency_us;
    double p99_latency_us;
    size_t total_queries;
    size_t total_matches;
    double throughput_qps;
    
    void clear() {
        avg_latency_us = p95_latency_us = p99_latency_us = 0;
        total_queries = total_matches = 0;
        throughput_qps = 0;
    }
};

class DictionaryCodec {
private:
    // Dictionary storage
    std::unordered_map<std::string, uint32_t> dictionary;
    std::vector<std::string> reverse_dictionary;
    std::vector<uint32_t> encoded_data;
    std::vector<std::string> original_data;
    
    // Thread safety
    mutable std::shared_mutex mutex;
    
    // Memory mapped file support
    int mmap_fd;
    void* mmap_data;
    size_t mmap_size;
    
    // Helper functions
    bool simdComparePrefix(const char* data, const char* prefix, size_t prefix_len) const;
    void simdScanChunk(__m256i* chunk, const std::string& target, std::vector<size_t>& results) const;
    void compressChunk(const char* input, size_t size, std::vector<uint8_t>& output) const;
    void decompressChunk(const uint8_t* input, size_t size, char* output) const;
    void memoryMapFile(const std::string& filename);
    void unmapFile();

    static constexpr size_t MAX_DICTIONARY_SIZE = 1000000;  // 1M entries
    static constexpr size_t CHUNK_SIZE = 10 * 1024 * 1024;  // 10MB


public:
    DictionaryCodec();
    ~DictionaryCodec();
    void saveState(const std::string& directory) const;
    void loadState(const std::string& directory);
    void saveResults(const std::string& directory, const std::string& test_name) const;

    
    // Accessor methods
    const std::vector<std::string>& getOriginalData() const { return original_data; }
    size_t getDictionarySize() const { return dictionary.size(); }
    size_t getDataSize() const { return encoded_data.size(); }
    double getCompressionRatio() const;
    size_t getMemoryUsage() const;
    
    // Core operations
    void encodeFile(const std::string& filename, int num_threads);
    void encodeSingleThread(const std::vector<std::string>& chunk, size_t start_idx);
    
    // Search operations
    std::vector<size_t> findMatches(const std::string& target) const;
    std::vector<size_t> findMatchesSIMD(const std::string& target) const;
    std::vector<size_t> baselineFind(const std::string& target) const;
    
    // Prefix search operations
    std::vector<std::pair<std::string, std::vector<size_t>>> prefixSearch(const std::string& prefix) const;
    std::vector<std::pair<std::string, std::vector<size_t>>> prefixSearchSIMD(const std::string& prefix) const;
    std::vector<std::pair<std::string, std::vector<size_t>>> baselinePrefixSearch(const std::string& prefix) const;
    
    // Batch operations
    std::vector<std::vector<size_t>> batchSearchSIMD(const std::vector<std::string>& queries) const;
    
    // Benchmark support
    QueryMetrics benchmarkSearch(const std::vector<std::string>& queries, bool use_simd) const;
    QueryMetrics benchmarkPrefixSearch(const std::vector<std::string>& prefixes, bool use_simd) const;
    
    // File I/O operations
    void saveToFile(const std::string& filename) const;
    void loadFromFile(const std::string& filename);
};
