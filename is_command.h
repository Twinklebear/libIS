#pragma once

#include <ostream>

namespace is {
// Internal command flags sent from client to sim
enum COMMAND { INVALID, CONNECT, QUERY, DISCONNECT };
}

inline std::ostream &operator<<(std::ostream &os, const is::COMMAND &cmd)
{
    switch (cmd) {
    case is::INVALID:
        os << "INVALID";
        break;
    case is::CONNECT:
        os << "CONNECT";
        break;
    case is::QUERY:
        os << "QUERY";
        break;
    case is::DISCONNECT:
        os << "DISCONNECT";
        break;
    default:
        break;
    }
    return os;
}
