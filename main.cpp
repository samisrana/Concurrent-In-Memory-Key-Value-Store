#include "dictionary_codec.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <random>
#include <thread>
#include <iomanip>
#include <filesystem>
#include <numeric>
#include <algorithm>

namespace chr = std::chrono;

struct EncodingResult {
    int threads;
    double duration_ms;
    double throughput_mbs;
    size_t dictionary_size;
};

struct SearchStats {
    double min_latency_us;
    double max_latency_us;
    double avg_latency_us;
    double median_latency_us;
    double p95_latency_us;
    double throughput_qps;
    size_t total_matches;
};

struct SearchResult {
    std::string method;
    SearchStats stats;
};

struct PrefixResult {
    std::string method;
    size_t prefix_length;
    SearchStats stats;
};

SearchStats calculateStats(std::vector<double>& times, size_t total_matches) {
    if (times.empty()) return SearchStats{0, 0, 0, 0, 0, 0, total_matches};
    
    std::sort(times.begin(), times.end());
    size_t size = times.size();
    
    double min = times.front();
    double max = times.back();
    double sum = std::accumulate(times.begin(), times.end(), 0.0);
    double avg = sum / size;
    double median = size % 2 == 0 ? 
                   (times[size/2 - 1] + times[size/2]) / 2 : 
                   times[size/2];
    double p95 = times[static_cast<size_t>(size * 0.95)];
    double throughput = (size * 1000000.0) / sum;
    
    return SearchStats{
        min,
        max,
        avg,
        median,
        p95,
        throughput,
        total_matches
    };
}

void writeResults(const std::string& output_dir,
                 const std::vector<EncodingResult>& encoding_results,
                 const std::vector<SearchResult>& search_results,
                 const std::vector<PrefixResult>& prefix_results) {
    
    std::filesystem::create_directories(output_dir);
    
    // Write encoding results
    std::ofstream encoding_file(output_dir + "/encoding_results.csv");
    encoding_file << "Threads,Duration_ms,Throughput_MBps,DictionarySize\n";
    for (const auto& result : encoding_results) {
        encoding_file << result.threads << ","
                     << result.duration_ms << ","
                     << result.throughput_mbs << ","
                     << result.dictionary_size << "\n";
    }
    
    // Write search results with detailed stats
    std::ofstream search_file(output_dir + "/search_results.csv");
    search_file << "Method,MinLatency_us,MaxLatency_us,AvgLatency_us,MedianLatency_us,P95Latency_us,Throughput_QPS,TotalMatches\n";
    for (const auto& result : search_results) {
        search_file << result.method << ","
                   << result.stats.min_latency_us << ","
                   << result.stats.max_latency_us << ","
                   << result.stats.avg_latency_us << ","
                   << result.stats.median_latency_us << ","
                   << result.stats.p95_latency_us << ","
                   << result.stats.throughput_qps << ","
                   << result.stats.total_matches << "\n";
    }
    
    // Write prefix search results with detailed stats
    std::ofstream prefix_file(output_dir + "/prefix_results.csv");
    prefix_file << "Method,PrefixLength,MinLatency_us,MaxLatency_us,AvgLatency_us,MedianLatency_us,P95Latency_us,Throughput_QPS,TotalMatches\n";
    for (const auto& result : prefix_results) {
        prefix_file << result.method << ","
                   << result.prefix_length << ","
                   << result.stats.min_latency_us << ","
                   << result.stats.max_latency_us << ","
                   << result.stats.avg_latency_us << ","
                   << result.stats.median_latency_us << ","
                   << result.stats.p95_latency_us << ","
                   << result.stats.throughput_qps << ","
                   << result.stats.total_matches << "\n";
    }
}

void validateFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    
    std::cout << "File opened successfully\n";
    std::string line;
    for (int i = 0; i < 5 && std::getline(file, line); i++) {
        std::cout << "Sample line " << (i+1) << ": " << line << "\n";
    }
}

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: " << argv[0] << " <input_file>\n";
            return 1;
        }

        const std::string input_filename = argv[1];
        std::string output_dir = "benchmark_results_" + 
                               std::filesystem::path(input_filename).stem().string();
        
        std::cout << "Dictionary Codec Performance Analysis\n";
        std::cout << "===================================\n\n";
        
        validateFile(input_filename);
        DictionaryCodec codec;

        std::vector<EncodingResult> encoding_results;
        std::vector<SearchResult> search_results;
        std::vector<PrefixResult> prefix_results;

        // Part 1: Multi-threaded Encoding Performance
        std::cout << "\n1. Dictionary Encoding Performance with Different Thread Counts\n";
        std::cout << "--------------------------------------------------------\n";
        std::vector<int> thread_counts = {1, 2, 4, 8};

        for (int threads : thread_counts) {
            std::cout << "\nTesting with " << threads << " threads...\n";
            auto start = chr::steady_clock::now();
            
            codec.encodeFile(input_filename, threads);
            
            auto end = chr::steady_clock::now();
            auto duration = chr::duration_cast<chr::milliseconds>(end - start).count();
            
            size_t file_size = std::ifstream(input_filename, std::ios::ate | std::ios::binary).tellg();
            double throughput = (file_size / 1024.0 / 1024.0) / (duration / 1000.0);
            
            encoding_results.push_back({
                threads,
                static_cast<double>(duration),
                throughput,
                codec.getDictionarySize()
            });
            
            std::cout << "Time: " << duration << "ms\n";
            std::cout << "Throughput: " << throughput << " MB/s\n";
            std::cout << "Dictionary size: " << codec.getDictionarySize() << " entries\n";
        }

        // Part 2: Generate test queries
        const auto& reverse_dict = codec.getReverseDictionary();
        std::vector<std::string> test_queries;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, reverse_dict.size() - 1);
        
        const int NUM_QUERIES = 10;
        for (int i = 0; i < NUM_QUERIES; i++) {
            test_queries.push_back(reverse_dict[dis(gen)]);
        }

        // Single Item Search Tests
        std::cout << "\n2. Single Item Search Performance\n";
        std::cout << "--------------------------------\n";
        
        // Warmup phase
        std::cout << "Warming up...\n";
        for (int i = 0; i < 3; i++) {
            for (const auto& query : test_queries) {
                codec.baselineFind(query);
                codec.findMatches(query);
                codec.findMatchesSIMD(query);
            }
        }

        // Vanilla search
        std::cout << "Running vanilla search benchmark...\n";
        size_t total_matches = 0;
        std::vector<double> vanilla_times;

        for (const auto& query : test_queries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            
            auto start = chr::steady_clock::now();
            auto results = codec.baselineFind(query);
            auto end = chr::steady_clock::now();
            
            auto duration = chr::duration_cast<chr::microseconds>(end - start).count();
            total_matches += results.size();
            if (duration > 0) {
                vanilla_times.push_back(duration);
            }
        }

        if (!vanilla_times.empty()) {
            search_results.push_back({"Vanilla", calculateStats(vanilla_times, total_matches)});
        }

        // Dictionary search without SIMD
        std::cout << "\nRunning dictionary search benchmark...\n";
        total_matches = 0;
        std::vector<double> dict_times;

        for (const auto& query : test_queries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            
            auto start = chr::steady_clock::now();
            auto results = codec.findMatches(query);
            auto end = chr::steady_clock::now();
            
            auto duration = chr::duration_cast<chr::microseconds>(end - start).count();
            total_matches += results.size();
            if (duration > 0) {
                dict_times.push_back(duration);
            }
        }

        if (!dict_times.empty()) {
            search_results.push_back({"Dictionary", calculateStats(dict_times, total_matches)});
        }

        // Dictionary search with SIMD
        std::cout << "\nRunning SIMD search benchmark...\n";
        total_matches = 0;
        std::vector<double> simd_times;

        for (const auto& query : test_queries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            
            auto start = chr::steady_clock::now();
            auto results = codec.findMatchesSIMD(query);
            auto end = chr::steady_clock::now();
            
            auto duration = chr::duration_cast<chr::microseconds>(end - start).count();
            total_matches += results.size();
            if (duration > 0) {
                simd_times.push_back(duration);
            }
        }

        if (!simd_times.empty()) {
            search_results.push_back({"SIMD", calculateStats(simd_times, total_matches)});
        }

        // Prefix Search Tests
        std::cout << "\n3. Prefix Search Performance\n";
        std::cout << "---------------------------\n";
        std::vector<size_t> prefix_lengths = {2, 4, 8};
        
        for (size_t prefix_len : prefix_lengths) {
            std::vector<std::string> prefix_queries;
            for (int i = 0; i < NUM_QUERIES; i++) {
                std::string str = reverse_dict[dis(gen)];
                if (str.length() > prefix_len) {
                    prefix_queries.push_back(str.substr(0, prefix_len));
                }
            }

            if (prefix_queries.empty()) continue;

            std::cout << "\nPrefix length " << prefix_len << ":\n";
            
            // Warmup
            std::cout << "Warming up...\n";
            for (const auto& prefix : prefix_queries) {
                codec.baselinePrefixSearch(prefix);
                codec.prefixSearchSIMD(prefix);
            }
            
            // Baseline prefix search
            size_t progress = 0;
            total_matches = 0;
            std::vector<double> vanilla_prefix_times;
            std::cout << "Running baseline prefix search...\n";
            
            for (const auto& prefix : prefix_queries) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                
                auto start = chr::steady_clock::now();
                auto results = codec.baselinePrefixSearch(prefix);
                auto end = chr::steady_clock::now();
                
                auto duration = chr::duration_cast<chr::microseconds>(end - start).count();
                
                for (const auto& match : results) {
                    total_matches += match.second.size();
                }
                
                if (duration > 0) {
                    vanilla_prefix_times.push_back(duration);
                }
                
                // Update progress
                progress++;
                std::cout << "\rProgress: " << progress << "/" << prefix_queries.size() 
                          << " queries (" << (progress * 100.0 / prefix_queries.size()) 
                          << "%)" << std::flush;
            }
            
            if (!vanilla_prefix_times.empty()) {
                prefix_results.push_back({
                    "Vanilla",
                    prefix_len,
                    calculateStats(vanilla_prefix_times, total_matches)
                });
            }
            std::cout << "\nCompleted with " << total_matches << " total matches\n";
            
            // SIMD prefix search
            progress = 0;
            total_matches = 0;
            std::vector<double> simd_prefix_times;
            std::cout << "\nRunning SIMD prefix search...\n";
            
            for (const auto& prefix : prefix_queries) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                
                auto start = chr::steady_clock::now();
                auto results = codec.prefixSearchSIMD(prefix);
                auto end = chr::steady_clock::now();
                
                auto duration = chr::duration_cast<chr::microseconds>(end - start).count();
                
                for (const auto& match : results) {
                    total_matches += match.second.size();
                }
                
                if (duration > 0) {
                    simd_prefix_times.push_back(duration);
                }
                
                // Update progress
                progress++;
                std::cout << "\rProgress: " << progress << "/" << prefix_queries.size() 
                          << " queries (" << (progress * 100.0 / prefix_queries.size()) 
                          << "%)" << std::flush;
            }
            
            if (!simd_prefix_times.empty()) {
                prefix_results.push_back({
                    "SIMD",
                    prefix_len,
                    calculateStats(simd_prefix_times, total_matches)
                });
            }
            std::cout << "\nCompleted with " << total_matches << " total matches\n";
        }

        // Write results to files
        writeResults(output_dir, encoding_results, search_results, prefix_results);
        
        std::cout << "\nResults have been written to: " << output_dir << "\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
