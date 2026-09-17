// scheduler.cpp -- Process scheduling simulator (Laboratory 3)
//
// Usage:
//   ./scheduler <algorithm> <workload-file> [options]
//
// Algorithms : fifo | rr | mlfq
// Options    : --quantum <q>    time quantum for rr / mlfq        (default 2)
//              --cpus <n>       number of processors              (default 1)
//              --boost <b>      MLFQ priority boost period, 0=off (default 0)
//              --levels <l>     number of MLFQ queues             (default 3)
//              --base <b>       time origin used when printing    (default 0)
//              --schedule <f>   write the schedule to file f
//              --no-schedule    do not emit the schedule at all
//              --per-process    print a per-process summary table
//              --csv            print one CSV record instead of a human report
//
// Output     : Turnaround time (average and maximum), simulator run time
//              (simulation loop only, excluding file I/O), and the schedule
//              in the format of schedule_format.txt.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <list>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using i64 = long long;
constexpr i64 INF = std::numeric_limits<i64>::max() / 4;

enum class Algo { FIFO, RR, MLFQ };

struct Config {
    Algo algo = Algo::FIFO;
    std::string algoName = "fifo";
    std::string workload;
    i64 quantum = 2;
    int ncpus = 1;
    i64 boost = 0;   // 0 disables the periodic priority boost
    int levels = 3;
    i64 base = 0;
    bool printSchedule = true;
    bool perProcess = false;
    bool csv = false;
    std::string scheduleFile;
};

// ---------------------------------------------------------------------------
// Process description.
//
// bursts[] holds the alternating burst sequence exactly as read from the
// workload file: bursts[0], bursts[2], bursts[4] ... are CPU bursts and
// bursts[1], bursts[3] ... are I/O bursts.  Everything below `arrival` is
// runtime state that the simulator mutates.
// ---------------------------------------------------------------------------
struct Process {
    int pid = 0;
    i64 arrival = 0;
    std::vector<i64> bursts;

    std::size_t bi = 0;    // index of the burst currently being served
    i64 rem = 0;           // time left in the current CPU burst
    int cpuBurstNo = 0;    // 1-based ordinal of the current CPU burst
    i64 first = -1;        // first time this process reached a CPU
    i64 finish = -1;       // completion time
    i64 cpuTotal = 0;      // total CPU demand (for reference)

    int level = 0;         // MLFQ queue index
    i64 used = 0;          // allotment consumed at the current MLFQ level
    i64 gen = 0;           // boost generation this level/allotment belongs to
};

// One contiguous slice of CPU time.  [start, end) internally; printed as an
// inclusive tick range to match schedule_format.txt.
struct Segment {
    int pid;
    int burstNo;
    i64 start;
    i64 end;
};

struct Stats {
    int nproc = 0;
    double avgTat = 0, maxTat = 0, avgResp = 0, avgWait = 0;
    i64 makespan = 0;
    i64 switches = 0;
    double util = 0;
    double simMs = 0;
};

// ---------------------------------------------------------------------------
// Workload parsing
// ---------------------------------------------------------------------------
bool parseWorkload(const std::string &path, std::vector<Process> &out) {
    std::ifstream in(path);
    if (!in) return false;

    std::string line;
    int pid = 1;
    int clamped = 0;
    while (std::getline(in, line)) {
        if (auto h = line.find('#'); h != std::string::npos) line.erase(h);
        std::istringstream ss(line);
        std::vector<i64> nums;
        i64 v;
        while (ss >> v) {
            if (v == -1) break;      // sentinel terminating the row
            nums.push_back(v);
        }
        if (nums.size() < 2) continue;          // blank line or no CPU burst

        Process p;
        p.pid = pid++;
        p.arrival = nums[0];
        p.bursts.assign(nums.begin() + 1, nums.end());
        // A trailing I/O burst has no meaning: a process ends on a CPU burst.
        if (p.bursts.size() % 2 == 0) p.bursts.pop_back();
        for (auto &b : p.bursts)
            if (b <= 0) { b = 1; ++clamped; }    // keep the simulation well founded
        for (std::size_t i = 0; i < p.bursts.size(); i += 2) p.cpuTotal += p.bursts[i];
        out.push_back(std::move(p));
    }
    if (clamped)
        std::cerr << "warning: " << clamped
                  << " non-positive burst(s) clamped to 1\n";
    return true;
}

