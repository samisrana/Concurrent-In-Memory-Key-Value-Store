import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
from pathlib import Path
import sys

def plot_encoding_results(df, output_dir):
    plt.figure(figsize=(10, 6))
    sns.set_style("whitegrid")
    sns.lineplot(data=df, x='Threads', y='Throughput_MBps', marker='o', linewidth=2, markersize=8)
    plt.title('Encoding Performance vs Thread Count', fontsize=14, pad=20)
    plt.xlabel('Number of Threads', fontsize=12)
    plt.ylabel('Throughput (MB/s)', fontsize=12)
    plt.xticks(df['Threads'])
    plt.ylim(bottom=0)
    plt.tight_layout()
    plt.savefig(output_dir / 'encoding_performance.png', dpi=300, bbox_inches='tight')
    plt.close()

def plot_search_results(df, output_dir):
    plt.figure(figsize=(10, 6))
    sns.set_style("whitegrid")
    colors = ['#2E86C1', '#28B463', '#E67E22']
    ax = sns.barplot(data=df, x='Method', y='AvgLatency_us', palette=colors)
    
    # Add value labels on top of bars
    for i in ax.containers:
        ax.bar_label(i, fmt='%.1f μs', padding=3)
    
    plt.title('Search Method Performance Comparison', fontsize=14, pad=20)
    plt.xlabel('Search Method', fontsize=12)
    plt.ylabel('Average Latency (μs)', fontsize=12)
    plt.xticks(rotation=0)
    plt.tight_layout()
    plt.savefig(output_dir / 'search_performance.png', dpi=300, bbox_inches='tight')
    plt.close()

def plot_prefix_results(df, output_dir):
    plt.figure(figsize=(10, 6))
    sns.set_style("whitegrid")
    
    # Create line plot with different styles for each method
    for method, color, marker in zip(['Vanilla', 'SIMD'], ['#2E86C1', '#E67E22'], ['o', 's']):
        method_data = df[df['Method'] == method]
        plt.plot(method_data['PrefixLength'], method_data['AvgLatency_us'],
                marker=marker, label=method, color=color, linewidth=2, markersize=8)
    
    plt.title('Prefix Search Performance', fontsize=14, pad=20)
    plt.xlabel('Prefix Length', fontsize=12)
    plt.ylabel('Average Latency (μs)', fontsize=12)
    plt.legend(fontsize=10, loc='upper right')
    plt.xticks(df['PrefixLength'].unique())
    
    # Format y-axis to be more readable
    plt.gca().yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x/1e6:.1f}M' if x >= 1e6 
                                                         else f'{x/1e3:.1f}K' if x >= 1e3 
                                                         else f'{x:.1f}'))
    
    plt.tight_layout()
    plt.savefig(output_dir / 'prefix_performance.png', dpi=300, bbox_inches='tight')
    plt.close()

def main():
    if len(sys.argv) != 2:
        print("Usage: python plot_results.py <results_directory>")
        return

    # Use the exact directory name from command line argument
    results_dir = Path(sys.argv[1])
    if not results_dir.exists():
        print(f"Results directory {results_dir} does not exist!")
        return

    # Read the CSV files
    encoding_df = pd.read_csv(results_dir / 'encoding_results.csv')
    search_df = pd.read_csv(results_dir / 'search_results.csv')
    prefix_df = pd.read_csv(results_dir / 'prefix_results.csv')

    # Set the style
    plt.style.use('seaborn')
    sns.set_palette("husl")

    # Create plots
    plot_encoding_results(encoding_df, results_dir)
    plot_search_results(search_df, results_dir)
    plot_prefix_results(prefix_df, results_dir)

    print("Plots have been generated with improved styling in:", results_dir)

if __name__ == "__main__":
    main()
