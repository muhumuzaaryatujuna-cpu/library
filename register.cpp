// =====================================================================================
//  library_register.cpp  -  Library Books Returns Register  (single-file version)
//  Doubly linked list + insertion sort / merge sort / quicksort (templates) + binary search
//
//  Build:   g++ -std=c++17 -O2 library_register.cpp -o library_register
//  Run:     library_register            -> interactive menu (creates returns.txt if missing)
//           library_register test       -> runs the A3 test table
//           library_register bench      -> runs the A4 timing experiment
//           library_register myfile.txt -> menu using another data file
//
//  Sections:  1 Record + list   2 Sorts + binary search   3 Menu program
//             4 Tests (A3)      5 Timing experiment (A4)  6 main()
// =====================================================================================
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <new>
#include <numeric>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ===================== 1. RECORD STRUCT + DOUBLY LINKED LIST =====================
// register.h - Library Books Returns Register (A1)
// Doubly linked list of ReturnRecord with deep-copy semantics.

// ---------- The scenario record ----------
struct ReturnRecord {
    int         id = 0;          // unique return-slip number (also the arrival sequence)
    std::string memberName;      // borrower
    std::string bookTitle;       // book returned
    std::string dueDate;         // YYYY-MM-DD
    std::string returnDate;      // YYYY-MM-DD

    int  daysLate() const;       // 0 if returned on/before due date
    long fineUGX()  const;       // 500 UGX per late day
};

// Helpers (pure functions, never throw)
bool isValidDate(const std::string& s);          // strict YYYY-MM-DD with real calendar check
long dayNumber(const std::string& s);            // days since 1970-01-01, -1 if invalid
bool isValidRecord(const ReturnRecord& r);       // id>0, non-empty text without '|', valid dates

// ---------- Doubly linked list ----------
class ReturnsRegister {
public:
    ReturnsRegister();
    ReturnsRegister(const ReturnsRegister& other);             // deep copy
    ReturnsRegister(ReturnsRegister&& other) noexcept;         // steal
    ReturnsRegister& operator=(ReturnsRegister other) noexcept;// copy-and-swap
    ~ReturnsRegister();

    // Insertion - return false (and change nothing) if the record is invalid,
    // the id already exists, or memory cannot be allocated.
    bool pushBack(const ReturnRecord& r);
    bool pushFront(const ReturnRecord& r);

    // Removal - return false if the list is empty / id absent.
    bool removeFirst(ReturnRecord* removed = nullptr);
    bool removeLast(ReturnRecord* removed = nullptr);
    bool removeById(int id, ReturnRecord* removed = nullptr);

    // Query
    const ReturnRecord* findById(int id) const;   // nullptr if absent
    std::size_t size() const { return count_; }
    bool        empty() const { return count_ == 0; }
    void        clear();

    // Display
    void printForward(std::ostream& os) const;
    void printBackward(std::ostream& os) const;

    // File I/O. Format: id|member|title|due|returned   ('#' comments, blank lines ignored)
    // load: returns number of records loaded, or -1 if the file cannot be opened
    //       (list untouched). Bad lines are skipped and counted in *skipped.
    int  load(const std::string& path, int* skipped = nullptr);
    bool save(const std::string& path) const;

    // Bridge to the array-based sorting/searching code (A2)
    std::vector<ReturnRecord> toVector() const;
    bool replaceWith(const std::vector<ReturnRecord>& v);   // list takes the vector's order

    // Test hook: checks prev/next/head/tail/count consistency
    bool selfCheck() const;

private:
    struct Node {
        ReturnRecord data;
        Node* prev;
        Node* next;
        explicit Node(const ReturnRecord& r) : data(r), prev(nullptr), next(nullptr) {}
    };
    Node*       head_;
    Node*       tail_;
    std::size_t count_;

    void   unlink(Node* n);          // detaches and deletes n
    bool   appendUnchecked(const ReturnRecord& r);
    Node*  findNode(int id) const;
    void   swap(ReturnsRegister& o) noexcept;
};

// ---------- list member definitions ----------

// ================= date helpers =================
static bool parseDate(const std::string& s, int& y, int& m, int& d) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i : {0,1,2,3,5,6,8,9}) if (s[i] < '0' || s[i] > '9') return false;
    y = std::atoi(s.substr(0,4).c_str());
    m = std::atoi(s.substr(5,2).c_str());
    d = std::atoi(s.substr(8,2).c_str());
    if (y < 1900 || y > 2200 || m < 1 || m > 12 || d < 1) return false;
    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    int maxd = dim[m-1] + ((m == 2 && leap) ? 1 : 0);
    return d <= maxd;
}
bool isValidDate(const std::string& s) { int y,m,d; return parseDate(s,y,m,d); }

long dayNumber(const std::string& s) {           // days-from-civil algorithm
    int y,m,d;
    if (!parseDate(s,y,m,d)) return -1;
    y -= m <= 2;
    long era = (y >= 0 ? y : y-399) / 400;
    long yoe = y - era * 400;
    long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe/4 - yoe/100 + doy;
    return era * 146097 + doe - 719468;
}

int ReturnRecord::daysLate() const {
    long due = dayNumber(dueDate), ret = dayNumber(returnDate);
    if (due < 0 || ret < 0 || ret <= due) return 0;
    return static_cast<int>(ret - due);
}
long ReturnRecord::fineUGX() const { return 500L * daysLate(); }

static bool cleanText(const std::string& s) {
    if (s.empty()) return false;
    bool anyNonSpace = false;
    for (char c : s) {
        if (c == '|' || c == '\n' || c == '\r') return false;
        if (c != ' ' && c != '\t') anyNonSpace = true;
    }
    return anyNonSpace;
}
bool isValidRecord(const ReturnRecord& r) {
    return r.id > 0 && cleanText(r.memberName) && cleanText(r.bookTitle)
        && isValidDate(r.dueDate) && isValidDate(r.returnDate);
}

