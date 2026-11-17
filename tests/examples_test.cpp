// Golden tests: every program in examples/ has a matching .expected file with
// the output it must produce.
//
// Each program is run twice, with the optimizer off and on. Requiring both runs
// to match the same expected output is a differential test: any optimization
// that changes what a program does gets caught here, even for behaviour nobody
// thought to write a unit test for.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "test_util.h"

namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::vector<fs::path> examplePrograms() {
    std::vector<fs::path> programs;
    for (const auto& entry : fs::directory_iterator(SPRIG_EXAMPLES_DIR)) {
        if (entry.path().extension() == ".sprig") programs.push_back(entry.path());
    }
    std::sort(programs.begin(), programs.end());
    return programs;
}

}  // namespace

TEST(Examples, DirectoryIsNotEmpty) {
    // Guards against the path being wrong, which would make the test below pass by doing nothing.
    EXPECT_GE(examplePrograms().size(), 5u);
}

TEST(Examples, OutputMatchesExpectedWithAndWithoutOptimizer) {
    for (const fs::path& program : examplePrograms()) {
        SCOPED_TRACE(program.filename().string());

        fs::path expectedPath = program;
        expectedPath.replace_extension(".expected");
        ASSERT_TRUE(fs::exists(expectedPath)) << "missing " << expectedPath.filename().string();

        const std::string source = readFile(program);
        const std::string expected = readFile(expectedPath);
        EXPECT_EQ(sprig::testing::run(source, false), expected) << "with -O0";
        EXPECT_EQ(sprig::testing::run(source, true), expected) << "with -O1";
    }
}
