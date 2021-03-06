#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "verify_strategy.h"

enum class GuaranteedBest { Yes=1, No=0 };
enum class BelongsInFile { Yes=1, No=0 };

struct Strategy {
    std::function<std::vector<std::string>()> tests;
    int t;
    GuaranteedBest guaranteed_best;
    BelongsInFile belongs_in_file;

    explicit Strategy(std::vector<std::string> testvec, GuaranteedBest gb, BelongsInFile bf) :
        t(testvec.size()), guaranteed_best(gb), belongs_in_file(bf)
    {
        tests = [vec = std::move(testvec)]() { return vec; };
    }

    explicit Strategy(int t, GuaranteedBest gb, BelongsInFile bf, std::function<std::vector<std::string>()> tests) :
        t(t), guaranteed_best(gb), belongs_in_file(bf), tests(std::move(tests))
    {
    }

    bool isBetterThan(const Strategy& rhs) const {
        if (t != rhs.t) return (t < rhs.t);
        if (guaranteed_best != rhs.guaranteed_best) return (guaranteed_best == GuaranteedBest::Yes);
        return false;
    }

    std::string to_string(int n, int d) const {
        std::ostringstream oss;
        oss << "N=" << n << " D=" << d << " T=" << t;
        oss << " guaranteed_best=" << ((guaranteed_best == GuaranteedBest::Yes) ? '1' : '0') << '\n';
        auto concrete_tests = tests();
        for (auto&& test : concrete_tests) {
            oss << test << '\n';
        }
        return std::move(oss).str();
    }

    static std::string to_hex(unsigned long long bits) {
        std::ostringstream oss;
        oss << std::hex << bits;
        return std::move(oss).str();
    }

    std::string to_emathgroup_string(int n, int d) const {
        // This format comes from Zhao Hui Du, https://emathgroup.github.io/blog/two-poisoned-wine
        std::ostringstream oss;
        oss << "N=" << n << " D=" << d << " T=" << t;
        oss << " guaranteed_best=" << ((guaranteed_best == GuaranteedBest::Yes) ? '1' : '0') << '\n';
        oss << "emathgroup";
        int wordwrap = 10;
        auto concrete_tests = tests();
        for (int c=0; c < n; ++c) {
            unsigned long long bits = 0;
            for (int r = t-1; r >= 0; --r) {
                bits = ((bits << 1) | (concrete_tests[r][c] == '1'));
            }
            std::string hexbits = to_hex(bits);
            if (wordwrap + 1 + hexbits.size() > 75) {
                oss << "\n" << hexbits;
                wordwrap = hexbits.size();
            } else {
                oss << " " << hexbits;
                wordwrap += 1 + hexbits.size();
            }
        }
        oss << "\n";
        return std::move(oss).str();
    }

};

std::shared_ptr<Strategy> empty_strategy()
{
    return std::make_shared<Strategy>(std::vector<std::string>{}, GuaranteedBest::Yes, BelongsInFile::No);
}

std::shared_ptr<Strategy> perfect_strategy_for_one_wolf(int n)
{
    std::vector<std::string> tests;
    for (int bit = 1; bit < n; bit <<= 1) {
        tests.push_back(std::string(n, '.'));
        for (int i=0; i < n; ++i) {
            tests.back()[i] = ((i & bit) ? '1' : '.');
        }
    }
    return std::make_shared<Strategy>(std::move(tests), GuaranteedBest::Yes, BelongsInFile::No);
}

std::shared_ptr<Strategy> worst_case_strategy(int n, GuaranteedBest gb)
{
    return std::make_shared<Strategy>(
        n-1,
        gb,
        BelongsInFile::No,
        [n]() {
            std::vector<std::string> tests;
            for (int i=0; i < n-1; ++i) {
                tests.push_back(std::string(n, '.'));
                tests.back()[i] = '1';
            }
            return tests;
        }
    );
}