// ================= list =================
ReturnsRegister::ReturnsRegister() : head_(nullptr), tail_(nullptr), count_(0) {}

// Deep copy: every node is newly allocated; no pointers shared with 'other'.
ReturnsRegister::ReturnsRegister(const ReturnsRegister& other)
    : head_(nullptr), tail_(nullptr), count_(0) {
    try {
        for (Node* p = other.head_; p; p = p->next)
            if (!appendUnchecked(p->data)) throw std::bad_alloc();
    } catch (...) {          // a destructor is NOT run if a constructor throws
        clear();
        throw;
    }
}
ReturnsRegister::ReturnsRegister(ReturnsRegister&& o) noexcept
    : head_(o.head_), tail_(o.tail_), count_(o.count_) {
    o.head_ = o.tail_ = nullptr; o.count_ = 0;
}
ReturnsRegister& ReturnsRegister::operator=(ReturnsRegister other) noexcept {
    swap(other);            // 'other' (the copy) frees our old nodes; self-assignment safe
    return *this;
}
ReturnsRegister::~ReturnsRegister() { clear(); }

void ReturnsRegister::swap(ReturnsRegister& o) noexcept {
    std::swap(head_, o.head_); std::swap(tail_, o.tail_); std::swap(count_, o.count_);
}
void ReturnsRegister::clear() {
    Node* p = head_;
    while (p) { Node* nx = p->next; delete p; p = nx; }
    head_ = tail_ = nullptr; count_ = 0;
}

ReturnsRegister::Node* ReturnsRegister::findNode(int id) const {
    for (Node* p = head_; p; p = p->next) if (p->data.id == id) return p;
    return nullptr;
}
const ReturnRecord* ReturnsRegister::findById(int id) const {
    Node* n = findNode(id);
    return n ? &n->data : nullptr;
}

bool ReturnsRegister::appendUnchecked(const ReturnRecord& r) {
    Node* n = new (std::nothrow) Node(r);
    if (!n) return false;
    n->prev = tail_;
    if (tail_) tail_->next = n; else head_ = n;
    tail_ = n; ++count_;
    return true;
}
bool ReturnsRegister::pushBack(const ReturnRecord& r) {
    if (!isValidRecord(r) || findNode(r.id)) return false;
    return appendUnchecked(r);
}
bool ReturnsRegister::pushFront(const ReturnRecord& r) {
    if (!isValidRecord(r) || findNode(r.id)) return false;
    Node* n = new (std::nothrow) Node(r);
    if (!n) return false;
    n->next = head_;
    if (head_) head_->prev = n; else tail_ = n;
    head_ = n; ++count_;
    return true;
}

void ReturnsRegister::unlink(Node* n) {
    if (n->prev) n->prev->next = n->next; else head_ = n->next;
    if (n->next) n->next->prev = n->prev; else tail_ = n->prev;
    delete n; --count_;
}
bool ReturnsRegister::removeFirst(ReturnRecord* out) {
    if (!head_) return false;
    if (out) *out = head_->data;
    unlink(head_);
    return true;
}
bool ReturnsRegister::removeLast(ReturnRecord* out) {
    if (!tail_) return false;
    if (out) *out = tail_->data;
    unlink(tail_);
    return true;
}
bool ReturnsRegister::removeById(int id, ReturnRecord* out) {
    Node* n = findNode(id);
    if (!n) return false;
    if (out) *out = n->data;
    unlink(n);
    return true;
}

static void printHeader(std::ostream& os) {
    os << std::left << std::setw(6) << "ID" << std::setw(20) << "Member"
       << std::setw(30) << "Book" << std::setw(12) << "Due"
       << std::setw(12) << "Returned" << std::right << std::setw(6) << "Late"
       << std::setw(10) << "Fine(UGX)" << "\n";
}
static void printRow(std::ostream& os, const ReturnRecord& r) {
    os << std::left << std::setw(6) << r.id << std::setw(20) << r.memberName.substr(0,19)
       << std::setw(30) << r.bookTitle.substr(0,29) << std::setw(12) << r.dueDate
       << std::setw(12) << r.returnDate << std::right << std::setw(6) << r.daysLate()
       << std::setw(10) << r.fineUGX() << "\n";
}
void ReturnsRegister::printForward(std::ostream& os) const {
    if (!head_) { os << "(register is empty)\n"; return; }
    printHeader(os);
    for (Node* p = head_; p; p = p->next) printRow(os, p->data);
}
void ReturnsRegister::printBackward(std::ostream& os) const {
    if (!tail_) { os << "(register is empty)\n"; return; }
    printHeader(os);
    for (Node* p = tail_; p; p = p->prev) printRow(os, p->data);
}

// ---------- file I/O ----------
static bool parseLine(const std::string& line, ReturnRecord& r) {
    std::vector<std::string> f;
    std::string cur;
    std::istringstream ss(line);
    while (std::getline(ss, cur, '|')) f.push_back(cur);
    if (!line.empty() && line.back() == '|') f.push_back("");
    if (f.size() != 5) return false;
    char* end = nullptr;
    long id = std::strtol(f[0].c_str(), &end, 10);
    if (f[0].empty() || *end != '\0' || id <= 0 || id > 2000000000L) return false;
    r.id = static_cast<int>(id);
    r.memberName = f[1]; r.bookTitle = f[2]; r.dueDate = f[3]; r.returnDate = f[4];
    return isValidRecord(r);
}

