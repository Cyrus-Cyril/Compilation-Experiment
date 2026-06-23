#pragma once

#include <memory>
#include <string>

namespace toyc {

class CompUnit;

std::unique_ptr<CompUnit> parseString(const std::string& input);
std::unique_ptr<CompUnit> parseStdin();
const std::string& lastParseError();

}  // namespace toyc
