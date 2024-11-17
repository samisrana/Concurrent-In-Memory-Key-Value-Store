#include "benchmark.h"
#include <fstream>
#include <algorithm>
#include <numeric>
#include <random>
#include <sys/time.h>
#include <sys/resource.h>
#include <iomanip>

void BenchmarkSuite::saveResultsToFile(const std::string& results_dir) {
    // Create results directory
    std::filesystem::create_directories(results_dir + "/benchmark_results");

    // Save encoding results
    std::ofstream encoding_file(results_dir + "/benchmark_results/encoding_results.csv");
    encoding_file << "Threads,Duration(ms),Throughput(MB/s),CompressionRatio,MemoryUsage(MB)\n";
    for (const auto& result : results.encoding_results) {
        encoding_file << result.num_threads << ","
                     << result.duration_ms << ","
                     << result.throughput_mbs << ","
                     << result.compression_ratio << ","
                     << result.memory_usage_mb << "\n";
    }

    // Save search results
    std::ofstream search_file(results_dir + "/benchmark_results/search_results.csv");
    search_file << "TestName,AvgLatency(us),P95Latency(us),P99Latency(us),Throughput(QPS),Matches\n";
    for (const auto& result : results.search_results) {
        search_file << result.test_name << ","
                   << result.avg_latency_us << ","
                   << result.p95_latency_us << ","
                   << result.p99_latency_us << ","
                   << result.throughput_qps << ","
                   << result.total_matches << "\n";
    }
}

std::vector<std::string> BenchmarkSuite::generateQueries(size_t count, size_t prefix_len) const {
    std::vector<std::string> queries;
    queries.reserve(count);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, codec.getDictionarySize() - 1);
    
    const auto& original_data = codec.getOriginalData();
    for (size_t i = 0; i < count; i++) {
        std::string query = !original_data.empty() ? 
            original_data[dis(gen) % original_data.size()] : "test";
            
        if (prefix_len > 0 && query.length() > prefix_len) {
            query = query.substr(0, prefix_len);
        }
        queries.push_back(query);
    }
    
    return queries;
}

std::vector<std::string> BenchmarkSuite::generateRandomPrefixes(size_t count, size_t length) const {
    std::vector<std::string> prefixes;
    prefixes.reserve(count);
    
    const auto& original_data = codec.getOriginalData();
    if (original_data.empty()) {
        std::cerr << "Warning: Empty data when generating random prefixes" << std::endl;
        return prefixes;
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, original_data.size() - 1);
    
    size_t attempts = 0;
    const size_t MAX_ATTEMPTS = count * 2;
    std::unordered_set<std::string> unique_prefixes; // To avoid duplicates
    
    while (unique_prefixes.size() < count && attempts < MAX_ATTEMPTS) {
        const std::string& str = original_data[dis(gen)];
        std::string prefix = str;
        
        if (prefix.length() > length) {
            prefix = prefix.substr(0, length);
        }
        
        if (!prefix.empty()) {
            unique_prefixes.insert(prefix);
        }
        attempts++;
    }
    
    prefixes.assign(unique_prefixes.begin(), unique_prefixes.end());
    
    std::cout << "Generated " << prefixes.size() << " unique prefixes of length " << length 
              << " (from " << attempts << " attempts)" << std::endl;
    
    return prefixes;
}

void BenchmarkSuite::warmUp() const {
    std::cout << "Generating warm-up queries..." << std::flush;
    auto warm_up_queries = generateQueries(config.num_warm_up_queries);
    std::cout << " generated " << warm_up_queries.size() << " warm-up queries\n" << std::flush;
    
    std::cout << "Running warm-up queries..." << std::flush;
    size_t total_matches = 0;
    for (const auto& query : warm_up_queries) {
        auto matches = codec.findMatchesSIMD(query);
        total_matches += matches.size();
    }
    std::cout << " completed with " << total_matches << " total matches\n" << std::flush;

}

double BenchmarkSuite::measureMemoryUsage() const {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss / 1024.0;  // Convert to MB
}

