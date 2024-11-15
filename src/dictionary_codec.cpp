#include "dictionary_codec.h"
#include <fstream>
#include <algorithm>
#include <numeric>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <zstd.h>
#include <mutex>
#include <iostream>   // Added for std::cout
#include <iomanip>    // Added for std::setprecision

DictionaryCodec::DictionaryCodec() : mmap_fd(-1), mmap_data(nullptr), mmap_size(0) {}

DictionaryCodec::~DictionaryCodec() {
    if (mmap_data) {
        unmapFile();
    }
}
void DictionaryCodec::memoryMapFile(const std::string& filename) {
    if (mmap_data) {
        unmapFile();
    }
    
    mmap_fd = open(filename.c_str(), O_RDONLY);
    if (mmap_fd == -1) {
        throw std::runtime_error("Failed to open file for memory mapping");
    }
    
    mmap_size = lseek(mmap_fd, 0, SEEK_END);
    mmap_data = mmap(nullptr, mmap_size, PROT_READ, MAP_PRIVATE, mmap_fd, 0);
    
    if (mmap_data == MAP_FAILED) {
        close(mmap_fd);
        throw std::runtime_error("Failed to memory map file");
    }
}

void DictionaryCodec::unmapFile() {
    if (mmap_data) {
        munmap(mmap_data, mmap_size);
        close(mmap_fd);
        mmap_data = nullptr;
        mmap_fd = -1;
        mmap_size = 0;
    }
}
double DictionaryCodec::getCompressionRatio() const {
    if (original_data.empty() || encoded_data.empty()) {
        return 0.0;
    }
    
    size_t original_size = 0;
    for (const auto& str : original_data) {
        original_size += str.length();
    }
    
    size_t encoded_size = encoded_data.size() * sizeof(uint32_t) + 
                         dictionary.size() * sizeof(uint32_t);
                         
    return static_cast<double>(original_size) / encoded_size;
}

size_t DictionaryCodec::getMemoryUsage() const {
    size_t usage = 0;
    for (const auto& [str, _] : dictionary) {
        usage += str.length() + sizeof(uint32_t);
    }
    for (const auto& str : reverse_dictionary) {
        usage += str.length();
    }
    usage += encoded_data.size() * sizeof(uint32_t);
    for (const auto& str : original_data) {
        usage += str.length();
    }
    return usage;
}

void DictionaryCodec::encodeFile(const std::string& filename, int num_threads) {
    // Get file size
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    size_t file_size = file.tellg();
    file.seekg(0);
    
    // Calculate smaller chunk size and buffer sizes
    const size_t CHUNK_SIZE = 10 * 1024 * 1024;  // 10MB chunks (reduced from 100MB)
    const size_t MAX_LINES_PER_CHUNK = CHUNK_SIZE / 16;  // Estimate average line length
    const size_t num_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    // Pre-allocate a fixed size for the dictionary to prevent reallocation
    dictionary.reserve(1000000);  // Reserve space for 1M unique strings
    reverse_dictionary.reserve(1000000);
    
    // Count lines first to properly size vectors
    size_t total_lines = 0;
    {
        std::string line;
        std::ifstream count_file(filename);
        while (std::getline(count_file, line)) {
            total_lines++;
        }
    }
    
    // Reserve space once
    encoded_data.resize(total_lines);
    
    // Reopen file for processing
    file.clear();
    file.seekg(0);
    
    std::string line;
    size_t processed_size = 0;
    size_t processed_lines = 0;
    
    // Process file in chunks
    while (!file.eof()) {
        std::vector<std::string> chunk_data;
        chunk_data.reserve(MAX_LINES_PER_CHUNK);
        size_t chunk_size = 0;
        size_t chunk_start_line = processed_lines;
        
        // Read chunk
        while (std::getline(file, line) && chunk_size < CHUNK_SIZE) {
            chunk_size += line.length() + 1;
            chunk_data.push_back(std::move(line));  // Use move to save memory
            
            if (chunk_data.size() >= MAX_LINES_PER_CHUNK) {
                break;
            }
        }
        
        if (chunk_data.empty()) {
            break;
        }
        
        // Process chunk with multiple threads
        size_t lines_in_chunk = chunk_data.size();
        size_t lines_per_thread = lines_in_chunk / num_threads;
        
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        
        for (int i = 0; i < num_threads; i++) {
            size_t start = i * lines_per_thread;
            size_t end = (i == num_threads - 1) ? lines_in_chunk : (i + 1) * lines_per_thread;
            
            // Create views instead of copying data
            size_t thread_offset = chunk_start_line + start;
            auto chunk_begin = chunk_data.begin() + start;
            auto chunk_end = chunk_data.begin() + end;
            
            threads.emplace_back(&DictionaryCodec::encodeSingleThread, this,
                std::vector<std::string>(chunk_begin, chunk_end), thread_offset);
        }
        
        // Wait for threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        processed_lines += lines_in_chunk;
        processed_size += chunk_size;
        
        // Print progress
        float progress = (float)processed_size / file_size * 100;
        std::cout << "\rProcessing: " << std::fixed << std::setprecision(1) 
                  << progress << "% complete" << std::flush;
        
        // Clear chunk data to free memory
        std::vector<std::string>().swap(chunk_data);
    }
    
    std::cout << "\nProcessed " << processed_lines << " lines\n";
    std::cout << "Dictionary size: " << dictionary.size() << " entries\n";
}

