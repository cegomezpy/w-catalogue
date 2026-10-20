#include "doctest.h"

#include <string>

#include "runtime/app_info.hpp"

TEST_CASE("app_version devuelve la versión del proyecto") {
    CHECK(std::string(wcatalog::app_version()) == "0.1.0");
}