void BenchmarkSuite::runEncodingBenchmark(const std::string& filename) {
    for (int threads : config.thread_counts) {
        BenchmarkResult::EncodingMetrics metrics;
        metrics.num_threads = threads;
        
        auto start = std::chrono::high_resolution_clock::now();
        codec.encodeFile(filename, threads);
        auto end = std::chrono::high_resolution_clock::now();
        
        metrics.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start).count();
        
        // Calculate throughput
        size_t file_size = std::ifstream(filename, std::ios::ate | std::ios::binary).tellg();
        metrics.throughput_mbs = (file_size / 1024.0 / 1024.0) / (metrics.duration_ms / 1000.0);
        
        metrics.compression_ratio = codec.getCompressionRatio();
        metrics.memory_usage_mb = codec.getMemoryUsage() / (1024 * 1024);
        
        results.encoding_results.push_back(metrics);
        
        std::cout << "Threads: " << threads 
                  << ", Time: " << metrics.duration_ms << "ms"
                  << ", Throughput: " << metrics.throughput_mbs << "MB/s\n";
    }
}


void BenchmarkSuite::runSearchBenchmark() {
    std::cout << "Starting warmup phase..." << std::flush;
    warmUp();
    std::cout << " done\n" << std::flush;
    
    std::cout << "Generating queries..." << std::flush;
    auto queries = generateQueries(10); // Drastically reduced for testing
    std::cout << " generated " << queries.size() << " queries\n" << std::flush;
    
    if (queries.empty()) {
        std::cerr << "Error: No queries generated\n";
        return;
    }

    std::cout << "First few queries: ";
    for (size_t i = 0; i < std::min(queries.size(), size_t(3)); i++) {
        std::cout << queries[i] << " ";
    }
    std::cout << "\n" << std::flush;

    // Baseline search
    {
        std::cout << "Starting baseline search..." << std::flush;
        auto baseline_metrics = codec.benchmarkSearch(queries, false);
        std::cout << " completed baseline search\n" << std::flush;
        
        BenchmarkResult::SearchMetrics baseline;
        baseline.test_name = "Baseline Search";
        baseline.avg_latency_us = baseline_metrics.avg_latency_us;
        baseline.p95_latency_us = baseline_metrics.p95_latency_us;
        baseline.p99_latency_us = baseline_metrics.p99_latency_us;
        baseline.throughput_qps = baseline_metrics.throughput_qps;
        baseline.total_matches = baseline_metrics.total_matches;
        results.search_results.push_back(baseline);
    }

    // SIMD search
    {
        std::cout << "Starting SIMD search..." << std::flush;
        auto simd_metrics = codec.benchmarkSearch(queries, true);
        std::cout << " completed SIMD search\n" << std::flush;
        
        BenchmarkResult::SearchMetrics simd;
        simd.test_name = "SIMD Search";
        simd.avg_latency_us = simd_metrics.avg_latency_us;
        simd.p95_latency_us = simd_metrics.p95_latency_us;
        simd.p99_latency_us = simd_metrics.p99_latency_us;
        simd.throughput_qps = simd_metrics.throughput_qps;
        simd.total_matches = simd_metrics.total_matches;
        results.search_results.push_back(simd);
    }
}
void BenchmarkSuite::runPrefixSearchBenchmark() {
    warmUp();  // Make sure to warm up before benchmarking
    
    for (size_t prefix_len : config.prefix_lengths) {
        // Generate prefixes with the specified length
        auto prefixes = generateRandomPrefixes(config.num_queries_per_test, prefix_len);
        
        if (prefixes.empty()) {
            std::cerr << "Warning: No prefixes generated for length " << prefix_len << std::endl;
            continue;
        }

        // Baseline prefix search
        {
            std::cout << "Running baseline prefix search (len=" << prefix_len << ")..." << std::endl;
            auto baseline_metrics = codec.benchmarkPrefixSearch(prefixes, false);
            if (baseline_metrics.total_queries > 0) {  // Add validation
                BenchmarkResult::SearchMetrics baseline;
                baseline.test_name = "Baseline Prefix Search (len=" + std::to_string(prefix_len) + ")";
                baseline.avg_latency_us = baseline_metrics.avg_latency_us;
                baseline.p95_latency_us = baseline_metrics.p95_latency_us;
                baseline.p99_latency_us = baseline_metrics.p99_latency_us;
                baseline.throughput_qps = baseline_metrics.throughput_qps;
                baseline.total_matches = baseline_metrics.total_matches;
                results.search_results.push_back(baseline);
            }
        }
        
        // SIMD prefix search
        {
            std::cout << "Running SIMD prefix search (len=" << prefix_len << ")..." << std::endl;
            auto simd_metrics = codec.benchmarkPrefixSearch(prefixes, true);
            if (simd_metrics.total_queries > 0) {  // Add validation
                BenchmarkResult::SearchMetrics simd;
                simd.test_name = "SIMD Prefix Search (len=" + std::to_string(prefix_len) + ")";
                simd.avg_latency_us = simd_metrics.avg_latency_us;
                simd.p95_latency_us = simd_metrics.p95_latency_us;
                simd.p99_latency_us = simd_metrics.p99_latency_us;
                simd.throughput_qps = simd_metrics.throughput_qps;
                simd.total_matches = simd_metrics.total_matches;
                results.search_results.push_back(simd);
            }
        }
    }
}

