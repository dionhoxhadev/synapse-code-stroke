#include "braintest.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// steady_clock only moves forward and is not changed by the computer's clock,
// so it is the one to use for measuring a short time.
typedef std::chrono::steady_clock Clock;

static const std::string INK_RESET = "\033[0m";

struct Colour
{
    std::string word;
    char letter;
    std::string ink;      // the terminal code that prints in this colour
};

static const std::vector<Colour>& colours()
{
    static const std::vector<Colour> list = {
        {"RED", 'r', "\033[91m"},
        {"BLUE", 'b', "\033[94m"},
        {"GREEN", 'g', "\033[92m"},
        {"YELLOW", 'y', "\033[93m"},
    };
    return list;
}

// Seeded from the clock, so every visit gets different numbers.
static std::mt19937& randomNumbers()
{
    static std::mt19937 generator((unsigned)Clock::now().time_since_epoch().count());
    return generator;
}

static int randomBetween(int low, int high)
{
    return std::uniform_int_distribution<int>(low, high)(randomNumbers());
}

static void waitMilliseconds(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// The Windows console prints colour codes only after being asked to.
static void enableColours()
{
#ifdef _WIN32
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(console, &mode))
        SetConsoleMode(console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

std::string ReactionTest::title() const
{
    return "reaction time   (" + std::to_string(REACTION_ROUNDS) + " rounds)";
}

bool ReactionTest::run(double& result)
{
    std::cout << "Press ENTER as soon as you see *\n";

    int total = 0;
    int round = 0;
    while (round < REACTION_ROUNDS) {
        std::cout << "Wait..." << std::endl;
        waitMilliseconds(randomBetween(WAIT_MIN_MS, WAIT_MAX_MS));

        std::cout << "*" << std::endl;
        Clock::time_point shown = Clock::now();
        std::string line;
        if (!std::getline(std::cin, line)) return false;
        int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - shown).count();

        // An ENTER pressed during the wait is already waiting here, so it lands under the limit.
        if (ms < TOO_SOON_MS) {
            std::cout << "Too soon!\n";
            continue;
        }
        std::cout << ms << " ms\n";
        total += ms;
        ++round;
    }

    result = (total + REACTION_ROUNDS / 2) / REACTION_ROUNDS;     // rounded to the nearest ms
    std::cout << "Average: " << (int)result << " ms\n";
    return true;
}

static std::string randomRow(int length)
{
    std::string row;
    while ((int)row.size() < length) {
        char digit = (char)('0' + randomBetween(0, 9));
        if (row.empty() || row.back() != digit) row += digit;
    }
    return row;
}

// The digits stay on one line, so writing spaces over that line hides them.
static void showThenHide(const std::string& row)
{
    std::string shown;
    for (char digit : row) {
        shown += digit;
        shown += ' ';
    }
    std::cout << shown << std::flush;
    waitMilliseconds(1000 * (int)row.size());
    std::cout << "\r" << std::string(shown.size(), ' ') << "\r" << std::flush;
}

static std::string withoutSpaces(std::string text)
{
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](unsigned char c) { return std::isspace(c) != 0; }),
               text.end());
    return text;
}

std::string MemoryTest::title() const
{
    return "memory span";
}

bool MemoryTest::run(double& result)
{
    std::cout << "Remember the digits. They disappear after a moment.\n";

    int longest = 0;
    for (int length = MEMORY_FIRST_ROW; length <= MAX_MEMORY_DIGITS; ++length) {
        std::string row = randomRow(length);
        showThenHide(row);

        // An empty line is a stray ENTER, not an answer.
        std::string answer;
        while (answer.empty()) {
            std::cout << "Digits: " << std::flush;
            if (!std::getline(std::cin, answer)) return false;
            answer = withoutSpaces(answer);
        }
        if (answer != row) break;
        longest = length;
    }

    result = longest;
    std::cout << "Longest row repeated: " << longest << (longest == 1 ? " digit\n" : " digits\n");
    return true;
}

// Returns the letter typed, or nothing if the input ended.
static char askColourLetter()
{
    while (true) {
        std::cout << "> " << std::flush;
        std::string answer;
        if (!std::getline(std::cin, answer)) return 0;
        answer = toLower(trim(answer));
        if (answer.size() == 1)
            for (const auto& colour : colours())
                if (colour.letter == answer[0]) return answer[0];
        std::cout << "Type r, b, g or y.\n";
    }
}

std::string ColorWordTest::title() const
{
    return "colour words";
}

bool ColorWordTest::run(double& result)
{
    enableColours();
    std::cout << "Type the first letter of the COLOUR of each word, not the word: r, b, g or y.\n";

    // At least this many rounds must differ. The others are left to chance.
    std::vector<int> mustDiffer(COLOUR_ROUNDS, 0);
    std::fill(mustDiffer.begin(), mustDiffer.begin() + COLOUR_MIN_DIFFERENT, 1);
    std::shuffle(mustDiffer.begin(), mustDiffer.end(), randomNumbers());

    const int count = (int)colours().size();
    int wrong = 0;
    Clock::time_point start = Clock::now();

    for (int round = 0; round < COLOUR_ROUNDS; ++round) {
        int word = randomBetween(0, count - 1);
        int ink = randomBetween(0, count - 1);
        if (mustDiffer[round]) ink = (word + randomBetween(1, count - 1)) % count;

        std::cout << colours()[ink].ink << colours()[word].word << INK_RESET << std::endl;
        char letter = askColourLetter();
        if (!letter) return false;
        if (letter != colours()[ink].letter) ++wrong;
    }

    double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count() / 1000.0;
    seconds = oneDecimal(seconds);
    result = seconds + wrong * COLOUR_PENALTY_SECONDS;

    std::cout << std::fixed << std::setprecision(1) << "Time: " << seconds << " s, " << wrong
              << (wrong == 1 ? " wrong answer" : " wrong answers");
    if (wrong > 0) std::cout << " (+" << wrong * COLOUR_PENALTY_SECONDS << " s)";
    std::cout << " = " << result << " s\n" << std::defaultfloat;
    return true;
}

std::vector<std::unique_ptr<BrainTest>> allBrainTests()
{
    std::vector<std::unique_ptr<BrainTest>> tests;
    tests.emplace_back(new ReactionTest);
    tests.emplace_back(new MemoryTest);
    tests.emplace_back(new ColorWordTest);
    return tests;
}