std::shared_ptr<Strategy> test_last_animal_individually(int n, std::shared_ptr<Strategy> orig)
{
    return std::make_shared<Strategy>(
        orig->t + 1,
        GuaranteedBest::No,
        BelongsInFile::No,
        [orig, n]() {
            auto tests = orig->tests();
            for (auto& test : tests) {
                test.push_back('.');
            }
            tests.push_back(std::string(n, '.') + '1');
            return tests;
        }
    );
}

std::shared_ptr<Strategy> replace_most_tested_animal(std::shared_ptr<Strategy> orig, bool with_wolf)
{
    auto tests = orig->tests();
    const int n = tests[0].size();

    std::vector<int> counts(n);
    for (auto&& test : tests) {
        for (int i=0; i < n; ++i) {
            counts[i] += (test[i] == '1');
        }
    }
    auto most_tested_idx = std::max_element(counts.begin(), counts.end()) - counts.begin();
    int most_tested_count = counts[most_tested_idx];
    assert(most_tested_count >= 2);

    if (with_wolf) {
        tests.erase(
            std::remove_if(
                tests.begin(),
                tests.end(),
                [&](const auto& test){ return test[most_tested_idx] == '1'; }
            ),
            tests.end()
        );
    }

    return std::make_shared<Strategy>(
        tests.size(),
        GuaranteedBest::No,
        BelongsInFile::No,
        [most_tested_idx, captured_tests = std::move(tests)]() {
            auto tests = captured_tests;
            for (auto&& test : tests) {
                test.erase(test.begin() + most_tested_idx);
            }
            return tests;
        }
    );
}


struct ND {
    int n, d;
    bool operator<(const ND& rhs) const { return std::tie(d, n) < std::tie(rhs.d, rhs.n); }
};

bool overwrite_if_better(std::map<ND, std::shared_ptr<Strategy>>& m, int n, int d, std::shared_ptr<Strategy> strategy)
{
    assert(0 <= n);
    assert(0 <= d && d <= n);
    auto it = m.find(ND{n,d});
    if (it == m.end()) {
        return false;
    } else if (strategy->isBetterThan(*it->second)) {
        if (it->second->guaranteed_best != GuaranteedBest::No) {
            printf("Replacing t(%d,%d)<=%d with t(%d,%d)<=%d\n", n,d,it->second->t,n,d,strategy->t);
        }
        assert((it->second->guaranteed_best == GuaranteedBest::No) || !"found something better than the guaranteed best");
        if (it->second->belongs_in_file == BelongsInFile::Yes) {
            // If this solution came from the file, we don't want to completely vanish it.
            // Replace it in the file with this better solution.
            strategy->belongs_in_file = BelongsInFile::Yes;
        }
        it->second = strategy;
        return true;
    }
    return false;
}

void preserve_from_file(std::map<ND, std::shared_ptr<Strategy>>& m, int n, int d, std::shared_ptr<Strategy> strategy)
{
    m.insert(std::make_pair(ND{n,d}, worst_case_strategy(n, GuaranteedBest::No)));
    bool overwritten = overwrite_if_better(m, n, d, strategy);
    assert(overwritten);
}

void read_solutions_from_file(const char *filename, std::map<ND, std::shared_ptr<Strategy>>& m)
{
    std::ifstream infile(filename);
    if (!infile.is_open()) {
        throw std::runtime_error("Failed to open solution file");
    }
    std::string line;
    bool seen_a_grid = false;
    while (std::getline(infile, line)) {
        if (line.compare(0, 2, "N=") == 0) {
            int n, d, t, gb;
            std::vector<std::string> tests;
            int rc = std::sscanf(line.c_str(), "N=%d D=%d T=%d guaranteed_best=%d", &n, &d, &t, &gb);
            assert(rc == 4 || !"input file contained malformed lines");
            char nextch = infile.get();
            infile.putback(nextch);
            if (nextch == 'e') {
                std::string word;
                infile >> word;
                assert(word == "emathgroup");
                // This format comes from Zhao Hui Du, https://emathgroup.github.io/blog/two-poisoned-wine
                tests.resize(t);
                for (int i=0; i < n; ++i) {
                    infile >> word;
                    unsigned long long bits;
                    rc = std::sscanf(word.c_str(), "%llx", &bits);
                    assert(rc == 1 || !"emathgroup format contained malformed lines");
                    assert(0 <= bits && bits < (1uLL << t));
                    for (int r = 0; r < t; ++r) {
                        tests[r].push_back((bits & 1) ? '1' : '.');
                        bits >>= 1;
                    }
                }
            } else {
                for (int r=0; r < t; ++r) {
                    std::getline(infile, line);
                    assert(line.size() == n || !"input file contained malformed solution");
                    tests.push_back(line);
                }
            }
            auto strategy = std::make_shared<Strategy>(tests, gb ? GuaranteedBest::Yes : GuaranteedBest::No, BelongsInFile::Yes);
            preserve_from_file(m, n, d, std::move(strategy));
            seen_a_grid = true;
        } else if (seen_a_grid && line != "") {
            assert(!"input file contained malformed lines after the first grid");
        }
    }
}

