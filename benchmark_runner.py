#Run in build folder: python3 ../../vortex-prefetching/benchmark_runner.py 2>&1 | tee benchmarks_log.txt

#Might need venv to run pip installed packages in vortex folder
import csv, subprocess, glob, time
from datetime import datetime
import pytz #ensure pytz is installed
import pandas as pd #ensure pandas is installed + openpyxl for Excel export

#PARAMETERS
clear_csv_on_run = True #delete prior csv files on new run
clear_xlsx_on_run = True #delete prior xlsx files on new run
export_to_xlsx = True #combine csv files into a single Excel file at the end of the run

#CWT configs to run
configs = ['c1w2t16', 'c1w2t32', 'c1w4t8', 'c1w4t16', 'c1w8t8', 'c2w2t16', 'c2w2t32', 'c2w4t8', 'c2w4t16', 'c2w8t8', 'c4w4t8', 'c4w4t16', 'c8w4t4'] #cores, warps, threads

#prefetching configs
prefetching_configs = ["baseline", "MT_HWP_ENABLE", "ORCHESTRATED_PREFETCH_ENABLE", "SNAKE_PREFETCH_ENABLE"]
config_setup = {'baseline': '', 'MT_HWP_ENABLE': '-DMT_HWP_ENABLE', 'ORCHESTRATED_PREFETCH_ENABLE': '-DORCHESTRATED_PREFETCH_ENABLE', 'SNAKE_PREFETCH_ENABLE': '-DSNAKE_PREFETCH_ENABLE'}

#Codebase to run on
driver = "simx" #running on simx

#running opencl test set
tests = ["oclprintf", "conv3", "nearn", "dotproduct", "blackscholes", "bfs", "sfilter", "psum", "psort", "saxpy", "guassian", "lbm", "vecadd", "sgemm", "sgemm2", "sgemm3", "spmv", "kmeans", "stencil", "transpose"]

#tracking prefetching metrics
header = ["Config", "Application", "Status", "IPC", "dcache_read_hit_%", "l2_read_hit_%", "coalescer_hit_%", "memory_read_reqs", "dcache_mshr_stalls", "l2_mshr_stalls", "memory_latency_cycles"]


#Clear prior csv files
def clear_files():
    if clear_xlsx_on_run:
        cmd = "rm -f *.xlsx"
        subprocess.run(cmd, shell=True)
    if clear_csv_on_run:
        cmd = "rm -f *.csv"
        subprocess.run(cmd, shell=True)

#Combine all csv files into a single Excel file with separate sheets for each config
def combine_csvs_to_excel(output_file="benchmark_results.xlsx"):
    csv_files = glob.glob("*.csv")
    if not csv_files:
        print("No CSV files found to combine.")
        return
    
    with pd.ExcelWriter(output_file, engine='openpyxl') as writer:
        for csv_file in sorted(csv_files):
            sheet_name = csv_file.replace('.csv', '')[:31]  # Excel sheet names max 31 chars
            df = pd.read_csv(csv_file)
            df.to_excel(writer, sheet_name=sheet_name, index=False)
    
    print(f"Combined {len(csv_files)} CSV files into {output_file}")

#Test execution command (currently set to run w/ l2cache + perf=2 along with the parameters provided)
def run_test(driver, cores, warps, threads, config, test):
    cmd = f"CONFIGS={config} ./ci/blackbox.sh --app={test} --cores={cores} --warps={warps} --threads={threads} --driver={driver} --l2cache --perf=2"
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=900)
        return result
    except subprocess.TimeoutExpired:
        # Return a fake result indicating timeout
        class TimeoutResult:
            returncode = 124 
            stdout = "TIMEOUT\n"
            stderr = "Test exceeded 15 minute timeout"
        return TimeoutResult()