int ReturnsRegister::load(const std::string& path, int* skipped) {
    std::ifstream in(path);
    if (!in) return -1;
    ReturnsRegister temp;                    // build aside; commit only at the end
    int bad = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        ReturnRecord r;
        if (!parseLine(line, r) || !temp.pushBack(r)) ++bad;   // pushBack rejects duplicate ids
    }
    if (skipped) *skipped = bad;
    int n = static_cast<int>(temp.count_);
    swap(temp);
    return n;
}
bool ReturnsRegister::save(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "# id|member|title|due|returned\n";
    for (Node* p = head_; p; p = p->next)
        out << p->data.id << '|' << p->data.memberName << '|' << p->data.bookTitle << '|'
            << p->data.dueDate << '|' << p->data.returnDate << '\n';
    out.flush();
    return static_cast<bool>(out);
}

// ---------- bridge to sorting ----------
std::vector<ReturnRecord> ReturnsRegister::toVector() const {
    std::vector<ReturnRecord> v;
    v.reserve(count_);
    for (Node* p = head_; p; p = p->next) v.push_back(p->data);
    return v;
}
bool ReturnsRegister::replaceWith(const std::vector<ReturnRecord>& v) {
    ReturnsRegister temp;
    for (const auto& r : v) if (!temp.pushBack(r)) return false;   // original untouched on failure
    swap(temp);
    return true;
}

bool ReturnsRegister::selfCheck() const {
    std::size_t n = 0;
    Node* last = nullptr;
    for (Node* p = head_; p; p = p->next) {
        if (p->prev != last) return false;
        last = p; ++n;
        if (n > count_) return false;
    }
    if (last != tail_ || n != count_) return false;
    if (!head_ && (tail_ || count_)) return false;
    if (head_ && head_->prev) return false;
    if (tail_ && tail_->next) return false;
    return true;
}

// ---------- built-in sample data (12 records) so the program runs with no extra file ----------
static const char* SAMPLE_DATA = R"DATA(# Library returns register sample data
# id|member|title|due|returned
1001|Achieng Grace|Things Fall Apart|2026-10-20|2026-10-22
1002|Okello David|Introduction to Algorithms|2026-10-18|2026-10-18
1003|Nakato Sarah|Data Structures in C++|2026-10-21|2026-10-25
1004|Mugisha Peter|The River Between|2026-10-19|2026-10-17
1005|Namukasa Ruth|Operating System Concepts|2026-10-22|2026-10-22
1006|Ssemwogerere Ivan|Clean Code|2026-10-15|2026-10-24
1007|Atim Joan|Weep Not, Child|2026-10-23|2026-10-22
1008|Kato Brian|Computer Networks|2026-10-20|2026-10-25
1009|Nabirye Esther|Database System Concepts|2026-10-24|2026-10-25
1010|Opio Samuel|The Pragmatic Programmer|2026-10-17|2026-10-25
1011|Tumusiime Alice|Discrete Mathematics|2026-10-25|2026-10-25
1012|Byaruhanga Moses|Artificial Intelligence: A Modern Approach|2026-10-16|2026-10-22
)DATA";

static void writeSampleFile(const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    f << SAMPLE_DATA;
}
static bool fileExists(const std::string& path) { std::ifstream f(path); return static_cast<bool>(f); }

// ===================== 2. SORTS + BINARY SEARCH =====================
// sorts.h - A2: comparator-templated sorts and iterative binary search.
// A comparator is a strict "less-than": cmp(a,b) is true iff a must come before b.

// ---- Insertion sort: O(n^2) worst/average, O(n) best, STABLE, in place ----
// Loop invariant: at the start of each iteration with index i, a[0..i-1] holds the
// ORIGINAL first i elements in sorted (non-decreasing) order.
template <typename T, typename Comp>
void insertionSort(std::vector<T>& a, Comp cmp) {
    for (std::size_t i = 1; i < a.size(); ++i) {
        T key = std::move(a[i]);
        std::size_t j = i;
        while (j > 0 && cmp(key, a[j - 1])) {     // strict: equal keys are NOT passed => stable
            a[j] = std::move(a[j - 1]);
            --j;
        }
        a[j] = std::move(key);
    }
}

// ---- Merge sort: O(n log n) all cases, STABLE, O(n) extra space ----
namespace detail {
template <typename T, typename Comp>
void mergeSortRec(std::vector<T>& a, std::vector<T>& tmp,
                  std::size_t lo, std::size_t hi, Comp& cmp) {      // sorts a[lo, hi)
    if (hi - lo < 2) return;
    std::size_t mid = lo + (hi - lo) / 2;
    mergeSortRec(a, tmp, lo, mid, cmp);
    mergeSortRec(a, tmp, mid, hi, cmp);
    std::size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        if (cmp(a[j], a[i])) tmp[k++] = std::move(a[j++]);   // right only if STRICTLY smaller
        else                 tmp[k++] = std::move(a[i++]);   // ties go left => stable
    }
    while (i < mid) tmp[k++] = std::move(a[i++]);
    while (j < hi)  tmp[k++] = std::move(a[j++]);
    for (k = lo; k < hi; ++k) a[k] = std::move(tmp[k]);
}
} // namespace detail

template <typename T, typename Comp>
void mergeSort(std::vector<T>& a, Comp cmp) {
    if (a.size() < 2) return;
    std::vector<T> tmp(a.size());
    detail::mergeSortRec(a, tmp, 0, a.size(), cmp);
}

// ---- Quicksort, random pivot, Hoare partition: O(n log n) expected, O(n^2) worst,
//      NOT stable, in place (recurses on the smaller side => O(log n) stack) ----
namespace detail {
template <typename T, typename Comp, typename Rng>
void quickSortRec(std::vector<T>& a, std::ptrdiff_t lo, std::ptrdiff_t hi, Comp& cmp, Rng& rng) {
    while (lo < hi) {                                              // sorts a[lo..hi]
        std::uniform_int_distribution<std::ptrdiff_t> pick(lo, hi);
        T pivot = a[pick(rng)];                                    // random pivot (copy)
        std::ptrdiff_t i = lo, j = hi;
        while (i <= j) {
            while (cmp(a[i], pivot)) ++i;
            while (cmp(pivot, a[j])) --j;
            if (i <= j) { std::swap(a[i], a[j]); ++i; --j; }
        }
        if (j - lo < hi - i) { quickSortRec(a, lo, j, cmp, rng); lo = i; }
        else                 { quickSortRec(a, i, hi, cmp, rng); hi = j; }
    }
}
} // namespace detail

