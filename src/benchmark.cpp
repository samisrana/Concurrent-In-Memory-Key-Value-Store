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
        return prefixes;
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, original_data.size() - 1);
    
    for (size_t i = 0; i < count; i++) {
        std::string prefix = original_data[dis(gen)];
        if (prefix.length() > length) {
            prefix = prefix.substr(0, length);
        }
        prefixes.push_back(prefix);
    }
    
    return prefixes;
}

void BenchmarkSuite::warmUp() const {
    auto warm_up_queries = generateQueries(config.num_warm_up_queries);
    for (const auto& query : warm_up_queries) {
        codec.findMatchesSIMD(query);
    }
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
    warmUp();
    
    auto queries = generateQueries(config.num_queries_per_test);
    
    // Baseline search
    {
        auto baseline_metrics = codec.benchmarkSearch(queries, false);
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
        auto simd_metrics = codec.benchmarkSearch(queries, true);
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
    for (size_t prefix_len : config.prefix_lengths) {
        auto prefixes = generateRandomPrefixes(config.num_queries_per_test, prefix_len);
        
        // Baseline prefix search
        {
            auto baseline_metrics = codec.benchmarkPrefixSearch(prefixes, false);
            BenchmarkResult::SearchMetrics baseline;
            baseline.test_name = "Baseline Prefix Search (len=" + std::to_string(prefix_len) + ")";
            baseline.avg_latency_us = baseline_metrics.avg_latency_us;
            baseline.p95_latency_us = baseline_metrics.p95_latency_us;
            baseline.p99_latency_us = baseline_metrics.p99_latency_us;
            baseline.throughput_qps = baseline_metrics.throughput_qps;
            baseline.total_matches = baseline_metrics.total_matches;
            results.search_results.push_back(baseline);
        }
        
        // SIMD prefix search
        {
            auto simd_metrics = codec.benchmarkPrefixSearch(prefixes, true);
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
