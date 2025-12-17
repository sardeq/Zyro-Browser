#pragma once
#include <string>
#include <vector>

void init_security();
std::string encrypt_string(const std::string& plain);
std::string decrypt_string(const std::string& hex_cipher);
std::string hex_encode(const unsigned char* data, int len);
std::vector<unsigned char> hex_decode(const std::string& input);