template <typename T, typename Comp>
void quickSort(std::vector<T>& a, Comp cmp) {
    if (a.size() < 2) return;
    static std::mt19937 rng(std::random_device{}());
    detail::quickSortRec(a, 0, static_cast<std::ptrdiff_t>(a.size()) - 1, cmp, rng);
}

// ---- Choose the O(n log n) sort by the last digit of the registration number ----
//   even -> merge sort (1)      odd -> quicksort with random pivot (0)
#ifndef USE_MERGE_SORT
#define USE_MERGE_SORT 1
#endif
template <typename T, typename Comp>
void fastSort(std::vector<T>& a, Comp cmp) {
#if USE_MERGE_SORT
    mergeSort(a, cmp);
#else
    quickSort(a, cmp);
#endif
}

// ---- Iterative binary search: O(log n) ----
// cmp3(element, key) returns <0 if element<key, 0 if equal, >0 if element>key.
// Returns the index of the LEFTMOST match (so duplicates are found from their
// first occurrence) or -1 if absent.  Requires a sorted by the same key.
template <typename T, typename K, typename Cmp3>
std::ptrdiff_t binarySearch(const std::vector<T>& a, const K& key, Cmp3 cmp3) {
    std::ptrdiff_t lo = 0, hi = static_cast<std::ptrdiff_t>(a.size()) - 1, found = -1;
    while (lo <= hi) {
        std::ptrdiff_t mid = lo + (hi - lo) / 2;
        int c = cmp3(a[mid], key);
        if (c == 0)      { found = mid; hi = mid - 1; }   // keep looking left for first match
        else if (c < 0)  lo = mid + 1;
        else             hi = mid - 1;
    }
    return found;
}

// ===================== 3. INTERACTIVE MENU PROGRAM =====================
namespace menu_app {
// main.cpp - interactive Library Returns Register

// ---- comparators (strict less-than). Ties are broken by id = arrival order, so even the
//      unstable quicksort gives a deterministic, arrival-preserving result. ----
static bool byReturnDate(const ReturnRecord& a, const ReturnRecord& b) {
    if (a.returnDate != b.returnDate) return a.returnDate < b.returnDate;   // ISO dates sort as text
    return false;                                                           // pure key (stability test)
}
static bool byReturnDateThenId(const ReturnRecord& a, const ReturnRecord& b) {
    if (a.returnDate != b.returnDate) return a.returnDate < b.returnDate;
    return a.id < b.id;
}
static bool byMember(const ReturnRecord& a, const ReturnRecord& b) { return a.memberName < b.memberName; }
static bool byTitle (const ReturnRecord& a, const ReturnRecord& b) { return a.bookTitle  < b.bookTitle; }
static bool byFineDesc(const ReturnRecord& a, const ReturnRecord& b) { return a.fineUGX() > b.fineUGX(); }
static bool byId    (const ReturnRecord& a, const ReturnRecord& b) { return a.id < b.id; }

static std::string readLine(const std::string& prompt) {
    std::cout << prompt;
    std::string s;
    if (!std::getline(std::cin, s)) { std::cout << "\n(input closed)\n"; std::exit(0); }
    return s;
}
static int readInt(const std::string& prompt) {      // returns -1 on bad input
    std::string s = readLine(prompt);
    try { std::size_t p; int v = std::stoi(s, &p); return p == s.size() ? v : -1; }
    catch (...) { return -1; }
}

static void sortMenu(ReturnsRegister& reg) {
    std::cout << " 1 return date (stable)  2 member  3 title  4 fine (desc)  5 id\n";
    int k = readInt("Sort by: ");
    int alg = readInt(" Algorithm: 1 insertion  2 O(n log n) sort: ");
    if (k < 1 || k > 5 || (alg != 1 && alg != 2)) { std::cout << "Invalid choice.\n"; return; }
    auto v = reg.toVector();
    auto run = [&](auto cmp) { if (alg == 1) insertionSort(v, cmp); else fastSort(v, cmp); };
    switch (k) {
        case 1: run(byReturnDate);  break;
        case 2: run(byMember);      break;
        case 3: run(byTitle);       break;
        case 4: run(byFineDesc);    break;
        case 5: run(byId);          break;
    }
    if (reg.replaceWith(v)) { std::cout << "Sorted.\n"; reg.printForward(std::cout); }
    else std::cout << "Sort failed; register unchanged.\n";
}

static void searchMenu(ReturnsRegister& reg) {
    if (reg.empty()) { std::cout << "Register is empty.\n"; return; }
    std::string d = readLine("Return date to search (YYYY-MM-DD): ");
    if (!isValidDate(d)) { std::cout << "Invalid date.\n"; return; }
    auto v = reg.toVector();
    mergeSort(v, byReturnDateThenId);                 // binary search needs sorted-by-key data
    auto idx = binarySearch(v, d, [](const ReturnRecord& r, const std::string& key) {
        return r.returnDate < key ? -1 : (r.returnDate > key ? 1 : 0); });
    if (idx < 0) { std::cout << "No returns on " << d << ".\n"; return; }
    std::cout << "Returns on " << d << ":\n";
    for (std::size_t i = idx; i < v.size() && v[i].returnDate == d; ++i)
        std::cout << "  #" << v[i].id << "  " << v[i].memberName << "  -  " << v[i].bookTitle
                  << "  (fine " << v[i].fineUGX() << " UGX)\n";
}

int runMenu(const std::string& path) {
    ReturnsRegister reg;
    int skipped = 0, n = reg.load(path, &skipped);
    if (n < 0) std::cout << "Could not open '" << path << "' - starting with an empty register.\n";
    else       std::cout << "Loaded " << n << " record(s) from '" << path << "' (" << skipped << " skipped).\n";

    for (;;) {
        std::cout << "\n=== Library Returns Register (" << reg.size() << " records) ===\n"
                     " 1 Add return (end)   2 Add return (front)  3 Remove by id\n"
                     " 4 Remove first       5 Remove last         6 Find by id\n"
                     " 7 Show forward       8 Show backward       9 Sort\n"
                     "10 Search by return date (binary)  11 Save  12 Reload  0 Save & quit\n";
        int c = readInt("Choice: ");
        switch (c) {
            case 1: case 2: {
                ReturnRecord r;
                r.id = readInt("Slip id (positive int): ");
                r.memberName = readLine("Member name: ");
                r.bookTitle  = readLine("Book title: ");
                r.dueDate    = readLine("Due date (YYYY-MM-DD): ");
                r.returnDate = readLine("Return date (YYYY-MM-DD): ");
                bool ok = (c == 1) ? reg.pushBack(r) : reg.pushFront(r);
                std::cout << (ok ? "Added.\n" : "Rejected: invalid field or duplicate id.\n");
                break; }
            case 3: { int id = readInt("Id to remove: ");
                      std::cout << (reg.removeById(id) ? "Removed.\n" : "No such id.\n"); break; }
            case 4: std::cout << (reg.removeFirst() ? "Removed first.\n" : "Register is empty.\n"); break;
            case 5: std::cout << (reg.removeLast()  ? "Removed last.\n"  : "Register is empty.\n"); break;
            case 6: { int id = readInt("Id: "); const ReturnRecord* r = reg.findById(id);
                      if (r) std::cout << r->memberName << " returned '" << r->bookTitle << "' on "
                                       << r->returnDate << ", fine " << r->fineUGX() << " UGX\n";
                      else   std::cout << "Not found.\n";
                      break; }
            case 7: reg.printForward(std::cout);  break;
            case 8: reg.printBackward(std::cout); break;
            case 9: sortMenu(reg);   break;
            case 10: searchMenu(reg); break;
            case 11: std::cout << (reg.save(path) ? "Saved.\n" : "Save FAILED.\n"); break;
            case 12: { int sk = 0; int m = reg.load(path, &sk);
                       if (m < 0) std::cout << "Cannot open file.\n";
                       else std::cout << "Reloaded " << m << " (" << sk << " skipped).\n";
                       break; }
            case 0: std::cout << (reg.save(path) ? "Saved. Goodbye.\n" : "Save FAILED. Goodbye.\n"); return 0;
            default: std::cout << "Invalid choice.\n";
        }
    }
}

} // namespace menu_app

