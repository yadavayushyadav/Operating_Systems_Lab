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
    vector<Segment> schedule;
    vector<int> completion;
    int total_time = 0;
    int cpu_busy = 0;
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

void print_header(int num_processes, const string& scheduling_algorithm) {
    cout << "\n\nScheduling Algorithm: " << scheduling_algorithm << endl;
    cout << "Number of Processes: " << num_processes << endl;
    cout << "----------------------------------------" << endl;
    cout << "Time\t";
    for (int i = 1; i <= num_processes; ++i) {
        cout << "P" << i << "\t";
    }
    cout << "Ready Queue\t" << endl;
}

void print_tick(int current_time, const vector<Process>& processes, int running_pid, const vector<int>& io_queue, const deque<int>& ready_queue) {
    cout << current_time << "\t";
    for (const Process& p : processes) {
        string status;
        if (p.current_burst_index >= (int)p.bursts.size()) status = "DONE";
        else if (p.arrival_time > current_time) status = "-";
        else if (p.pid == running_pid) status = "RUN";
        else if (find(io_queue.begin(), io_queue.end(), p.pid) != io_queue.end()) status = "I/O";
        else status = "RDY";
        cout << status << "\t";
    }
    for (int pid : ready_queue) cout << "P" << pid << " ";
    cout << endl;
}