// ---------------------------------------------------------------------------
// The simulator itself.
//
// This is an event-driven simulation rather than a tick-by-tick loop: at every
// step the clock jumps forward by the largest interval during which no
// scheduling decision can possibly change (the minimum over remaining CPU
// bursts, remaining quanta, the next arrival, the next I/O completion and the
// next priority boost).  The schedule produced is identical to that of a
// per-tick loop, but the cost is O(events) instead of O(makespan).
// ---------------------------------------------------------------------------
class Simulator {
public:
    Simulator(Config cfg, std::vector<Process> procs)
        : cfg_(std::move(cfg)), P_(std::move(procs)) {}

    void run() {
        const int n = static_cast<int>(P_.size());
        const int m = cfg_.ncpus;
        const int nq = (cfg_.algo == Algo::MLFQ) ? cfg_.levels : 1;

        rq_.assign(nq, {});
        run_.assign(m, -1);
        qleft_.assign(m, 0);
        seg_.assign(m, {});

        order_.resize(n);
        std::iota(order_.begin(), order_.end(), 0);
        std::stable_sort(order_.begin(), order_.end(), [this](int a, int b) {
            return P_[a].arrival < P_[b].arrival;
        });

        i64 nextBoost = (cfg_.algo == Algo::MLFQ && cfg_.boost > 0) ? cfg_.boost : INF;
        std::vector<int> pending;   // preempted this instant, re-queued next instant
        i64 busy = 0;

        const auto t0 = std::chrono::steady_clock::now();

        while (done_ < n) {
            // (a) admit every process that has arrived by now
            while (nextArr_ < order_.size() &&
                   P_[order_[nextArr_]].arrival <= t_) {
                Process &p = P_[order_[nextArr_]];
                p.bi = 0;
                p.rem = p.bursts[0];
                p.cpuBurstNo = 1;
                p.level = 0;
                p.used = 0;
                enqueue(p.pid);
                ++nextArr_;
            }

            // (b) processes whose I/O has finished return to a ready queue
            while (!io_.empty() && io_.top().first <= t_) {
                const int pid = io_.top().second;
                io_.pop();
                Process &p = P_[pid - 1];
                ++p.bi;                       // step from the I/O burst to the CPU burst
                p.rem = p.bursts[p.bi];
                ++p.cpuBurstNo;
                enqueue(pid);
            }

            // (c) periodic priority boost
            while (t_ >= nextBoost) {
                boostAll();
                nextBoost += cfg_.boost;
            }

            // (d) processes preempted at this exact instant go behind anything
            //     that arrived or returned from I/O at the same instant
            for (int pid : pending) enqueue(pid);
            pending.clear();

            // (e) fill idle CPUs, then let a higher-priority ready process
            //     displace a lower-priority running one (MLFQ only)
            for (int guard = 0; guard <= 4 * m; ++guard) {
                dispatchIdle();
                if (cfg_.algo != Algo::MLFQ || !preemptOnce()) break;
            }

            // (f) how far can the clock safely jump?
            i64 delta = INF;
            bool anyBusy = false;
            for (int c = 0; c < m; ++c) {
                if (run_[c] < 0) continue;
                anyBusy = true;
                delta = std::min(delta, P_[run_[c] - 1].rem);
                delta = std::min(delta, qleft_[c]);
            }

            i64 next = INF;
            if (nextArr_ < order_.size())
                next = std::min(next, P_[order_[nextArr_]].arrival);
            if (!io_.empty()) next = std::min(next, io_.top().first);
            next = std::min(next, nextBoost);

            if (!anyBusy) {                   // every CPU idle: skip to the next event
                if (next >= INF) break;       // nothing left to do (should not happen)
                t_ = next;
                continue;
            }
            if (next < INF) delta = std::min(delta, next - t_);
            if (delta <= 0) delta = 1;        // defensive: never stall

            // (g) advance every running process by delta
            for (int c = 0; c < m; ++c) {
                if (run_[c] < 0) continue;
                Process &p = P_[run_[c] - 1];
                addSegment(c, p.pid, p.cpuBurstNo, t_, t_ + delta);
                p.rem -= delta;
                qleft_[c] -= delta;
                if (cfg_.algo == Algo::MLFQ) p.used += delta;
                busy += delta;
            }
            t_ += delta;

            // (h) burst completions take precedence over quantum expiry
            for (int c = 0; c < m; ++c) {
                if (run_[c] < 0) continue;
                Process &p = P_[run_[c] - 1];
                if (p.rem == 0) {
                    run_[c] = -1;
                    demoteIfExhausted(p);
                    if (p.bi + 1 >= p.bursts.size()) {
                        p.finish = t_;
                        ++done_;
                    } else {
                        ++p.bi;               // now sitting on an I/O burst
                        io_.emplace(t_ + p.bursts[p.bi], p.pid);
                    }
                } else if (qleft_[c] == 0) {
                    run_[c] = -1;
                    demoteIfExhausted(p);
                    pending.push_back(p.pid);
                }
            }
        }

        const auto t1 = std::chrono::steady_clock::now();
        st_.simMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // ---- metrics -------------------------------------------------------
        st_.nproc = n;
        i64 sumTat = 0, maxTat = 0, sumResp = 0, sumWait = 0;
        for (const Process &p : P_) {
            const i64 tat = p.finish - p.arrival;
            sumTat += tat;
            maxTat = std::max(maxTat, tat);
            sumResp += (p.first >= 0 ? p.first - p.arrival : 0);
            sumWait += tat - p.cpuTotal;
            st_.makespan = std::max(st_.makespan, p.finish);
        }
        if (n) {
            st_.avgTat = static_cast<double>(sumTat) / n;
            st_.maxTat = static_cast<double>(maxTat);
            st_.avgResp = static_cast<double>(sumResp) / n;
            st_.avgWait = static_cast<double>(sumWait) / n;
        }
        st_.util = st_.makespan ? static_cast<double>(busy) /
                                      (static_cast<double>(st_.makespan) * m)
                                : 0.0;
    }