// ===================== 4. TESTS (A3 table) =====================
namespace tests {
// tests.cpp - generates the A3 test table (ID | operation | expected | actual | PASS/FAIL)

static const char* FIXTURE = "_returns_test_fixture.txt";
static int passed = 0, failed = 0;
static void row(const char* id, const std::string& op, const std::string& expected, const std::string& actual) {
    bool ok = (expected == actual);
    (ok ? passed : failed)++;
    std::cout << "| " << id << " | " << op << " | " << expected << " | " << actual << " | " << (ok ? "PASS" : "FAIL") << " |\n";
}
static std::string b(bool v) { return v ? "true" : "false"; }
// Braced-init-list elements are evaluated strictly left-to-right (operands of '+' are not),
// so use cat({...}) whenever one operand has a side effect that a later operand observes.
static std::string cat(std::initializer_list<std::string> l) { std::string s; for (auto& x : l) s += x; return s; }

static ReturnRecord mk(int id, const std::string& name = "Member", const std::string& title = "Book",
                       const std::string& due = "2026-10-10", const std::string& ret = "2026-10-12") {
    ReturnRecord r; r.id = id; r.memberName = name; r.bookTitle = title; r.dueDate = due; r.returnDate = ret; return r;
}
static std::string ids(const std::vector<ReturnRecord>& v) {
    std::string s; for (auto& r : v) s += (s.empty() ? "" : ",") + std::to_string(r.id); return s.empty() ? "(none)" : s;
}
static std::string idsFwd(const ReturnsRegister& g) { return ids(g.toVector()); }
static std::string idsBwd(const ReturnsRegister& g) {
    auto v = g.toVector(); std::reverse(v.begin(), v.end()); return ids(v);
}
static bool lessDate(const ReturnRecord& a, const ReturnRecord& b2) { return a.returnDate < b2.returnDate; }
static bool lessId  (const ReturnRecord& a, const ReturnRecord& b2) { return a.id < b2.id; }
static int  cmpDate (const ReturnRecord& r, const std::string& k) { return r.returnDate < k ? -1 : (r.returnDate > k ? 1 : 0); }
static int  cmpId   (const ReturnRecord& r, const int& k) { return r.id < k ? -1 : (r.id > k ? 1 : 0); }

int runTests() {
    writeSampleFile(FIXTURE);
    std::cout << "| Test ID | Input / operation | Expected result | Actual result | Pass/Fail |\n|---|---|---|---|---|\n";

    // ============ A1: list ============
    { ReturnsRegister g;
      row("A1-01", "Empty register: size(), empty(), selfCheck()", "0, true, true", std::to_string(g.size()) + ", " + b(g.empty()) + ", " + b(g.selfCheck()));
      row("A1-02", "removeFirst() on empty", "false", b(g.removeFirst()));
      row("A1-03", "removeLast() on empty", "false", b(g.removeLast()));
      row("A1-04", "removeById(5) on empty", "false", b(g.removeById(5)));
      std::ostringstream os; g.printForward(os); g.printBackward(os);
      row("A1-05", "Print forward/backward on empty", "\"(register is empty)\" twice", os.str() == "(register is empty)\n(register is empty)\n" ? "\"(register is empty)\" twice" : "other output");
      row("A1-06", "findById(1) on empty", "nullptr", g.findById(1) ? "found" : "nullptr");
      // one record
      row("A1-07", "pushBack(id 1) into empty", "true, size 1, list 1", cat({b(g.pushBack(mk(1))), ", size ", std::to_string(g.size()), ", list ", idsFwd(g)}));
      row("A1-08", "removeFirst() with ONE record (head==tail)", "true, size 0, selfCheck true", cat({b(g.removeFirst()), ", size ", std::to_string(g.size()), ", selfCheck ", b(g.selfCheck())}));
      g.pushBack(mk(1));
      row("A1-09", "removeLast() with ONE record", "true, size 0, selfCheck true", cat({b(g.removeLast()), ", size ", std::to_string(g.size()), ", selfCheck ", b(g.selfCheck())}));
      row("A1-10", "pushBack after emptying (reuse)", "true, list 7", cat({b(g.pushBack(mk(7))), ", list ", idsFwd(g)}));
    }
    { ReturnsRegister g;
      for (int i = 1; i <= 5; ++i) g.pushBack(mk(i));
      row("A1-11", "pushBack 1..5, forward order", "1,2,3,4,5", idsFwd(g));
      row("A1-12", "Duplicate key: pushBack(id 3) again", "false, size 5", cat({b(g.pushBack(mk(3))), ", size ", std::to_string(g.size())}));
      row("A1-13", "pushFront(id 0 = invalid id)", "false", b(g.pushFront(mk(0))));
      row("A1-14", "pushBack with empty member name", "false", b(g.pushBack(mk(9, ""))));
      row("A1-15", "pushBack with impossible date 2026-02-30", "false", b(g.pushBack(mk(9, "M", "B", "2026-02-30"))));
      row("A1-16", "pushBack with '|' in title (would corrupt file)", "false", b(g.pushBack(mk(9, "M", "A|B"))));
      row("A1-17", "pushFront(id 10); forward order", "10,1,2,3,4,5", g.pushFront(mk(10)) ? idsFwd(g) : "rejected");
      ReturnRecord out;
      row("A1-18", "Remove FIRST of 6; removed id; list", "true, 10, 1,2,3,4,5", cat({b(g.removeFirst(&out)), ", ", std::to_string(out.id), ", ", idsFwd(g)}));
      row("A1-19", "Remove LAST of 5; removed id; list", "true, 5, 1,2,3,4", cat({b(g.removeLast(&out)), ", ", std::to_string(out.id), ", ", idsFwd(g)}));
      row("A1-20", "Remove MIDDLE (id 2); forward / backward", "true, 1,3,4 / 4,3,1", cat({b(g.removeById(2)), ", ", idsFwd(g), " / ", idsBwd(g)}));
      row("A1-21", "removeById(999) absent key", "false, size 3", cat({b(g.removeById(999)), ", size ", std::to_string(g.size())}));
      row("A1-22", "Links consistent after removals (selfCheck)", "true", b(g.selfCheck()));
    }
    { // deep copy
      ReturnsRegister a; for (int i = 1; i <= 4; ++i) a.pushBack(mk(i, "Orig"));
      ReturnsRegister c(a);
      a.removeById(2); a.removeFirst(); a.pushBack(mk(50, "New"));
      row("A1-23", "Copy ctor: mutate ORIGINAL afterwards; copy unchanged", "copy 1,2,3,4 ; orig 3,4,50", "copy " + idsFwd(c) + " ; orig " + idsFwd(a));
      c.removeLast();
      row("A1-24", "Copy ctor: mutate COPY; original unchanged", "orig 3,4,50 ; copy 1,2,3", "orig " + idsFwd(a) + " ; copy " + idsFwd(c));
      row("A1-25", "Copy shares no nodes (findById addresses differ)", "true", b(a.findById(3) != c.findById(3)));
      ReturnsRegister e; ReturnsRegister ec(e);
      row("A1-26", "Copy of an EMPTY register", "size 0, selfCheck true", "size " + std::to_string(ec.size()) + ", selfCheck " + b(ec.selfCheck()));
      ReturnsRegister d; d.pushBack(mk(99)); d = a;
      row("A1-27", "Copy assignment replaces old contents", "3,4,50", idsFwd(d));
      ReturnsRegister& dr = d; d = dr;
      row("A1-28", "Self-assignment safe", "3,4,50", idsFwd(d));
    }
    { // file I/O
      ReturnsRegister g; int sk = -1;
      int n = g.load(FIXTURE, &sk);
      row("A1-29", "Load sample file returns.txt", "12 loaded, 0 skipped", std::to_string(n) + " loaded, " + std::to_string(sk) + " skipped");
      row("A1-30", "Loaded list consistent and in file order", "1001..1012, selfCheck true", (idsFwd(g) == "1001,1002,1003,1004,1005,1006,1007,1008,1009,1010,1011,1012" ? "1001..1012" : idsFwd(g)) + ", selfCheck " + b(g.selfCheck()));
      int n2 = g.load("no_such_file.txt", &sk);
      row("A1-31", "Load missing file", "-1, list untouched (12)", std::to_string(n2) + ", list untouched (" + std::to_string(g.size()) + ")");
      { std::ofstream f("bad.txt");
        f << "# test\n1|Ann|Book A|2026-10-01|2026-10-02\n"
             "oops line\n2|Bob|Book B|2026-13-01|2026-10-02\n"
             "1|Dup|Book C|2026-10-01|2026-10-02\n"
             "3|Cy||2026-10-01|2026-10-02\n\n4|Di|Book D|2026-10-01|2026-10-05\n"; }
      ReturnsRegister h; int n3 = h.load("bad.txt", &sk);
      row("A1-32", "Load file with 5 bad lines (junk, bad date, dup id, empty title) + 2 good", "2 loaded, 4 skipped", std::to_string(n3) + " loaded, " + std::to_string(sk) + " skipped");
      { std::ofstream f("empty.txt"); }
      ReturnsRegister z; int n4 = z.load("empty.txt", &sk);
      row("A1-33", "Load an empty file", "0 loaded, empty=true", std::to_string(n4) + " loaded, empty=" + b(z.empty()));
      g.removeFirst(); g.removeLast(); g.pushBack(mk(2000, "Zed Z", "Roundtrip", "2026-11-01", "2026-11-05"));
      bool sv = g.save("saved.txt");
      ReturnsRegister r2; int n5 = r2.load("saved.txt");
      row("A1-34", "Save modified list then reload (round trip)", "true, 11 records, identical order", b(sv) + ", " + std::to_string(n5) + " records, " + (idsFwd(g) == idsFwd(r2) ? "identical order" : "DIFFERENT"));
      row("A1-35", "Save to impossible path /nonexistent/x.txt", "false", b(g.save("/nonexistent/x.txt")));
      std::remove("bad.txt"); std::remove("empty.txt"); std::remove("saved.txt");
    }

    // ============ A2: sorting / searching ============
    std::vector<ReturnRecord> e;
    { auto v = e; insertionSort(v, lessDate); mergeSort(e, lessDate); auto q = e; quickSort(q, lessDate);
      row("A2-01", "All three sorts on EMPTY vector", "still empty", (v.empty() && e.empty() && q.empty()) ? "still empty" : "changed"); }
    { std::vector<ReturnRecord> o{mk(1)}, m = o, q = o; insertionSort(o, lessId); mergeSort(m, lessId); quickSort(q, lessId);
      row("A2-02", "All three sorts on ONE record", "1 / 1 / 1", ids(o) + " / " + ids(m) + " / " + ids(q)); }
    auto sortedIn = [] { std::vector<ReturnRecord> v; for (int i = 1; i <= 8; ++i) v.push_back(mk(i)); return v; };
    { auto v = sortedIn(), m = v, q = v; insertionSort(v, lessId); mergeSort(m, lessId); quickSort(q, lessId);
      row("A2-03", "ALREADY SORTED input (1..8), by id", "1..8 for all three", (ids(v) == "1,2,3,4,5,6,7,8" && ids(m) == ids(v) && ids(q) == ids(v)) ? "1..8 for all three" : ids(v) + " | " + ids(m) + " | " + ids(q)); }
    { auto v = sortedIn(); std::reverse(v.begin(), v.end()); auto m = v, q = v;
      insertionSort(v, lessId); mergeSort(m, lessId); quickSort(q, lessId);
      row("A2-04", "REVERSED input (8..1), by id", "1..8 for all three", (ids(v) == "1,2,3,4,5,6,7,8" && ids(m) == ids(v) && ids(q) == ids(v)) ? "1..8 for all three" : ids(v) + " | " + ids(m) + " | " + ids(q)); }
    { ReturnsRegister g; g.load(FIXTURE);
      auto v = g.toVector(), m = v; insertionSort(v, lessDate); mergeSort(m, lessDate);
      row("A2-05", "Sample file by returnDate; DUPLICATE keys (10-22 x4, 10-25 x5): insertion = merge = original order within ties (stable)",
          "1004,1002,1001,1005,1007,1012,1006,1003,1008,1009,1010,1011", (ids(v) == ids(m) ? ids(v) : "differ: " + ids(v) + " | " + ids(m)));
      auto q = g.toVector(); quickSort(q, lessDate);
      bool sortedOk = std::is_sorted(q.begin(), q.end(), lessDate);
      row("A2-06", "Quicksort by returnDate: keys non-decreasing (tie order NOT guaranteed)", "true", b(sortedOk)); }
    { std::mt19937 rng(42); std::vector<ReturnRecord> base;
      for (int i = 1; i <= 2000; ++i) base.push_back(mk(i, "M", "B", "2026-10-10", "2026-10-" + std::to_string(10 + rng() % 20)));
      auto a = base, m = base, q = base, ref = base;
      insertionSort(a, lessDate); mergeSort(m, lessDate); quickSort(q, lessDate); std::stable_sort(ref.begin(), ref.end(), lessDate);
      row("A2-07", "2000 random records, 20 distinct dates: insertion & merge identical to std::stable_sort", "true, true", b(ids(a) == ids(ref)) + ", " + b(ids(m) == ids(ref)));
      row("A2-08", "Same data: quicksort keys sorted & same multiset of ids", "true", b(std::is_sorted(q.begin(), q.end(), lessDate) && [&]{ auto x=q,y=base; std::sort(x.begin(),x.end(),lessId); std::sort(y.begin(),y.end(),lessId); return ids(x)==ids(y);}()));
      row("A2-09", "Quicksort: ALL keys equal (2000 records) finishes (no O(n^2) blow-up / crash)", "true", [&]{ std::vector<ReturnRecord> s(2000, mk(1)); quickSort(s, lessDate); return b(s.size()==2000);}()); }
    // binary search
    { ReturnsRegister g; g.load(FIXTURE); auto v = g.toVector(); mergeSort(v, lessDate);
      auto i1 = binarySearch(v, std::string("2026-10-17"), cmpDate);
      row("A2-10", "Binary search present key 2026-10-17 (first element)", "index 0 -> id 1004", "index " + std::to_string(i1) + " -> id " + std::to_string(i1 >= 0 ? v[i1].id : -1));
      auto i2 = binarySearch(v, std::string("2026-10-25"), cmpDate);
      row("A2-11", "Present key 2026-10-25 (LAST value, 5 duplicates) returns LEFTMOST", "index 7 -> id 1003", "index " + std::to_string(i2) + " -> id " + std::to_string(i2 >= 0 ? v[i2].id : -1));
      auto i3 = binarySearch(v, std::string("2026-10-22"), cmpDate);
      row("A2-12", "Duplicate key 2026-10-22 (4 copies) returns first copy", "index 2 -> id 1001", "index " + std::to_string(i3) + " -> id " + std::to_string(i3 >= 0 ? v[i3].id : -1));
      row("A2-13", "ABSENT key 2026-10-23", "-1", std::to_string(binarySearch(v, std::string("2026-10-23"), cmpDate)));
      row("A2-14", "Absent key smaller than all (2026-01-01)", "-1", std::to_string(binarySearch(v, std::string("2026-01-01"), cmpDate)));
      row("A2-15", "Absent key larger than all (2027-01-01)", "-1", std::to_string(binarySearch(v, std::string("2027-01-01"), cmpDate)));
      std::vector<ReturnRecord> none;
      row("A2-16", "Binary search on EMPTY vector", "-1", std::to_string(binarySearch(none, std::string("2026-10-17"), cmpDate)));
      std::vector<ReturnRecord> one{mk(5)};
      row("A2-17", "Binary search ONE record: id 5 present / id 6 absent", "0 / -1", std::to_string(binarySearch(one, 5, cmpId)) + " / " + std::to_string(binarySearch(one, 6, cmpId)));
      std::vector<ReturnRecord> big; for (int i = 1; i <= 1000; ++i) big.push_back(mk(i * 2));
      bool all = true; for (int i = 1; i <= 1000; ++i) all &= (binarySearch(big, i * 2, cmpId) == i - 1);
      bool none2 = true; for (int i = 0; i <= 1000; ++i) none2 &= (binarySearch(big, i * 2 + 1, cmpId) == -1);
      row("A2-18", "1000 sorted ids: every present key found at right index; every absent (odd) key = -1", "true, true", b(all) + ", " + b(none2));
      // round-trip: sort list through register
      ReturnsRegister h; h.load(FIXTURE); auto hv = h.toVector(); mergeSort(hv, lessDate);
      bool rep = h.replaceWith(hv);
      row("A2-19", "Sort via register.toVector() -> mergeSort -> replaceWith(): list order & links", "true, 1004 first, 1011 last, selfCheck true",
          b(rep) + ", " + std::to_string(h.toVector().front().id) + " first, " + std::to_string(h.toVector().back().id) + " last, selfCheck " + b(h.selfCheck())); }

    std::remove(FIXTURE);
    std::cout << "\nTOTAL: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

} // namespace tests

// ===================== 5. TIMING EXPERIMENT (A4) =====================
namespace benchmark {
// bench.cpp - A4 timing experiment. Build with optimisation:  g++ -O2 -std=c++17 bench.cpp register.cpp -o bench
// Sorts n random ReturnRecords (unique random ids) by id; 3 repeats per size; prints averages + doubling ratios as CSV.

static bool lessId(const ReturnRecord& a, const ReturnRecord& b) { return a.id < b.id; }

static std::vector<ReturnRecord> randomRecords(std::size_t n, std::mt19937& rng) {
    std::vector<int> perm(n);
    std::iota(perm.begin(), perm.end(), 1);
    std::shuffle(perm.begin(), perm.end(), rng);                 // unique random keys
    std::vector<ReturnRecord> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i].id = perm[i];
        v[i].memberName = "Member " + std::to_string(perm[i] % 977);
        v[i].bookTitle  = "Book "   + std::to_string(perm[i] % 463);
        v[i].dueDate    = "2026-10-10";
        v[i].returnDate = "2026-10-" + std::to_string(10 + perm[i] % 20);
    }
    return v;
}