void write_solutions_to_file(const char *filename, const std::map<ND, std::shared_ptr<Strategy>>& m)
{
    std::ofstream outfile(filename);

    // Write out the triangle, up to n=30.
    const int max_n_to_print = 30;

    outfile << "    d=       1  2  3  4  5  6  ...\n";
    outfile << "          .\n";
    outfile << "    n=1   .  0\n";
    outfile << "    n=2   .  1  0\n";
    outfile << "    n=3   .  2  2\n";

    for (int n = 4; n <= max_n_to_print; ++n) {
        outfile << "    n=" << std::setw(2) << std::left << n << "   ";
        for (int d = 1; d < n; ++d) {
            auto it = m.find(ND{n,d});
            if (it != m.end()) {
                int value = it->second->t;
                outfile << ' ' << std::setw(2) << std::right << value;
                if (value == n-1) {
                    // Don't bother filling out the rest of this line.
                    break;
                }
            } else {
                outfile << std::setw(3) << std::right << '?';
            }
        }
        outfile << "\n";
    }
    outfile << "\n\n";

    for (const auto& kv : m) {
        int n = kv.first.n;
        int d = kv.first.d;
        const auto& strategy = kv.second;
        if (strategy->belongs_in_file == BelongsInFile::Yes) {
            if (n > 150) {
                outfile << strategy->to_emathgroup_string(n, d) << '\n';
            } else {
                outfile << strategy->to_string(n, d) << '\n';
            }
        }
    }
}

void add_easy_solutions(std::map<ND, std::shared_ptr<Strategy>>& m, int max_n)
{
    for (int n=0; n <= max_n; ++n) {
        for (int d=0; d <= n; ++d) {
            auto strategy =
                (d == 0 || d == n) ? empty_strategy() :
                (d == 1) ? perfect_strategy_for_one_wolf(n) :
                (d >= n/2) ? worst_case_strategy(n, GuaranteedBest::Yes) :
                worst_case_strategy(n, GuaranteedBest::No);
            m.insert(std::make_pair(ND{n,d}, strategy));
        }
    }
}

template<class KeyValue>
void add_solutions_derived_from(std::map<ND, std::shared_ptr<Strategy>>& m, const KeyValue& kv)
{
    int n = kv.first.n;
    int d = kv.first.d;
    const std::shared_ptr<Strategy>& strategy = kv.second;
    int t = strategy->t;

    if (2 <= d && d < n && t < n-1) {
        // A solution to t(n-k,d) can be constructed from t(n,d): simply introduce k innocent sheep.
        // It's only worth doing if t < n-1.
        auto smaller_strategy = replace_most_tested_animal(strategy, false);
        if (overwrite_if_better(m, n-1, d, smaller_strategy)) {
            add_solutions_derived_from(m, *m.find(ND{n-1, d}));
        }
        if (d-1 >= 2) {
            auto smaller_strategy = replace_most_tested_animal(strategy, true);
            if (overwrite_if_better(m, n-1, d-1, smaller_strategy)) {
                add_solutions_derived_from(m, *m.find(ND{n-1, d-1}));
            }
        }
    }
    if (2 < d && d < n-1 && t < n-1) {
        // A solution for (n,d) also works for (n,d-1) except when d >= n-1.
        std::shared_ptr<Strategy> r = std::make_shared<Strategy>(
            strategy->t,
            GuaranteedBest::No,
            BelongsInFile::No,
            [strategy]() { return strategy->tests(); }
        );
        if (overwrite_if_better(m, n, d-1, r)) {
            add_solutions_derived_from(m, *m.find(ND{n, d-1}));
        }
    }
    if (2 <= d && d < n && t < n-1) {
        if (overwrite_if_better(m, n+1, d, test_last_animal_individually(n, strategy))) {
            add_solutions_derived_from(m, *m.find(ND{n+1, d}));
        }
    }
    if (strategy->t == n-1) {
        if (overwrite_if_better(m, n+2, d+1, worst_case_strategy(n+2, strategy->guaranteed_best))) {
            add_solutions_derived_from(m, *m.find(ND{n+2, d+1}));
        }
    }
}

