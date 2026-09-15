#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <queue>
#include <algorithm>
#include <iomanip>
#include <cctype>
#include <cstdlib>

using namespace std;

const int NUM_CPUS = 2;

enum class BurstType {
    CPU,
    IO
};

struct Burst {
    BurstType type;
    int duration;
};

struct Process {
    int pid;
    int arrival_time;
    vector<Burst> bursts;
    int current_burst_index = 0;
    int remaining_burst_time = -1;
};

struct Segment {
    int pid;
    int burst_number;
    int start;
    int end;
};

struct SimResult {
    vector<vector<Segment>> schedule;
    vector<int> completion;
    vector<int> cpu_busy;
    int total_time = 0;
};

vector<Process> file_to_processes(const string& path) {
    ifstream file(path);
    if (!file.is_open()) {
        cerr << "Error opening file: " << path << endl;
        return vector<Process>();
    }
    vector<Process> processes;
    string line;
    while (getline(file, line)) {
        Process process;
        process.pid = processes.size() + 1;
        istringstream iss(line);
        int arrival_time;
        if (!(iss >> arrival_time)) continue;
        process.arrival_time = arrival_time;
        int cpu_burst;
        int io_burst;
        while (iss >> cpu_burst) {
            if (cpu_burst == -1) break;
            process.bursts.push_back({BurstType::CPU, cpu_burst});
            if (iss >> io_burst) {
                if (io_burst == -1) break;
                process.bursts.push_back({BurstType::IO, io_burst});
            }
        }
        process.remaining_burst_time = process.bursts.empty() ? -1 : process.bursts[0].duration;
        processes.push_back(process);
    }
    file.close();
    return processes;
}

int cpu_burst_number(const Process& p) {
    return p.current_burst_index / 2 + 1;
}

void record_segment(vector<Segment>& schedule, int pid, int burst_number, int current_time) {
    if (!schedule.empty()) {
        Segment& last = schedule.back();
        if (last.pid == pid && last.burst_number == burst_number && last.end == current_time) {
            last.end = current_time + 1;
            return;
        }
    }
    schedule.push_back({pid, burst_number, current_time + 1, current_time + 1});
}

SimResult new_result(int num_processes) {
    SimResult result;
    result.schedule.assign(NUM_CPUS, vector<Segment>());
    result.completion.assign(num_processes, 0);
    result.cpu_busy.assign(NUM_CPUS, 0);
    return result;
}

void print_header(int num_processes, const string& scheduling_algorithm) {
    cout << "\n\nScheduling Algorithm: " << scheduling_algorithm << endl;
    cout << "Number of Processes: " << num_processes << endl;
    cout << "Number of CPUs: " << NUM_CPUS << endl;
    cout << "----------------------------------------" << endl;
    cout << "Time\t";
    for (int i = 1; i <= num_processes; ++i) {
        cout << "P" << i << "\t";
    }
    cout << "Ready Queue\t" << endl;
}

void print_tick(int current_time, const vector<Process>& processes, const vector<int>& running_pid, const vector<int>& io_queue, const deque<int>& ready_queue) {
    cout << current_time << "\t";
    for (const Process& p : processes) {
        string status;
        int cpu = find(running_pid.begin(), running_pid.end(), p.pid) - running_pid.begin();
        if (p.current_burst_index >= (int)p.bursts.size()) status = "DONE";
        else if (p.arrival_time > current_time) status = "-";
        else if (cpu < NUM_CPUS) status = "RUN" + to_string(cpu);
        else if (find(io_queue.begin(), io_queue.end(), p.pid) != io_queue.end()) status = "I/O";
        else status = "RDY";
        cout << status << "\t";
    }
    for (int pid : ready_queue) cout << "P" << pid << " ";
    cout << endl;
}

void print_schedule(const SimResult& result) {
    for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
        cout << "CPU" << cpu << endl;
        for (const Segment& s : result.schedule[cpu]) {
            cout << "P" << s.pid << "," << s.burst_number << "\t" << s.start << "\t" << s.end << endl;
        }
    }
}