    const Stats &stats() const { return st_; }

    void writeSchedule(std::ostream &os) const {
        for (std::size_t c = 0; c < seg_.size(); ++c) {
            os << "CPU" << c << "\n";
            for (const Segment &s : seg_[c])
                os << 'P' << s.pid << ',' << s.burstNo << '\t'
                   << (s.start + cfg_.base) << '\t'
                   << (s.end - 1 + cfg_.base) << '\n';
            os << "\n";
        }
    }

    void writePerProcess(std::ostream &os) const {
        os << "\npid  arrival  finish  turnaround  response\n";
        for (const Process &p : P_)
            os << 'P' << p.pid << "\t" << p.arrival << "\t" << p.finish << "\t"
               << (p.finish - p.arrival) << "\t"
               << (p.first >= 0 ? p.first - p.arrival : 0) << "\n";
    }

private:
    // Lazy boost: a boost only bumps a generation counter and splices the
    // queues, so any process whose recorded generation is stale is known to
    // belong at the top queue with a fresh allotment.  sync() pays that cost
    // once, when the process is next looked at.
    void sync(Process &p) {
        if (p.gen < boostGen_) { p.level = 0; p.used = 0; p.gen = boostGen_; }
    }

    void enqueue(int pid) {
        Process &p = P_[pid - 1];
        sync(p);
        rq_[cfg_.algo == Algo::MLFQ ? p.level : 0].push_back(pid);
    }

    int popHighest() {
        for (auto &q : rq_)
            if (!q.empty()) {
                const int pid = q.front();
                q.pop_front();
                return pid;
            }
        return -1;
    }