int main(int argc, char **argv)
{
    const char *filename = "wolfy-out.txt";
    bool verify = false;
    bool verify_all = false;
    int i = 1;
    for (; argv[i] != nullptr && argv[i][0] == '-'; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            puts("./wolfy [--file f.txt] [--verify] N D");
            puts("");
            puts("Print the smallest known D-separable matrix with N columns.");
            puts("  --file f.txt    Read best known solutions from this file");
            puts("  --verify        Verbosely verify the solution that is printed");
            puts("  --verify-all    Verify every solution in the input file");
            exit(0);
        } else if (strcmp(argv[i], "--file") == 0) {
            filename = argv[++i];
        } else if (strcmp(argv[i], "--verify") == 0) {
            verify = true;
        } else if (strcmp(argv[i], "--verify-all") == 0) {
            verify_all = true;
        } else {
            printf("Unrecognized option '%s'; --help for help\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }
    if (i + 2 != argc) {
        printf("Usage: ./wolfy N D; --help for help\n");
        exit(EXIT_FAILURE);
    }

    int n = atoi(argv[i]);
    int d = atoi(argv[i+1]);

    if (n < d || d < 0) {
        printf("Usage: ./wolfy N D; --help for help\n");
        exit(EXIT_FAILURE);
    }

    std::map<ND, std::shared_ptr<Strategy>> solutions_from_file;
    read_solutions_from_file(filename, solutions_from_file);

    if (verify_all) {
        for (auto&& kv : solutions_from_file) {
            VerifyStrategyResult r = verify_strategy(kv.first.n, kv.first.d, kv.second->tests());
            if (!r.success) {
                printf("INVALID! (This should never happen unless the solution file is bad.)\n");
                printf("%s\n", kv.second->to_string(n, d).c_str());
                printf("These two wolf arrangements cannot be distinguished:\n");
                printf("%s\n", r.w1.c_str());
                printf("%s\n", r.w2.c_str());
            }
        }
    }

    std::map<ND, std::shared_ptr<Strategy>> all_solutions;
    add_easy_solutions(all_solutions, n + 100);
    for (auto&& kv : solutions_from_file) {
        preserve_from_file(all_solutions, kv.first.n, kv.first.d, kv.second);
        add_solutions_derived_from(all_solutions, kv);
    }

    write_solutions_to_file("wolfy-out.txt", all_solutions);

    std::shared_ptr<Strategy> strategy = all_solutions.at(ND{n, d});
    auto tests = strategy->tests();

    if (verify) {
        printf("Candidate is\n");
        printf("%s\n", strategy->to_string(n, d).c_str());
        VerifyStrategyResult r = verify_strategy(n, d, tests);
        if (r.success) {
            printf("Verified. This is a solution for t(%d, %d) <= %zu.\n", n, d, tests.size());
        } else {
            printf("INVALID! (This should never happen unless the solution file is bad.)\n");
            printf("These two wolf arrangements cannot be distinguished:\n");
            printf("%s\n", r.w1.c_str());
            printf("%s\n", r.w2.c_str());
        }
    } else {
        for (auto&& line : tests) printf("%s\n", line.c_str());
    }
}