void DictionaryCodec::encodeSingleThread(const std::vector<std::string>& chunk, size_t start_idx) {
    const size_t BATCH_SIZE = 100;  // Reduced batch size for better memory usage
    std::vector<std::pair<std::string, size_t>> pending_inserts;
    pending_inserts.reserve(BATCH_SIZE);
    
    for (size_t i = 0; i < chunk.size(); i++) {
        const auto& str = chunk[i];
        
        {
            std::shared_lock<std::shared_mutex> read_lock(mutex);
            auto it = dictionary.find(str);
            if (it != dictionary.end()) {
                encoded_data[start_idx + i] = it->second;
                continue;
            }
        }
        
        pending_inserts.emplace_back(str, start_idx + i);
        
        if (pending_inserts.size() >= BATCH_SIZE || i == chunk.size() - 1) {
            std::unique_lock<std::shared_mutex> write_lock(mutex);
            
            for (const auto& [pending_str, idx] : pending_inserts) {
                auto it = dictionary.find(pending_str);
                if (it == dictionary.end()) {
                    uint32_t new_id = dictionary.size();
                    dictionary[pending_str] = new_id;
                    reverse_dictionary.push_back(pending_str);
                    encoded_data[idx] = new_id;
                } else {
                    encoded_data[idx] = it->second;
                }
            }
            
            pending_inserts.clear();
        }
    }
}

std::vector<size_t> DictionaryCodec::baselineFind(const std::string& target) const {
    std::vector<size_t> results;
    for (size_t i = 0; i < original_data.size(); i++) {
        if (original_data[i] == target) {
            results.push_back(i);
        }
    }
    return results;
}

std::vector<size_t> DictionaryCodec::findMatches(const std::string& target) const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    std::vector<size_t> results;
    
    auto it = dictionary.find(target);
    if (it == dictionary.end()) {
        return results;
    }
    
    uint32_t target_id = it->second;
    for (size_t i = 0; i < encoded_data.size(); i++) {
        if (encoded_data[i] == target_id) {
            results.push_back(i);
        }
    }
    
    return results;
}

std::vector<size_t> DictionaryCodec::findMatchesSIMD(const std::string& target) const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    std::vector<size_t> results;
    
    auto it = dictionary.find(target);
    if (it == dictionary.end()) {
        return results;
    }
    
    uint32_t target_id = it->second;
    __m256i target_vec = _mm256_set1_epi32(target_id);
    
    for (size_t i = 0; i < encoded_data.size(); i += 8) {
        __m256i data_vec = _mm256_loadu_si256((__m256i*)&encoded_data[i]);
        __m256i cmp = _mm256_cmpeq_epi32(data_vec, target_vec);
        int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp));
        
        while (mask) {
            int idx = _tzcnt_u32(mask);
            if (i + idx < encoded_data.size()) {
                results.push_back(i + idx);
            }
            mask &= mask - 1;
        }
    }
    
    return results;
}