    void dispatchIdle() {
        for (int c = 0; c < cfg_.ncpus; ++c) {
            if (run_[c] >= 0) continue;
            const int pid = popHighest();
            if (pid < 0) return;
            run_[c] = pid;
            Process &p = P_[pid - 1];
            sync(p);
            if (p.first < 0) p.first = t_;
            switch (cfg_.algo) {
                case Algo::FIFO: qleft_[c] = INF; break;
                case Algo::RR:   qleft_[c] = cfg_.quantum; break;
                case Algo::MLFQ: qleft_[c] = cfg_.quantum - p.used; break;
            }
            ++st_.switches;
        }
    }

    // MLFQ rule 1: a job with strictly higher priority runs.  Returns true if a
    // running process was displaced, in which case the caller dispatches again.
    bool preemptOnce() {
        int best = -1;
        for (std::size_t l = 0; l < rq_.size(); ++l)
            if (!rq_[l].empty()) { best = static_cast<int>(l); break; }
        if (best < 0) return false;

        int victim = -1, worst = -1;
        for (int c = 0; c < cfg_.ncpus; ++c) {
            if (run_[c] < 0) return false;          // an idle CPU exists; no need
            const int lv = P_[run_[c] - 1].level;
            if (lv > worst) { worst = lv; victim = c; }
        }
        if (victim < 0 || best >= worst) return false;

        const int pid = run_[victim];
        run_[victim] = -1;
        rq_[P_[pid - 1].level].push_back(pid);      // allotment is preserved
        return true;
    }

    void demoteIfExhausted(Process &p) {
        if (cfg_.algo != Algo::MLFQ) return;
        sync(p);
        if (p.used >= cfg_.quantum) {               // allotment is cumulative:
            p.level = std::min(p.level + 1, cfg_.levels - 1);
            p.used = 0;                             // a job cannot game the scheduler
        }
    }

    void boostAll() {
        ++boostGen_;                                // every process is now stale
        for (std::size_t l = 1; l < rq_.size(); ++l)
            rq_[0].splice(rq_[0].end(), rq_[l]);    // O(1) per queue
        for (int c = 0; c < cfg_.ncpus; ++c)
            if (run_[c] >= 0) {
                sync(P_[run_[c] - 1]);
                qleft_[c] = cfg_.quantum;
            }
    }

    void addSegment(int cpu, int pid, int burstNo, i64 s, i64 e) {
        auto &v = seg_[cpu];
        if (!v.empty() && v.back().pid == pid && v.back().burstNo == burstNo &&
            v.back().end == s)
            v.back().end = e;                       // merge contiguous slices
        else
            v.push_back({pid, burstNo, s, e});
    }

    Config cfg_;
    std::vector<Process> P_;
    std::vector<int> order_;
    std::vector<std::list<int>> rq_;
    std::vector<int> run_;
    std::vector<i64> qleft_;
    std::vector<std::vector<Segment>> seg_;
    std::priority_queue<std::pair<i64, int>, std::vector<std::pair<i64, int>>,
                        std::greater<>> io_;
    std::size_t nextArr_ = 0;
    i64 t_ = 0;
    int done_ = 0;
    i64 boostGen_ = 0;
    Stats st_;
};

