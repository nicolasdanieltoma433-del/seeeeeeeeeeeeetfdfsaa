#include "LiveLog.hpp"

#include <deque>

namespace ts::livelog {
namespace {
std::deque<std::string>& buffer() {
    static std::deque<std::string> b;
    return b;
}
} // namespace

void push(std::string line) {
    auto& b = buffer();
    b.push_back(std::move(line));
    while (b.size() > kCapacity) b.pop_front();
}

std::vector<std::string> lines() { return {buffer().begin(), buffer().end()}; }

std::string joined() {
    std::string out;
    for (auto const& line : buffer()) {
        out += line;
        out += '\n';
    }
    return out;
}

void clear() { buffer().clear(); }

} // namespace ts::livelog