void print_metrics(const SimResult& result, const vector<Process>& processes) {
    double turnaround_sum = 0;
    int turnaround_max = 0;
    int counted = 0;
    for (size_t i = 0; i < processes.size(); ++i) {
        if (processes[i].bursts.empty()) continue;
        int turnaround = result.completion[i] - processes[i].arrival_time;
        turnaround_sum += turnaround;
        turnaround_max = max(turnaround_max, turnaround);
        counted++;
    }
    int total_busy = 0;
    for (int busy : result.cpu_busy) total_busy += busy;

    cout << endl;
    cout << "Average Turnaround Time: " << fixed << setprecision(2)
         << (counted ? turnaround_sum / counted : 0.0) << endl;
    cout << "Maximum Turnaround Time: " << turnaround_max << endl;
    cout << "Run Time (not counting I/O): " << total_busy << endl;
    for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
        cout << "Run Time CPU" << cpu << ": " << result.cpu_busy[cpu] << endl;
    }
    cout << "Total Simulation Time: " << result.total_time << endl;
}

SimResult FIFO(vector<Process> processes, bool trace) {
    int num_processes = processes.size();
    SimResult result = new_result(num_processes);
    if (trace) print_header(num_processes, "FIFO Scheduling");

    int completed_processes = 0;
    int current_time = 0;

    deque<int> ready_queue;
    vector<int> io_queue;
    vector<int> running_pid(NUM_CPUS, -1);

    for (auto& p : processes) {
        if (p.bursts.empty()) completed_processes++;
    }

    while (completed_processes < num_processes) {
        for (int i = 0; i < num_processes; ++i) {
            if (processes[i].arrival_time == current_time && !processes[i].bursts.empty()) {
                ready_queue.push_back(processes[i].pid);
            }
        }

        for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
            if (running_pid[cpu] == -1 && !ready_queue.empty()) {
                running_pid[cpu] = ready_queue.front();
                ready_queue.pop_front();
            }
        }

        for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
            if (running_pid[cpu] != -1) {
                Process& running_process = processes[running_pid[cpu] - 1];
                running_process.remaining_burst_time--;
                record_segment(result.schedule[cpu], running_pid[cpu], cpu_burst_number(running_process), current_time);
                result.cpu_busy[cpu]++;
            }
        }
        for (int pid : io_queue) processes[pid - 1].remaining_burst_time--;

        if (trace) print_tick(current_time, processes, running_pid, io_queue, ready_queue);

        current_time++;

        vector<int> temp;
        for (auto pid : io_queue) {
            Process& io_process = processes[pid - 1];
            if (io_process.remaining_burst_time == 0) {
                io_process.current_burst_index++;
                if (io_process.current_burst_index < (int)io_process.bursts.size()) {
                    io_process.remaining_burst_time = io_process.bursts[io_process.current_burst_index].duration;
                    ready_queue.push_back(pid);
                } else {
                    completed_processes++;
                    result.completion[pid - 1] = current_time;
                }
            } else temp.push_back(pid);
        }
        io_queue = temp;

        for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
            if (running_pid[cpu] != -1) {
                Process& running_process = processes[running_pid[cpu] - 1];
                if (running_process.remaining_burst_time == 0) {
                    running_process.current_burst_index++;
                    if (running_process.current_burst_index < (int)running_process.bursts.size()) {
                        running_process.remaining_burst_time = running_process.bursts[running_process.current_burst_index].duration;
                        io_queue.push_back(running_pid[cpu]);
                    } else {
                        completed_processes++;
                        result.completion[running_pid[cpu] - 1] = current_time;
                    }
                    running_pid[cpu] = -1;
                }
            }
        }
    }

    result.total_time = current_time;
    return result;
}