void usage(const char *prog) {
    std::cerr
        << "usage: " << prog << " <fifo|rr|mlfq> <workload-file> [options]\n"
        << "  --quantum <q>   time quantum for rr / mlfq        (default 2)\n"
        << "  --cpus <n>      number of processors              (default 1)\n"
        << "  --boost <b>     MLFQ boost period, 0 disables it  (default 0)\n"
        << "  --levels <l>    number of MLFQ queues             (default 3)\n"
        << "  --base <b>      time origin used when printing    (default 0)\n"
        << "  --schedule <f>  write the schedule to file f\n"
        << "  --no-schedule   suppress the schedule\n"
        << "  --per-process   print a per-process summary\n"
        << "  --csv           emit one CSV record instead of a report\n";
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 1; }

    Config cfg;
    cfg.algoName = argv[1];
    if (cfg.algoName == "fifo" || cfg.algoName == "fcfs") cfg.algo = Algo::FIFO;
    else if (cfg.algoName == "rr") cfg.algo = Algo::RR;
    else if (cfg.algoName == "mlfq") cfg.algo = Algo::MLFQ;
    else { std::cerr << "unknown algorithm: " << cfg.algoName << "\n"; usage(argv[0]); return 1; }
    cfg.workload = argv[2];

    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char *what) -> std::string {
            if (i + 1 >= argc) { std::cerr << what << " needs a value\n"; std::exit(1); }
            return argv[++i];
        };
        if (a == "--quantum" || a == "-q") cfg.quantum = std::stoll(need("--quantum"));
        else if (a == "--cpus" || a == "-c") cfg.ncpus = std::stoi(need("--cpus"));
        else if (a == "--boost") cfg.boost = std::stoll(need("--boost"));
        else if (a == "--levels") cfg.levels = std::stoi(need("--levels"));
        else if (a == "--base") cfg.base = std::stoll(need("--base"));
        else if (a == "--schedule") cfg.scheduleFile = need("--schedule");
        else if (a == "--no-schedule") cfg.printSchedule = false;
        else if (a == "--per-process") cfg.perProcess = true;
        else if (a == "--csv") { cfg.csv = true; cfg.printSchedule = false; }
        else { std::cerr << "unknown option: " << a << "\n"; usage(argv[0]); return 1; }
    }
    if (cfg.quantum < 1) { std::cerr << "quantum must be >= 1\n"; return 1; }
    if (cfg.ncpus < 1) { std::cerr << "cpus must be >= 1\n"; return 1; }
    if (cfg.levels < 1) { std::cerr << "levels must be >= 1\n"; return 1; }

    std::vector<Process> procs;
    if (!parseWorkload(cfg.workload, procs)) {
        std::cerr << "cannot open workload file: " << cfg.workload << "\n";
        return 1;
    }
    if (procs.empty()) { std::cerr << "workload contains no processes\n"; return 1; }

    Simulator sim(cfg, procs);
    sim.run();
    const Stats &s = sim.stats();

    if (cfg.csv) {
        // algorithm,quantum,cpus,boost,workload,nproc,avg_tat,max_tat,
        // avg_resp,avg_wait,makespan,util,switches,sim_ms
        std::printf("%s,%lld,%d,%lld,%s,%d,%.4f,%.0f,%.4f,%.4f,%lld,%.4f,%lld,%.4f\n",
                    cfg.algoName.c_str(), cfg.quantum, cfg.ncpus, cfg.boost,
                    cfg.workload.c_str(), s.nproc, s.avgTat, s.maxTat, s.avgResp,
                    s.avgWait, s.makespan, s.util, s.switches, s.simMs);
        return 0;
    }

    std::printf("algorithm                : %s", cfg.algoName.c_str());
    if (cfg.algo == Algo::RR) std::printf(" (quantum %lld)", cfg.quantum);
    if (cfg.algo == Algo::MLFQ) {
        std::printf(" (%d queues, quantum %lld, ", cfg.levels, cfg.quantum);
        if (cfg.boost > 0) std::printf("boost every %lld)", cfg.boost);
        else std::printf("no boost)");
    }
    std::printf("\n");
    std::printf("workload                 : %s (%d processes)\n",
                cfg.workload.c_str(), s.nproc);
    std::printf("processors               : %d\n", cfg.ncpus);
    std::printf("average turnaround time  : %.4f\n", s.avgTat);
    std::printf("maximum turnaround time  : %.0f\n", s.maxTat);
    std::printf("average response time    : %.4f\n", s.avgResp);
    std::printf("average waiting time     : %.4f\n", s.avgWait);
    std::printf("makespan                 : %lld\n", s.makespan);
    std::printf("cpu utilisation          : %.4f\n", s.util);
    std::printf("dispatches               : %lld\n", s.switches);
    std::printf("simulator run time       : %.4f ms  (simulation only, no file I/O)\n",
                s.simMs);

    if (cfg.perProcess) sim.writePerProcess(std::cout);

    if (!cfg.scheduleFile.empty()) {
        std::ofstream out(cfg.scheduleFile);
        if (!out) { std::cerr << "cannot write " << cfg.scheduleFile << "\n"; return 1; }
        sim.writeSchedule(out);
    } else if (cfg.printSchedule) {
        std::printf("\n");
        sim.writeSchedule(std::cout);
    }
    return 0;
}