std::vector<std::pair<std::string, std::vector<size_t>>> DictionaryCodec::prefixSearch(
    const std::string& prefix) const {
    return prefixSearchSIMD(prefix);  // Default to SIMD implementation
}

std::vector<std::pair<std::string, std::vector<size_t>>> DictionaryCodec::prefixSearchSIMD(
    const std::string& prefix) const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    std::vector<std::pair<std::string, std::vector<size_t>>> results;
    
    if (prefix.empty()) {
        return results;
    }
    
    // Pre-allocate space for results to avoid reallocation
    results.reserve(100);  // Reasonable initial size
    
    // First pass: collect matching strings from dictionary
    std::vector<std::pair<std::string, uint32_t>> matching_entries;
    matching_entries.reserve(100);
    
    for (const auto& [str, id] : dictionary) {
        if (str.length() >= prefix.length() && 
            str.compare(0, prefix.length(), prefix) == 0) {
            matching_entries.emplace_back(str, id);
        }
    }
    
    // Process each matching entry
    for (const auto& [str, id] : matching_entries) {
        std::vector<size_t> positions;
        positions.reserve(100);  // Reserve reasonable space
        
        // Process encoded data in SIMD chunks
        const size_t SIMD_WIDTH = 8;  // AVX2 processes 8 integers at once
        const size_t aligned_size = encoded_data.size() & ~(SIMD_WIDTH - 1);
        
        __m256i target_vec = _mm256_set1_epi32(id);
        
        // Process aligned data
        for (size_t i = 0; i < aligned_size; i += SIMD_WIDTH) {
            if (i + SIMD_WIDTH <= encoded_data.size()) {  // Bounds check
                __m256i data_vec = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i*>(&encoded_data[i]));
                __m256i cmp = _mm256_cmpeq_epi32(data_vec, target_vec);
                int mask = _mm256_movemask_ps(_mm256_castsi256_ps(cmp));
                
                while (mask) {
                    int idx = _tzcnt_u32(mask);
                    if (i + idx < encoded_data.size()) {  // Additional bounds check
                        positions.push_back(i + idx);
                    }
                    mask &= mask - 1;
                }
            }
        }
        
        // Process remaining elements
        for (size_t i = aligned_size; i < encoded_data.size(); i++) {
            if (encoded_data[i] == id) {
                positions.push_back(i);
            }
        }
        
        if (!positions.empty()) {
            results.emplace_back(str, std::move(positions));
        }
    }
    
    return results;
}

std::vector<std::pair<std::string, std::vector<size_t>>> DictionaryCodec::baselinePrefixSearch(
    const std::string& prefix) const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    std::vector<std::pair<std::string, std::vector<size_t>>> results;
    
    if (prefix.empty()) {
        return results;
    }
    
    // Use map to collect matches with pre-allocated vectors
    std::unordered_map<std::string, std::vector<size_t>> matches;
    
    // First pass: find all matching strings in dictionary
    std::vector<std::string> matching_strings;
    matching_strings.reserve(100);
    
    for (const auto& [str, _] : dictionary) {
        if (str.length() >= prefix.length() && 
            str.compare(0, prefix.length(), prefix) == 0) {
            matching_strings.push_back(str);
            matches[str].reserve(100);  // Pre-allocate space for positions
        }
    }
    
    // Second pass: find positions
    for (size_t i = 0; i < encoded_data.size(); i++) {
        uint32_t id = encoded_data[i];
        if (id < reverse_dictionary.size()) {  // Bounds check
            const std::string& str = reverse_dictionary[id];
            if (str.length() >= prefix.length() && 
                str.compare(0, prefix.length(), prefix) == 0) {
                matches[str].push_back(i);
            }
        }
    }
    
    // Build results
    results.reserve(matches.size());
    for (const auto& str : matching_strings) {
        auto it = matches.find(str);
        if (it != matches.end() && !it->second.empty()) {
            results.emplace_back(str, std::move(it->second));
        }
    }
    
    return results;
}

