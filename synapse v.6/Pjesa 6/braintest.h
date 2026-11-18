#ifndef BRAINTEST_H
#define BRAINTEST_H

#include "clinic.h"
#include <memory>
#include <string>
#include <vector>

// Every test number lives here.
static const int REACTION_ROUNDS = 5;
static const int WAIT_MIN_MS = 1000;
static const int WAIT_MAX_MS = 3000;
static const int TOO_SOON_MS = 100;
static const int MEMORY_FIRST_ROW = 3;
static const int COLOUR_ROUNDS = 10;
static const int COLOUR_MIN_DIFFERENT = 7;
static const int COLOUR_PENALTY_SECONDS = 2;

// The clinic runs every test the same way: it asks for its title, plays it
// and gets one number back. It never needs to know which test it is.
class BrainTest
{
public:
    virtual ~BrainTest() {}

    virtual TestKind kind() const = 0;
    virtual std::string title() const = 0;

    // False when the keyboard input ended before the test finished.
    virtual bool run(double& result) = 0;
};

class ReactionTest : public BrainTest
{
public:
    TestKind kind() const override { return TestKind::Reaction; }
    std::string title() const override;
    bool run(double& result) override;
};

class MemoryTest : public BrainTest
{
public:
    TestKind kind() const override { return TestKind::Memory; }
    std::string title() const override;
    bool run(double& result) override;
};

class ColorWordTest : public BrainTest
{
public:
    TestKind kind() const override { return TestKind::ColorWord; }
    std::string title() const override;
    bool run(double& result) override;
};

std::vector<std::unique_ptr<BrainTest>> allBrainTests();

#endif // BRAINTEST_H