void BenchmarkSuite::runMemoryBenchmark() {
    results.peak_memory_usage_mb = measureMemoryUsage();
    results.avg_memory_usage_mb = codec.getMemoryUsage() / (1024 * 1024);
    results.compression_ratio = codec.getCompressionRatio();
}

void BenchmarkSuite::runScalabilityBenchmark() {
    // This is handled as part of the encoding benchmark
    // by testing with different thread counts
}

void BenchmarkSuite::runAllBenchmarks(const std::string& filename) {
    results.clear();
    
    std::cout << "Running encoding benchmark...\n";
    runEncodingBenchmark(filename);
    
    std::cout << "Running search benchmark...\n";
    runSearchBenchmark();
    
    std::cout << "Running prefix search benchmark...\n";
    runPrefixSearchBenchmark();
    
    std::cout << "Running memory benchmark...\n";
    runMemoryBenchmark();
    
    std::cout << "Running scalability benchmark...\n";
    runScalabilityBenchmark();
}

void BenchmarkSuite::printResults() const {
    std::cout << "\nBenchmark Results:\n";
    std::cout << std::setw(30) << "Test Name" 
              << std::setw(15) << "Avg Latency" 
              << std::setw(15) << "P95 Latency"
              << std::setw(15) << "P99 Latency"
              << std::setw(15) << "Throughput"
              << std::setw(15) << "Matches\n";
    
    for (const auto& result : results.search_results) {
        std::cout << std::setw(30) << result.test_name
                  << std::setw(15) << std::fixed << std::setprecision(2) 
                  << result.avg_latency_us
                  << std::setw(15) << result.p95_latency_us
                  << std::setw(15) << result.p99_latency_us
                  << std::setw(15) << result.throughput_qps
                  << std::setw(15) << result.total_matches << "\n";
    }
}

namespace BenchmarkUtils {
    std::string formatDuration(double microseconds) {
        if (microseconds < 1000) {
            return std::to_string(static_cast<int>(microseconds)) + "μs";
        } else if (microseconds < 1000000) {
            return std::to_string(microseconds / 1000) + "ms";
        } else {
            return std::to_string(microseconds / 1000000) + "s";
        }
    }

    std::string formatThroughput(double qps) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2);
        if (qps >= 1000000) {
            ss << qps / 1000000 << "M QPS";
        } else if (qps >= 1000) {
            ss << qps / 1000 << "K QPS";
        } else {
            ss << qps << " QPS";
        }
        return ss.str();
    }

    std::string formatMemory(size_t bytes) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2);
        if (bytes >= 1073741824) {
            ss << bytes / 1073741824.0 << " GB";
        } else if (bytes >= 1048576) {
            ss << bytes / 1048576.0 << " MB";
        } else if (bytes >= 1024) {
            ss << bytes / 1024.0 << " KB";
        } else {
            ss << bytes << " B";
        }
        return ss.str();
    }
} // namespace BenchmarkUtils