QueryMetrics DictionaryCodec::benchmarkSearch(
    const std::vector<std::string>& queries, bool use_simd) const {
    QueryMetrics metrics;
    metrics.clear();
    std::vector<double> latencies;
    latencies.reserve(queries.size());
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (const auto& query : queries) {
        auto query_start = std::chrono::high_resolution_clock::now();
        
        std::vector<size_t> results;
        if (use_simd) {
            results = findMatchesSIMD(query);
        } else {
            results = baselineFind(query);
        }
        
        auto query_end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            query_end - query_start).count();
        
        latencies.push_back(duration);
        metrics.total_matches += results.size();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();
    
    metrics.total_queries = queries.size();
    metrics.avg_latency_us = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    
    std::sort(latencies.begin(), latencies.end());
    metrics.p95_latency_us = latencies[size_t(latencies.size() * 0.95)];
    metrics.p99_latency_us = latencies[size_t(latencies.size() * 0.99)];
    
    metrics.throughput_qps = queries.size() / (total_duration / 1000000.0);
    
    return metrics;
}

QueryMetrics DictionaryCodec::benchmarkPrefixSearch(
    const std::vector<std::string>& prefixes, bool use_simd) const {
    QueryMetrics metrics;
    metrics.clear();
    
    if (prefixes.empty()) {
        return metrics;
    }
    
    std::vector<double> latencies;
    latencies.reserve(prefixes.size());
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (const auto& prefix : prefixes) {
        auto query_start = std::chrono::high_resolution_clock::now();
        
        try {
            std::vector<std::pair<std::string, std::vector<size_t>>> results;
            if (use_simd) {
                results = prefixSearchSIMD(prefix);
            } else {
                results = baselinePrefixSearch(prefix);
            }
            
            auto query_end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                query_end - query_start).count();
            
            latencies.push_back(duration);
            for (const auto& [_, positions] : results) {
                metrics.total_matches += positions.size();
            }
        } catch (const std::exception& e) {
            std::cerr << "Error processing prefix '" << prefix << "': " << e.what() << std::endl;
            continue;
        }
    }
    
    if (latencies.empty()) {
        return metrics;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();
    
    metrics.total_queries = prefixes.size();
    metrics.avg_latency_us = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    
    std::sort(latencies.begin(), latencies.end());
    metrics.p95_latency_us = latencies[size_t(latencies.size() * 0.95)];
    metrics.p99_latency_us = latencies[size_t(latencies.size() * 0.99)];
    
    metrics.throughput_qps = prefixes.size() / (total_duration / 1000000.0);
    
    return metrics;
}

void DictionaryCodec::compressChunk(const char* input, size_t size, 
                                  std::vector<uint8_t>& output) const {
    size_t compressed_bound = ZSTD_compressBound(size);
    output.resize(compressed_bound);
    
    size_t compressed_size = ZSTD_compress(output.data(), compressed_bound,
                                         input, size, 3);  // compression level 3
    
    if (ZSTD_isError(compressed_size)) {
        throw std::runtime_error("Compression failed");
    }
    
    output.resize(compressed_size);
}

void DictionaryCodec::decompressChunk(const uint8_t* input, size_t size,
                                    char* output) const {
    size_t decompressed_size = ZSTD_decompress(output, encoded_data.size() * sizeof(uint32_t),
                                             input, size);
    
    if (ZSTD_isError(decompressed_size)) {
        throw std::runtime_error("Decompression failed");
    }
}

void DictionaryCodec::saveToFile(const std::string& filename) const {
    std::ofstream file(filename, std::ios::binary);
    
    size_t dict_size = dictionary.size();
    file.write(reinterpret_cast<const char*>(&dict_size), sizeof(dict_size));
    
    for (const auto& [str, id] : dictionary) {
        size_t str_len = str.length();
        file.write(reinterpret_cast<const char*>(&str_len), sizeof(str_len));
        file.write(str.c_str(), str_len);
        file.write(reinterpret_cast<const char*>(&id), sizeof(id));
    }
    
    std::vector<uint8_t> compressed_data;
    compressChunk(reinterpret_cast<const char*>(encoded_data.data()), 
                 encoded_data.size() * sizeof(uint32_t),
                 compressed_data);
    
    size_t comp_size = compressed_data.size();
    file.write(reinterpret_cast<const char*>(&comp_size), sizeof(comp_size));
    file.write(reinterpret_cast<const char*>(compressed_data.data()), comp_size);
}