def main():
    if clear_csv_on_run or clear_xlsx_on_run:
        clear_files()
    runtime = time.time()
    for config in configs:
        config_runtime = time.time()
        print(f"Starting Config: {config}\n\n")
        c = int(config.split('w')[0][1:])
        w = int(config.split('w')[1].split('t')[0])
        t = int(config.split('t')[1])
        
        with open(config+".csv", "w", newline='') as f:
            writer = csv.writer(f)
            writer.writerow(header)
            
            for pconfig in prefetching_configs:
                pconfig_runtime = time.time()
                print(f"Running Prefetching Config: {pconfig}\n")
                prefetching_config_line = [pconfig if i == 0 else "" for i in range(len(header))]
                writer.writerow(prefetching_config_line)
                
                for test in tests:
                    test_runtime = time.time()
                    curr_time = datetime.now(pytz.utc).astimezone(pytz.timezone('US/Eastern')).strftime("%Y-%m-%d %H:%M:%S")
                    print(f"({curr_time} EST) Running {test} with config {config} and prefetching config {pconfig}.\n")
                    result = run_test(driver, c, w, t, config_setup[pconfig], test)
                    print(result.stdout, end='')
                    print(f"Return code: {result.returncode}, Time Elapsed: {((time.time() - test_runtime)):.2f} s\n")

                    output = []
                    if result.returncode == 124:
                        output = ['', test, 'TIMEOUT']
                        output.extend(['N/A'] * (len(header) - len(output)))   
                    elif result.returncode != 0:
                        output = ['', test, 'ERROR']
                        output.extend(['N/A'] * (len(header) - len(output)))   
                    elif "passed" not in result.stdout.lower():
                        output = ['', test, 'PASSED (npm)']
                        output.extend(['N/A'] * (len(header) - len(output)))      
                    else:
                        output = ['', test, 'PASSED']
                        lines = result.stdout.splitlines()
                        
                        ipc = "N/A"
                        dcache_read_misses = 0
                        dcache_reads = 0
                        dcache_mshr_stalls = 0
                        l2_hit_percent = "N/A"
                        l2_mshr_stalls = "N/A"
                        coalescer_miss_list = []
                        mem_read_reqs = "N/A"
                        #mem_bank_stalls = "N/A"
                        #mem_bank_util = "N/A"
                        mem_latency = "N/A"
                        for line in lines:
                            if "PERF: instrs" in line:
                                ipc = float(line.split("IPC=")[1].strip())
                            elif "dcache read misses" in line:
                                dcache_read_misses += float(line.split()[4].split('=')[1])
                            elif "dcache reads" in line:
                                dcache_reads += float(line.split()[3].split('=')[1])
                            elif "dcache mshr stalls" in line:
                                dcache_mshr_stalls += int(line.split()[4].split('=')[1])
                            elif "l2cache read misses" in line:
                                l2_hit_percent = int(line.split("(hit ratio=")[1][:-2])
                            elif "l2cache mshr stalls" in line:
                                l2_mshr_stalls = int(line.split()[3].split('=')[1])
                            elif "coalescer misses" in line:
                                coalescer_miss_list.append((float(line.split()[3].split('=')[1]), float(line.split("(hit ratio=")[1][:-2])))
                            elif "memory requests" in line:
                                mem_read_reqs = int(line.split("reads=")[1].split(",")[0])
                            # elif "memory bank stalls" in line:
                            #     mem_bank_stalls = int(line.split()[3].split('=')[1])
                            #     mem_bank_util = int(line.split('utilization=')[1][:-2])
                            elif "memory latency" in line:
                                mem_latency = int(line.split()[2].split('=')[1])

                        output.append(ipc)
                        dcache_hit_percent = round(((dcache_reads - dcache_read_misses) / dcache_reads) * 100) if dcache_reads > 0 else "N/A"
                        output.append(dcache_hit_percent)
                        output.append(l2_hit_percent)
                        coalescer_hit_perecent = 100 - round(100 * sum([i for i, j in coalescer_miss_list])/sum([i / ((100 - j) / 100)for i, j in coalescer_miss_list])) if len(coalescer_miss_list) > 0 else "N/A"
                        output.append(coalescer_hit_perecent if coalescer_hit_perecent != "N/A" and 0 <= coalescer_hit_perecent <= 100 else "N/A")
                        output.append(mem_read_reqs)
                        # output.append(mem_bank_stalls)
                        if ipc == "N/A":
                            output.append("N/A")
                        else:
                            output.append(dcache_mshr_stalls)
                        output.append(l2_mshr_stalls)
                        # output.append(mem_bank_util)
                        output.append(mem_latency)
                    
                    writer.writerow(output)
            
                writer.writerow([''] * len(header))
                print(f"Finished Prefetching Config: {pconfig}, Time Elapsed: {(time.time() - pconfig_runtime):.2f} s\n")
        print(f"Finished Config: {config}, Time Elapsed: {(time.time() - config_runtime):.2f} s\n\n")
    print(f"All tests completed, Total Time Elapsed: {(time.time() - runtime):.2f} s\n")
    if export_to_xlsx:
        combine_csvs_to_excel()

if __name__ == "__main__":
    main()