SimResult RoundRobbin(vector<Process> processes, int time_quantum, bool trace) {
    int num_processes = processes.size();
    SimResult result = new_result(num_processes);
    if (trace) print_header(num_processes, "Round Robin Scheduling");

    int completed_processes = 0;
    int current_time = 0;

    deque<int> ready_queue;
    vector<int> io_queue;
    vector<int> running_pid(NUM_CPUS, -1);
    vector<int> process_time_slice(NUM_CPUS, 0);

    for (auto& p : processes) {
        if (p.bursts.empty()) completed_processes++;
    }

    while (completed_processes < num_processes) {
        for (int i = 0; i < num_processes; ++i) {
            if (processes[i].arrival_time == current_time && !processes[i].bursts.empty()) {
                ready_queue.push_back(processes[i].pid);
            }
        }

        for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
            if (running_pid[cpu] == -1 && !ready_queue.empty()) {
                running_pid[cpu] = ready_queue.front();
                ready_queue.pop_front();
                process_time_slice[cpu] = 0;
            }
        }

        for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
            if (running_pid[cpu] != -1) {
                Process& running_process = processes[running_pid[cpu] - 1];
                running_process.remaining_burst_time--;
                process_time_slice[cpu]++;
                record_segment(result.schedule[cpu], running_pid[cpu], cpu_burst_number(running_process), current_time);
                result.cpu_busy[cpu]++;
            }
        }
        for (int pid : io_queue) processes[pid - 1].remaining_burst_time--;

        if (trace) print_tick(current_time, processes, running_pid, io_queue, ready_queue);

        current_time++;

        vector<int> temp;
        for (auto pid : io_queue) {
            Process& io_process = processes[pid - 1];
            if (io_process.remaining_burst_time == 0) {
                io_process.current_burst_index++;
                if (io_process.current_burst_index < (int)io_process.bursts.size()) {
                    io_process.remaining_burst_time = io_process.bursts[io_process.current_burst_index].duration;
                    ready_queue.push_back(pid);
                } else {
                    completed_processes++;
                    result.completion[pid - 1] = current_time;
                }
            } else temp.push_back(pid);
        }
        io_queue = temp;

        for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
            if (running_pid[cpu] != -1) {
                Process& running_process = processes[running_pid[cpu] - 1];
                if (running_process.remaining_burst_time == 0) {
                    running_process.current_burst_index++;
                    if (running_process.current_burst_index < (int)running_process.bursts.size()) {
                        running_process.remaining_burst_time = running_process.bursts[running_process.current_burst_index].duration;
                        io_queue.push_back(running_pid[cpu]);
                    } else {
                        completed_processes++;
                        result.completion[running_pid[cpu] - 1] = current_time;
                    }
                    running_pid[cpu] = -1;
                }
            }

            if (process_time_slice[cpu] == time_quantum) {
                if (running_pid[cpu] != -1) {
                    ready_queue.push_back(running_pid[cpu]);
                    running_pid[cpu] = -1;
                }
                process_time_slice[cpu] = 0;
            }
        }
    }

    result.total_time = current_time;
    return result;
}