void print_schedule(const SimResult& result) {
    cout << "CPU0" << endl;
    for (const Segment& s : result.schedule) {
        cout << "P" << s.pid << "," << s.burst_number << "\t" << s.start << "\t" << s.end << endl;
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
    cout << endl;
    cout << "Average Turnaround Time: " << fixed << setprecision(2)
         << (counted ? turnaround_sum / counted : 0.0) << endl;
    cout << "Maximum Turnaround Time: " << turnaround_max << endl;
    cout << "Run Time (not counting I/O): " << result.cpu_busy << endl;
    cout << "Total Simulation Time: " << result.total_time << endl;
}

SimResult FIFO(vector<Process> processes, bool trace) {
    int num_processes = processes.size();
    SimResult result;
    result.completion.assign(num_processes, 0);
    if (trace) print_header(num_processes, "FIFO Scheduling");

    int completed_processes = 0;
    int current_time = 0;

    deque<int> ready_queue;
    vector<int> io_queue;
    int running_pid = -1;

    for (auto& p : processes) {
        if (p.bursts.empty()) completed_processes++;
    }

    while (completed_processes < num_processes) {
        for (int i = 0; i < num_processes; ++i) {
            if (processes[i].arrival_time == current_time && !processes[i].bursts.empty()) {
                ready_queue.push_back(processes[i].pid);
            }
        }

        if (running_pid == -1 && !ready_queue.empty()) {
            running_pid = ready_queue.front();
            ready_queue.pop_front();
        }

        if (running_pid != -1) {
            Process& running_process = processes[running_pid - 1];
            running_process.remaining_burst_time--;
            record_segment(result.schedule, running_pid, cpu_burst_number(running_process), current_time);
            result.cpu_busy++;
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

        if (running_pid != -1) {
            Process& running_process = processes[running_pid - 1];
            if (running_process.remaining_burst_time == 0) {
                running_process.current_burst_index++;
                if (running_process.current_burst_index < (int)running_process.bursts.size()) {
                    running_process.remaining_burst_time = running_process.bursts[running_process.current_burst_index].duration;
                    io_queue.push_back(running_pid);
                } else {
                    completed_processes++;
                    result.completion[running_pid - 1] = current_time;
                }
                running_pid = -1;
            }
        }
    }

    result.total_time = current_time;
    return result;
}

SimResult RoundRobbin(vector<Process> processes, int time_quantum, bool trace) {
    int num_processes = processes.size();
    SimResult result;
    result.completion.assign(num_processes, 0);
    if (trace) print_header(num_processes, "Round Robin Scheduling");

    int completed_processes = 0;
    int current_time = 0;
    int process_time_slice = 0;

    deque<int> ready_queue;
    vector<int> io_queue;
    int running_pid = -1;

    for (auto& p : processes) {
        if (p.bursts.empty()) completed_processes++;
    }

    while (completed_processes < num_processes) {
        for (int i = 0; i < num_processes; ++i) {
            if (processes[i].arrival_time == current_time && !processes[i].bursts.empty()) {
                ready_queue.push_back(processes[i].pid);
            }
        }

        if (running_pid == -1 && !ready_queue.empty()) {
            running_pid = ready_queue.front();
            ready_queue.pop_front();
            process_time_slice = 0;
        }

        if (running_pid != -1) {
            Process& running_process = processes[running_pid - 1];
            running_process.remaining_burst_time--;
            process_time_slice++;
            record_segment(result.schedule, running_pid, cpu_burst_number(running_process), current_time);
            result.cpu_busy++;
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

        if (running_pid != -1) {
            Process& running_process = processes[running_pid - 1];
            if (running_process.remaining_burst_time == 0) {
                running_process.current_burst_index++;
                if (running_process.current_burst_index < (int)running_process.bursts.size()) {
                    running_process.remaining_burst_time = running_process.bursts[running_process.current_burst_index].duration;
                    io_queue.push_back(running_pid);
                } else {
                    completed_processes++;
                    result.completion[running_pid - 1] = current_time;
                }
                running_pid = -1;
            }
        }

        if (process_time_slice == time_quantum) {
            if (running_pid != -1) {
                ready_queue.push_back(running_pid);
                running_pid = -1;
            }
            process_time_slice = 0;
        }
    }

    result.total_time = current_time;
    return result;
}

SimResult MLFQ(vector<Process> processes, int boost_interval, bool trace) {
    int num_processes = processes.size();
    SimResult result;
    result.completion.assign(num_processes, 0);
    if (trace) print_header(num_processes, boost_interval > 0 ? "MLFQ Scheduling (with boost)" : "MLFQ Scheduling (no boost)");

    int completed_processes = 0;
    int current_time = 0;
    int time_quantum = 2;

    deque<int> Q0, Q1, Q2;
    vector<int> io_queue;
    vector<int> current_level(num_processes, 0);

    int running_pid = -1;
    int running_level = -1;
    int process_time_slice = 0;

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

        if (running_pid == -1) {
            if (!Q0.empty()) { running_pid = Q0.front(); Q0.pop_front(); running_level = 0; }
            else if (!Q1.empty()) { running_pid = Q1.front(); Q1.pop_front(); running_level = 1; }
            else if (!Q2.empty()) { running_pid = Q2.front(); Q2.pop_front(); running_level = 2; }
            process_time_slice = 0;
        }

        if (running_pid != -1) {
            Process& running_process = processes[running_pid - 1];
            running_process.remaining_burst_time--;
            process_time_slice++;
            record_segment(result.schedule, running_pid, cpu_burst_number(running_process), current_time);
            result.cpu_busy++;
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

        if (running_pid != -1) {
            Process& running_process = processes[running_pid - 1];
            if (running_process.remaining_burst_time == 0) {
                running_process.current_burst_index++;
                if (running_process.current_burst_index < (int)running_process.bursts.size()) {
                    running_process.remaining_burst_time = running_process.bursts[running_process.current_burst_index].duration;
                    io_queue.push_back(running_pid);
                } else {
                    completed_processes++;
                    result.completion[running_pid - 1] = current_time;
                }
                running_pid = -1;
            }
        }

        if (process_time_slice == time_quantum) {
            if (running_pid != -1) {
                int new_level = min(running_level + 1, 2);
                current_level[running_pid - 1] = new_level;
                if (new_level == 1) Q1.push_back(running_pid);
                else Q2.push_back(running_pid);
                running_pid = -1;
            }
            process_time_slice = 0;
        }

        if (boost_interval > 0 && current_time % boost_interval == 0) {
            if (running_pid != -1) {
                Q0.push_back(running_pid);
                current_level[running_pid - 1] = 0;
                running_pid = -1;
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