template <typename Sorter>
double timeOnce(const std::vector<ReturnRecord>& base, Sorter sorter) {
    auto v = base;                                               // copy outside the timed region
    auto t0 = std::chrono::steady_clock::now();
    sorter(v);
    auto t1 = std::chrono::steady_clock::now();
    if (!std::is_sorted(v.begin(), v.end(), lessId)) { std::cerr << "SORT BROKEN\n"; std::exit(1); }
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int runBench() {
    std::mt19937 rng(12345);
    std::printf("n,insertion_ms,insertion_ratio,merge_ms,merge_ratio,quick_ms,quick_ratio\n");
    double pi = 0, pm = 0, pq = 0;
    for (std::size_t n = 1000; n <= 64000; n *= 2) {
        double si = 0, sm = 0, sq = 0;
        for (int rep = 0; rep < 3; ++rep) {
            auto base = randomRecords(n, rng);                   // fresh random data each repeat
            si += timeOnce(base, [](auto& v) { insertionSort(v, lessId); });
            sm += timeOnce(base, [](auto& v) { mergeSort(v, lessId); });
            sq += timeOnce(base, [](auto& v) { quickSort(v, lessId); });
        }
        si /= 3; sm /= 3; sq /= 3;
        std::printf("%zu,%.3f,%.2f,%.3f,%.2f,%.3f,%.2f\n", n, si, pi ? si / pi : 0.0, sm, pm ? sm / pm : 0.0, sq, pq ? sq / pq : 0.0);
        std::fflush(stdout);
        pi = si; pm = sm; pq = sq;
    }
    return 0;
}

} // namespace benchmark

// ============================== 6. main() ==============================
int main(int argc, char** argv) {
    std::string arg = argc > 1 ? argv[1] : "";
    if (arg == "test")  return tests::runTests();
    if (arg == "bench") return benchmark::runBench();
    std::string path = arg.empty() ? "returns.txt" : arg;
    if (!fileExists(path)) {                       // first run: create the sample file
        writeSampleFile(path);
        std::cout << "Created sample data file '" << path << "'.\n";
    }
    return menu_app::runMenu(path);
}