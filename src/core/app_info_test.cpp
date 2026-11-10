#include <ostream>
#include <doctest/doctest.h>
#include <string_view>
import oryn.core;

TEST_CASE("Application identity") {
    CHECK(std::string_view{oryn::core::app_name()} == "Oryn");
}
