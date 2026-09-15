/*//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Copyright European Organization for Nuclear Research (CERN)
Licensed under the Apache License, Version 2.0 (the "License");
You may not use this file except in compliance with the License.
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
Authors:
 - Gabriele Gaetano Fronzé, <gfronze@cern.ch>, 2019-2020
/*//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <utility>

#ifndef RUCIO_FUSE_CONNNECTION_PARAMETERS_H
#define RUCIO_FUSE_CONNNECTION_PARAMETERS_H

#include <constants.h>
#include <curl/curl.h>
#include <string>
#include <string.h>
#include <time.h>
#include <unordered_map>
#include <vector>
#include <utility>
#include <map>
#include "curl-REST.h"
#include <nlohmann/json.hpp>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enumerator to define supported enum types
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
enum auth_mode{
    userpass,
    x509,
    oidc, 
    none
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Methods to retrieve selected auth method from settings string
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
auth_mode get_auth_mode(const std::string& settings_line);
std::string get_auth_name(auth_mode mode);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Connection parameters struct definition
// Contains all the parameteres defining how to contact a rucio server
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct connection_parameters{
  std::string server_url;
  std::string account_name;
  std::string user_name;
  std::string password;
  auth_mode rucio_auth_mode;
  std::string ca_path;

  connection_parameters(std::string server_url,
                        std::string account_name,
                        std::string user_name,
                        std::string password,
                        std::string ca_path,
                        const std::string& auth_method_line = "userpass"):
                        server_url(std::move(server_url)),
                        account_name(std::move(account_name)),
                        user_name(std::move(user_name)),
                        password(std::move(password)),
                        ca_path(std::move(ca_path)),
                        rucio_auth_mode(get_auth_mode(auth_method_line)){}
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Token info struct definition
// Contains token string for REST calls and toke expire information
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct token_info{
  std::string conn_token = rucio_invalid_token;
  tm conn_token_exp;
  time_t conn_token_exp_epoch = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rucio server descriptor
// Packs together all the aboves structs
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct rucio_server{
  connection_parameters rucio_conn_params;
  std::map<uid_t, token_info> rucio_token_infos;
  std::string config_file_path;
  std::string temp_config_folder;

  rucio_server():rucio_conn_params("","","","",""), rucio_token_infos(){};

  rucio_server(std::string server_url,
               std::string account_name,
               std::string user_name,
               std::string password,
               std::string ca_path,
               const std::string& auth_mode):
               rucio_conn_params(std::move(server_url),
                                 std::move(account_name),
                                 std::move(user_name),
                                 std::move(password),
                                 std::move(ca_path),
                                 auth_mode),
               rucio_token_infos(){}

  connection_parameters* get_params(){ return &rucio_conn_params; };
  token_info* get_token(uid_t uid){ return &(rucio_token_infos[uid]); };
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Server descriptors cache
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
extern std::unordered_map<std::string, rucio_server>  rucio_server_map;
extern std::vector<std::string> rucio_server_names;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Server and scope existance utilities
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool server_exists(const std::string &key);
bool scope_exists(const std::string &server_name, const std::string &scope, uid_t uid, pid_t calling_pid, std::string username);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Methods to get server configs and params. Wrapped around the caches to protect against non existing servers.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
connection_parameters* get_server_params(const std::string& server_name);
std::string* get_server_config(const std::string& server_name);
std::string* get_temp_config_folder(const std::string& server_name);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Methods to retrieve authentication information
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
curlx509Bundle* get_server_SSL_bundle(const std::string& server_name);
curlOIDCBundle* get_server_OIDC_bundle(const std::string& server_name);
token_info* get_server_token(const std::string& server_name, uid_t uid);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Startup methods. Configuration file parser and permission checker.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void parse_settings_cfg(uid_t uid, pid_t calling_pid, std::string username, std::string ruciofs_settings_root = "./rucio-settings");
bool check_permissions(const std::string& mountpoint_path);


// --- Base64 decoding table ---
static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

// --- Base64URL decode ---
inline std::string base64url_decode(std::string input) {
    // Convert URL-safe → standard Base64
    std::replace(input.begin(), input.end(), '-', '+');
    std::replace(input.begin(), input.end(), '_', '/');

    // Add padding
    while (input.size() % 4) input += '=';

    std::string output;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[base64_chars[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            output.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return output;
}
// --- Extract exp ---
inline std::int64_t get_token_expiry(const std::string& jwt) {
    auto first_dot = jwt.find('.');
    auto second_dot = jwt.find('.', first_dot + 1);

    if (first_dot == std::string::npos || second_dot == std::string::npos) {
        throw std::runtime_error("Invalid JWT format");
    }

    std::string payload = jwt.substr(first_dot + 1, second_dot - first_dot - 1);
    std::string decoded = base64url_decode(payload);

    auto json = nlohmann::json::parse(decoded);
    return json["exp"].get<std::int64_t>();
}

#include <filesystem>
#include <fstream>
#include <stdexcept>

inline std::string copyConfig(const std::filesystem::path& source,
                     const std::string destination_folder,
                     const std::string& user)
{
    std::filesystem::path destination =
        destination_folder +
        (source.stem().string() + "_" + user + source.extension().string());

    if (std::filesystem::exists(destination)) {
        return destination.string();
    }

    if (!std::filesystem::copy_file(
            source,
            destination,
            std::filesystem::copy_options::overwrite_existing))
    {
        throw std::runtime_error("Failed to copy file");
    }

    std::ofstream out(destination, std::ios::app);
    if (!out) {
        throw std::runtime_error("Failed to open destination file");
    }
    std::string token_path = destination_folder + user + ".token";

    out << '\n' << "auth_token_file_path = " << token_path << '\n';

    return destination.string();
}

#endif //RUCIO_FUSE_CONNNECTION_PARAMETERS_H