SimResult MLFQ(vector<Process> processes, int boost_interval, bool trace) {
    int num_processes = processes.size();
    SimResult result = new_result(num_processes);
    if (trace) print_header(num_processes, boost_interval > 0 ? "MLFQ Scheduling (with boost)" : "MLFQ Scheduling (no boost)");

    int completed_processes = 0;
    int current_time = 0;
    int time_quantum = 2;

    deque<int> Q0, Q1, Q2;
    vector<int> io_queue;
    vector<int> current_level(num_processes, 0);

    vector<int> running_pid(NUM_CPUS, -1);
    vector<int> running_level(NUM_CPUS, -1);
    vector<int> process_time_slice(NUM_CPUS, 0);

    for (auto& p : processes) {
        if (p.bursts.empty()) completed_processes++;
    }

    while (completed_processes < num_processes) {
        for (int i = 0; i < num_processes; ++i) {
            if (processes[i].arrival_time == current_time && !processes[i].bursts.empty()) {
                Q0.push_back(processes[i].pid);
                current_level[i] = 0;
            }
        }

        for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
            if (running_pid[cpu] == -1) {
                if (!Q0.empty()) { running_pid[cpu] = Q0.front(); Q0.pop_front(); running_level[cpu] = 0; }
                else if (!Q1.empty()) { running_pid[cpu] = Q1.front(); Q1.pop_front(); running_level[cpu] = 1; }
                else if (!Q2.empty()) { running_pid[cpu] = Q2.front(); Q2.pop_front(); running_level[cpu] = 2; }
                process_time_slice[cpu] = 0;
            }
        }

        for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
            if (running_pid[cpu] != -1) {
                Process& running_process = processes[running_pid[cpu] - 1];
                running_process.remaining_burst_time--;
                process_time_slice[cpu]++;
                record_segment(result.schedule[cpu], running_pid[cpu], cpu_burst_number(running_process), current_time);
                result.cpu_busy[cpu]++;
            }
        }
        for (int pid : io_queue) processes[pid - 1].remaining_burst_time--;

        if (trace) {
            deque<int> ready_queue_display;
            for (int pid : Q0) ready_queue_display.push_back(pid);
            for (int pid : Q1) ready_queue_display.push_back(pid);
            for (int pid : Q2) ready_queue_display.push_back(pid);
            print_tick(current_time, processes, running_pid, io_queue, ready_queue_display);
        }

        current_time++;

        vector<int> temp;
        for (auto pid : io_queue) {
            Process& io_process = processes[pid - 1];
            if (io_process.remaining_burst_time == 0) {
                io_process.current_burst_index++;
                if (io_process.current_burst_index < (int)io_process.bursts.size()) {
                    io_process.remaining_burst_time = io_process.bursts[io_process.current_burst_index].duration;
                    int lvl = current_level[pid - 1];
                    if (lvl == 0) Q0.push_back(pid);
                    else if (lvl == 1) Q1.push_back(pid);
                    else Q2.push_back(pid);
                } else {
                    completed_processes++;
                    result.completion[pid - 1] = current_time;
                }
            } else temp.push_back(pid);
        }
        io_queue = temp;

        for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
            if (running_pid[cpu] != -1) {
                Process& running_process = processes[running_pid[cpu] - 1];
                if (running_process.remaining_burst_time == 0) {
                    running_process.current_burst_index++;
                    if (running_process.current_burst_index < (int)running_process.bursts.size()) {
                        running_process.remaining_burst_time = running_process.bursts[running_process.current_burst_index].duration;
                        io_queue.push_back(running_pid[cpu]);
                    } else {
                        completed_processes++;
                        result.completion[running_pid[cpu] - 1] = current_time;
                    }
                    running_pid[cpu] = -1;
                }
            }

            if (process_time_slice[cpu] == time_quantum) {
                if (running_pid[cpu] != -1) {
                    int new_level = min(running_level[cpu] + 1, 2);
                    current_level[running_pid[cpu] - 1] = new_level;
                    if (new_level == 1) Q1.push_back(running_pid[cpu]);
                    else Q2.push_back(running_pid[cpu]);
                    running_pid[cpu] = -1;
                }
                process_time_slice[cpu] = 0;
            }
        }

        if (boost_interval > 0 && current_time % boost_interval == 0) {
            for (int cpu = 0; cpu < NUM_CPUS; ++cpu) {
                if (running_pid[cpu] != -1) {
                    Q0.push_back(running_pid[cpu]);
                    current_level[running_pid[cpu] - 1] = 0;
                    running_pid[cpu] = -1;
                }
            }
            for (int pid : Q1) { Q0.push_back(pid); current_level[pid - 1] = 0; }
            Q1.clear();
            for (int pid : Q2) { Q0.push_back(pid); current_level[pid - 1] = 0; }
            Q2.clear();
            for (int pid : io_queue) current_level[pid - 1] = 0;
        }
    }

    result.total_time = current_time;
    return result;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <scheduling-algorithm> <path-to-workload-description-file> [option] [--trace]" << endl;
        cerr << "Algorithms: FIFO | RR | MLFQ" << endl;
        cerr << "  RR   [option] = time quantum (default 2)" << endl;
        cerr << "  MLFQ [option] = priority boost interval, 0 disables the boost (default 0)" << endl;
        return 1;
    }

    string scheduling_algorithm = argv[1];
    string workload_path = argv[2];
    int option = -1;
    bool trace = false;

    for (int i = 3; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--trace") trace = true;
        else option = atoi(argv[i]);
    }

    for (char& c : scheduling_algorithm) c = toupper((unsigned char)c);

    vector<Process> processes = file_to_processes(workload_path);
    if (processes.empty()) {
        cerr << "No processes found in the file." << endl;
        return 1;
    }

    SimResult result;
    if (scheduling_algorithm == "FIFO") {
        result = FIFO(processes, trace);
    } else if (scheduling_algorithm == "RR") {
        int time_quantum = (option >= 0) ? option : 2;
        if (time_quantum < 1) {
            cerr << "Time quantum must be at least 1." << endl;
            return 1;
        }
        result = RoundRobbin(processes, time_quantum, trace);
    } else if (scheduling_algorithm == "MLFQ") {
        int boost_interval = (option >= 0) ? option : 0;
        result = MLFQ(processes, boost_interval, trace);
    } else {
        cerr << "Unknown scheduling algorithm: " << scheduling_algorithm << endl;
        return 1;
    }

    print_schedule(result);
    print_metrics(result, processes);

    return 0;
}