void DictionaryCodec::loadFromFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    
    size_t dict_size;
    file.read(reinterpret_cast<char*>(&dict_size), sizeof(dict_size));
    
    dictionary.clear();
    reverse_dictionary.clear();
    reverse_dictionary.reserve(dict_size);
    
    for (size_t i = 0; i < dict_size; i++) {
        size_t str_len;
        file.read(reinterpret_cast<char*>(&str_len), sizeof(str_len));
        
        std::string str(str_len, '\0');
        file.read(&str[0], str_len);
        
        uint32_t id;
        file.read(reinterpret_cast<char*>(&id), sizeof(id));
        
        dictionary[str] = id;
        reverse_dictionary.push_back(str);  // Fixed the incomplete push_ to push_back
    }
    
    // Read compressed encoded data
    size_t comp_size;
    file.read(reinterpret_cast<char*>(&comp_size), sizeof(comp_size));
    
    std::vector<uint8_t> compressed_data(comp_size);
    file.read(reinterpret_cast<char*>(compressed_data.data()), comp_size);
    
    // Decompress data
    size_t decom_size = encoded_data.size() * sizeof(uint32_t);
    encoded_data.resize(decom_size / sizeof(uint32_t));
    decompressChunk(compressed_data.data(), comp_size, 
                    reinterpret_cast<char*>(encoded_data.data()));
}
void DictionaryCodec::saveState(const std::string& directory) const {
    // Create directory if it doesn't exist
    std::filesystem::create_directories(directory);
    
    // Save dictionary and encoded data
    std::string dict_file = directory + "/dictionary.bin";
    std::string data_file = directory + "/encoded_data.bin";
    std::string metadata_file = directory + "/metadata.txt";
    
    // Save dictionary and encoded data using existing method
    saveToFile(dict_file);
    
    // Save metadata
    std::ofstream meta(metadata_file);
    meta << "Dictionary size: " << dictionary.size() << "\n";
    meta << "Encoded data size: " << encoded_data.size() << "\n";
    meta << "Compression ratio: " << getCompressionRatio() << "\n";
    meta << "Memory usage (MB): " << getMemoryUsage() / (1024.0 * 1024.0) << "\n";
}

void DictionaryCodec::loadState(const std::string& directory) {
    std::string dict_file = directory + "/dictionary.bin";
    
    if (!std::filesystem::exists(dict_file)) {
        throw std::runtime_error("No saved state found in directory: " + directory);
    }
    
    loadFromFile(dict_file);
}

void DictionaryCodec::saveResults(const std::string& directory, const std::string& test_name) const {
    std::filesystem::create_directories(directory);
    
    std::string results_file = directory + "/" + test_name + "_results.csv";
    std::ofstream file(results_file);
    
    // Write header
    file << "Index,Original,Encoded,Dictionary_ID\n";
    
    // Write data
    for (size_t i = 0; i < encoded_data.size(); i++) {
        if (i < original_data.size()) {
            file << i << ","
                 << original_data[i] << ","
                 << encoded_data[i] << ","
                 << dictionary.at(original_data[i]) << "\n";
        }
    }
    
    // Save summary
    std::string summary_file = directory + "/" + test_name + "_summary.txt";
    std::ofstream summary(summary_file);
    summary << "Test Summary: " << test_name << "\n"
           << "-------------------\n"
           << "Total entries: " << encoded_data.size() << "\n"
           << "Dictionary size: " << dictionary.size() << "\n"
           << "Compression ratio: " << getCompressionRatio() << "\n"
           << "Memory usage (MB): " << getMemoryUsage() / (1024.0 * 1024.0) << "\n";
}

