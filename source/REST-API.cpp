/*//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Copyright European Organization for Nuclear Research (CERN)
Licensed under the Apache License, Version 2.0 (the "License");
You may not use this file except in compliance with the License.
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
Authors:
 - Gabriele Gaetano Fronzé, <gfronze@cern.ch>, 2019-2020
 - Vivek Nigam <viveknigam.nigam3@gmail.com>, 2020
/*//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <REST-API.h>
#include <curl-REST.h>
#include <globals.h>
#include <utils.h>
#include <iostream>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

using namespace fastlog;

bool rucio_ping(const std::string& short_server_name){
  auto conn_params = get_server_params(short_server_name);

  auto curl_res = safeGET(conn_params->server_url+"/ping", conn_params->ca_path);
  return curl_res.res == CURLE_OK;
}

void* GET_OIDC_wrapper(void* args)
{
  int pid = fork();
  void** argarray = reinterpret_cast<void**>(args);
  curlOIDCBundle* bundle = reinterpret_cast<curlOIDCBundle*>(argarray[0]);

  uid_t* uid = reinterpret_cast<uid_t*>(argarray[1]);
  pid_t* calling_pid = reinterpret_cast<pid_t*>(argarray[2]);
  std::string* username = reinterpret_cast<std::string*>(argarray[3]);
  token_info* token_info_p = reinterpret_cast<token_info*>(argarray[4]);
  std::string* short_server_name = reinterpret_cast<std::string*>(argarray[5]);
  std::string token = GET_OIDC(*bundle, *uid, *calling_pid, *username);
  if (token.length() == 0)
  {
    pthread_exit(NULL);
  }
  long exp = get_token_expiry(token);
  char localTimeString[80];
  char utcTimeString[80];
  strftime(localTimeString, sizeof(localTimeString), "%a %Y-%m-%d %H:%M:%S %Z", localtime(&exp));
  strftime(utcTimeString, sizeof(utcTimeString), "%a %Y-%m-%d %H:%M:%S %Z", gmtime(&exp));

  fastlog(INFO, "Fetched token for %s", short_server_name->data());
  fastlog(INFO,"Expiration (epoch):  %i", exp);
  fastlog(INFO,"Expiration (human):  %s", localTimeString);
  fastlog(INFO,"Expiration UTC (human)  %s", utcTimeString);

  token_info_p->conn_token = (strlen(token.data())>0) ? token : rucio_invalid_token;
  token_info_p->conn_token_exp = *gmtime(&exp);
  token_info_p->conn_token_exp_epoch = exp;
  pthread_exit(NULL);
}

std::string GET_OIDC_ASYNC(curlOIDCBundle& bundle, uid_t uid, pid_t calling_pid, std::string username, std::string short_server_name, token_info* token_info) {
  fastlog(INFO, "Spliting of process for user %s (uid %d, pid %d)", 
          username.c_str(), uid, calling_pid);
  pid_t pid; 
  void* args[6] = {&bundle, &uid, &calling_pid, &username, token_info, &short_server_name};
  pthread_t child;
  int result = pthread_create(&child, NULL, GET_OIDC_wrapper, args);
  if (result) {
    fastlog(ERROR, "return code from pthread_create() is %d\n", result);
  }

  return "";
}


int rucio_get_auth_token_oidc(const std::string& short_server_name, uid_t uid, pid_t calling_pid, std::string username, bool async = true){
  
  auto conn_params = get_server_params(short_server_name);

  if(not conn_params){
    fastlog(ERROR,"Server %s not found. Aborting!", short_server_name.data());
    return SERVER_NOT_LOADED;
  }

  curlOIDCBundle* bundle = get_server_OIDC_bundle(short_server_name);
  std::string token;
  if (async){
    token = GET_OIDC_ASYNC(*bundle, uid, calling_pid, username, short_server_name, get_server_token(short_server_name, uid));
  }
  else {
    token = GET_OIDC(*bundle, uid, calling_pid, username);
  }
  if (token.length() == 0)
  {
    return TOKEN_ERROR;
  }

  auto token_info = get_server_token(short_server_name, uid);

  if(not token_info){
    fastlog(ERROR,"Server %s didn't provide token. Aborting!", short_server_name.data());
    return TOKEN_ERROR;
  }

  long exp = get_token_expiry(token);
  char localTimeString[80];
  char utcTimeString[80];
  strftime(localTimeString, sizeof(localTimeString), "%a %Y-%m-%d %H:%M:%S %Z", localtime(&exp));
  strftime(utcTimeString, sizeof(utcTimeString), "%a %Y-%m-%d %H:%M:%S %Z", gmtime(&exp));

  fastlog(INFO, "Fetched token for %s", short_server_name.data());
  fastlog(INFO,"Expiration (epoch):  %i", exp);
  fastlog(INFO,"Expiration (human):  %s", localTimeString);
  fastlog(INFO,"Expiration UTC (human)  %s", utcTimeString);

  token_info->conn_token = (strlen(token.data())>0) ? token : rucio_invalid_token;

  token_info->conn_token_exp = *gmtime(&exp);
  token_info->conn_token_exp_epoch = exp;

  return TOKEN_OK;
}

int rucio_get_auth_token_root(const std::string& short_server_name, uid_t uid, pid_t calling_pid, std::string username){

  auto conn_params = get_server_params(short_server_name);

  switch (conn_params->rucio_auth_mode){
    case auth_mode::userpass: return rucio_get_auth_token_userpass(short_server_name, uid, calling_pid, username);
    case auth_mode::x509: return rucio_get_auth_token_x509(short_server_name, uid, calling_pid, username);
    case auth_mode::oidc: return rucio_get_auth_token_oidc(short_server_name, uid, calling_pid, username, false);
    default: return TOKEN_ERROR;
  }
}

int rucio_get_auth_token(const std::string& short_server_name, uid_t uid, pid_t calling_pid, std::string username){

  auto conn_params = get_server_params(short_server_name);

  switch (conn_params->rucio_auth_mode){
    case auth_mode::userpass: return rucio_get_auth_token_userpass(short_server_name, uid, calling_pid, username);
    case auth_mode::x509: return rucio_get_auth_token_x509(short_server_name, uid, calling_pid, username);
    case auth_mode::oidc: return rucio_get_auth_token_oidc(short_server_name, uid, calling_pid, username);
    default: return TOKEN_ERROR;
  }
}

int rucio_get_auth_token_userpass(const std::string& short_server_name, uid_t uid, pid_t calling_pid, std::string username){

  struct curl_slist *headers = nullptr;

  auto conn_params = get_server_params(short_server_name);

  if(not conn_params){
    fastlog(ERROR,"Server %s not found. Aborting!", short_server_name.data());
    return SERVER_NOT_LOADED;
  }

  auto xRucioAccount = "X-Rucio-Account: "+conn_params->account_name;
  auto xRucioUsername = "X-Rucio-Username: "+conn_params->user_name;
  auto xRucioPwd = "X-Rucio-Password: "+conn_params->password;

  headers= curl_slist_append(headers, xRucioAccount.data());
  headers= curl_slist_append(headers, xRucioUsername.data());
  headers= curl_slist_append(headers, xRucioPwd.data());

  auto curl_res = safeGET(conn_params->server_url+"/auth/userpass", conn_params->ca_path, headers, true);
  if(curl_res.res != CURLE_OK){
    fastlog(ERROR, "Token: Curl error. Abort.");
    return CURL_ERROR;
  }

  curl_slist_free_all(headers);

  std::string token;
  std::string expire_time_string;

  for(auto& line : curl_res.payload){
    if (line.find(rucio_token_exception_prefix) != std::string::npos) {
      fastlog(ERROR, "Wrong authentication parameters for server %s!",short_server_name.data());
      return CANNOT_AUTH;
    }

    if (line.find(rucio_token_prefix) != std::string::npos) {
      token = line;
      token.erase(0, rucio_token_prefix_size);
    }

    if (line.find(rucio_token_duration_prefix) != std::string::npos) {
      expire_time_string = line;
      expire_time_string.erase(0, rucio_token_duration_prefix_size);
    }
  }

  auto token_info = get_server_token(short_server_name, uid);

  if(not token_info){
    fastlog(ERROR,"Server %s didn't provide token. Aborting!", short_server_name.data());
    return TOKEN_ERROR;
  }

  token_info->conn_token = (strlen(token.data())>0) ? token : rucio_invalid_token;

  expire_time_string = (strlen(expire_time_string.data())>0) ? expire_time_string : rucio_default_exp;
  strptime(expire_time_string.data(), "%a, %d %b %Y %H:%M:%S",&token_info->conn_token_exp);
  char UTC[] = {'U','T','C'};
  token_info->conn_token_exp.tm_zone = UTC;
  token_info->conn_token_exp_epoch = mktime(&token_info->conn_token_exp) - timezone;

  return TOKEN_OK;
}

int rucio_get_auth_token_x509(const std::string& short_server_name, uid_t uid, pid_t calling_pid, std::string username){

  struct curl_slist *headers = nullptr;

  auto conn_params = get_server_params(short_server_name);

  if(not conn_params){
    fastlog(ERROR,"Server %s not found. Aborting!", short_server_name.data());
    return SERVER_NOT_LOADED;
  }

  auto xRucioAccount = "X-Rucio-Account: "+conn_params->account_name;

  curlx509Bundle* bundle = get_server_SSL_bundle(short_server_name);

  headers = curl_slist_append(headers, xRucioAccount.data());

  auto curl_res = GET_x509(conn_params->server_url + "/auth/x509", *bundle, headers, true);
  if(curl_res.res != CURLE_OK){
    fastlog(ERROR, "Token x509: Curl error. Abort.");
    return CURL_ERROR;
  }


  curl_slist_free_all(headers);

  std::string token;
  std::string expire_time_string;

  for(auto& line : curl_res.payload){
    if (line.find(rucio_token_exception_prefix) != std::string::npos) {
      fastlog(ERROR, "Wrong authentication parameters for server %s! Aborting!",short_server_name.data());
      return CANNOT_AUTH;
    }

    if (line.find(rucio_token_prefix) != std::string::npos) {
      token = line;
      token.erase(0, rucio_token_prefix_size);
    }

    if (line.find(rucio_token_duration_prefix) != std::string::npos) {
      expire_time_string = line;
      expire_time_string.erase(0, rucio_token_duration_prefix_size);
    }
  }

  auto token_info = get_server_token(short_server_name, uid);

  if(not token_info){
    fastlog(ERROR,"Server %s didn't provide token. Aborting!", short_server_name.data());
    return TOKEN_ERROR;
  }

  token_info->conn_token = (strlen(token.data())>0) ? token : rucio_invalid_token;

  expire_time_string = (strlen(expire_time_string.data())>0) ? expire_time_string : rucio_default_exp;
  strptime(expire_time_string.data(), "%a, %d %b %Y %H:%M:%S",&token_info->conn_token_exp);
  char UTC[] = {'U','T','C'};
  token_info->conn_token_exp.tm_zone = UTC;
  token_info->conn_token_exp_epoch = mktime(&token_info->conn_token_exp) - timezone;

  return TOKEN_OK;
}

bool rucio_is_token_valid(const std::string& short_server_name, uid_t uid){
  auto token_info = get_server_token(short_server_name, uid);

  if(not token_info){
    fastlog(ERROR,"Server %s not found. Aborting!", short_server_name.data());
    return false;
  }

  return token_info->conn_token != rucio_invalid_token && difftime(token_info->conn_token_exp_epoch, time(nullptr)) >= 0;
}

const std::vector<std::string>& rucio_list_servers(){
  return rucio_server_names;
}

std::vector<std::string> rucio_list_scopes(const std::string& short_server_name, uid_t uid, pid_t calling_pid, std::string username){
  auto found = scopes_cache.find(short_server_name);
  time_t time_now;
  time(&time_now);

  fastlog(INFO, "Fetching %s", short_server_name.data());
  if(found == scopes_cache.end()){
    fastlog(DEBUG, "rucio_list_scopes: Not in cache");}
  else if(found->second.first < time_now){
    fastlog(DEBUG, "rucio_list_scopes: Timed out");}
  else
    fastlog(DEBUG, "rucio_list_scopes: Using cache");

  if(found == scopes_cache.end() || found->second.first < time_now) {
    auto conn_params = get_server_params(short_server_name);
    auto token_info = get_server_token(short_server_name, uid);

    if (not token_info || not conn_params) {
      fastlog(ERROR,"Server %s not found. Aborting!", short_server_name.data());
      return {};
    }

    if (not rucio_is_token_valid(short_server_name, uid)) rucio_get_auth_token(short_server_name, uid, calling_pid, username);

    auto xRucioToken = "X-Rucio-Auth-Token: " + token_info->conn_token;

    struct curl_slist *headers = nullptr;

    headers = curl_slist_append(headers, xRucioToken.data());

    auto curl_res = safeGET(conn_params->server_url + "/scopes/", conn_params->ca_path, headers);
    if(curl_res.res != CURLE_OK){
      fastlog(ERROR, "Scopes: Curl error. Abort.");
      return {};
    }


    curl_slist_free_all(headers);

    std::vector<std::string> scopes;

    fastlog(DEBUG, "return value");
    for (auto &line : curl_res.payload) {
      fastlog(DEBUG, "%s", line.data());
      tokenize_python_list(line, scopes);
    }

    scopes_cache.set_value(short_server_name, std::pair(time_now + chache_duration, std::move(scopes)));
    return scopes_cache[short_server_name].second;
  } else {
    fastlog(DEBUG,"USING CACHE");
    return found->second.second;
  }
}

curl_slist* get_auth_headers(const std::string& short_server_name, uid_t uid, pid_t calling_pid, std::string username){
  auto conn_params = get_server_params(short_server_name);
  auto token_info = get_server_token(short_server_name, uid);

  if(not token_info || not conn_params){
    fastlog(ERROR,"Server %s not found. Aborting!", short_server_name.data());
    return nullptr;
  }

  if(not rucio_is_token_valid(short_server_name, uid)) rucio_get_auth_token(short_server_name, uid, calling_pid, username);

  auto xRucioToken = "X-Rucio-Auth-Token: "+token_info->conn_token;

  struct curl_slist *headers = nullptr;

  headers= curl_slist_append(headers, xRucioToken.data());

  return headers;
}

std::vector<rucio_did> rucio_list_dids(const std::string& scope, const std::string& short_server_name, uid_t uid, pid_t calling_pid, std::string username){
  auto conn_params = get_server_params(short_server_name);
  auto key = short_server_name+scope;
  auto found = dids_cache.find(key);
  time_t time_now;
  time(&time_now);
  fastlog(INFO, "Fetching %s", key.data());
  if(found == dids_cache.end()){
    fastlog(DEBUG, "rucio_list_dids: Not in cache");}
  else if(found->second.first < time_now){
    fastlog(DEBUG, "rucio_list_dids: Timed out");}
  else
    fastlog(DEBUG, "rucio_list_dids: Using cache");

  if(found == dids_cache.end() || found->second.first < time_now) {
    auto headers = get_auth_headers(short_server_name, uid, calling_pid, username);

    if (not headers) {
      fastlog(ERROR,"Server %s not found. Aborting!", short_server_name.data());
      return {};
    }

    auto curl_res = safeGET(conn_params->server_url + "/dids/" + scope + "/", conn_params->ca_path, headers);
    if(curl_res.res != CURLE_OK){
      fastlog(ERROR, "Dids: Curl error. Abort.");
      return {};
    }


    curl_slist_free_all(headers);

    std::vector<rucio_did> dids;

    fastlog(DEBUG, "return value:");
    for (auto &line : curl_res.payload) {
      fastlog(DEBUG, "%s", line.data());
      structurize_did(line, dids);
    }

    for(const auto& did : dids){
      is_container_cache.set_value(short_server_name+scope+did.name, did.type != rucio_data_type::rucio_file);
      file_size_cache.set_value(short_server_name+scope+did.name, did.size);
    }

    dids_cache.set_value(key, std::pair(time_now + chache_duration, std::move(dids)));
    return dids_cache[key].second;
  } else {
    fastlog(INFO,"USING CACHE");
    return found->second.second;
  }
}

std::vector<rucio_did> rucio_list_container_dids(const std::string& scope, const std::string& container_name, const std::string& short_server_name, uid_t uid, pid_t calling_pid, std::string username){
  auto conn_params = get_server_params(short_server_name);
  auto key = short_server_name+scope+container_name;
  auto found = container_dids_cache.find(key);
  time_t time_now;
  time(&time_now);

  fastlog(INFO, "Fetching %s", key.data());
  if(found == container_dids_cache.end()){
    fastlog(DEBUG, "rucio_list_container_dids: Not in cache");}
  else if(found->second.first < time_now){
    fastlog(DEBUG, "rucio_list_container_dids: Timed out");}
  else
    fastlog(DEBUG, "rucio_list_container_dids: Using cache");

  if(found == container_dids_cache.end() || found->second.first < time_now ) {

    auto headers = get_auth_headers(short_server_name, uid, calling_pid, username);

    if (not headers) {
      fastlog(ERROR,"Server %s not found. Aborting!", short_server_name.data());
      return {};
    }

    auto curl_res = safeGET(
            conn_params->server_url + "/dids/" + scope + "/" + container_name + "/dids",
            conn_params->ca_path,
            headers);
    if(curl_res.res != CURLE_OK){
      fastlog(ERROR, "Container: Curl error. Abort.");
      return {};  
    }


    curl_slist_free_all(headers);

    std::vector<rucio_did> dids;

    for (auto &line : curl_res.payload) {
      structurize_container_did(line, dids);
    }

    for(const auto& did : dids){
      is_container_cache.set_value(short_server_name+scope+did.name, did.type != rucio_data_type::rucio_file);
      file_size_cache.set_value(short_server_name+scope+did.name, did.size);
      //fastlog(DEBUG,"%s:%s:%s -> %s",short_server_name.data(), scope.data(), did.name.data(), // TODO: Remove
      //        (is_container_cache[short_server_name+scope+did.name])?"true":"false"); // TODO: Remove
    }

    container_dids_cache.set_value(key, std::pair(time_now + chache_duration, std::move(dids)));
    return container_dids_cache[key].second;
  } else {
    fastlog(DEBUG,"USING CACHE");
    return found->second.second;
  }
}

bool rucio_is_container(const rucio_did& did){
  return did.type != rucio_data_type::rucio_file;
}

bool rucio_is_container(const std::string& path, uid_t uid, pid_t calling_pid, std::string username){
  auto short_server_name = extract_server_name(path);
  auto conn_params = get_server_params(short_server_name);
  auto scope = extract_scope(path);
  auto name = extract_name(path);
  auto found = is_container_cache.find(short_server_name+scope+name);

  if(found == is_container_cache.end()) {
    auto headers = get_auth_headers(short_server_name, uid, calling_pid, username);

    if (not headers) {
      fastlog(ERROR,"Server %s not found. Aborting!", short_server_name.data());
      return {};
    }

    auto curl_res = safeGET(conn_params->server_url + "/dids/" + scope + "/" + name,
                        conn_params->ca_path,
                        headers);
    if(curl_res.res != CURLE_OK){
      fastlog(ERROR, "IsCont: Curl error. Abort.");
      return false;
    }


    curl_slist_free_all(headers);

    is_container_cache.set_value(path, (curl_res.payload.front().find(R"("CONTAINER",)") != std::string::npos) || (curl_res.payload.front().find(R"("DATASET",)") != std::string::npos));
    return is_container_cache[path];
  } else {
    fastlog(DEBUG,"USING CACHE");
    fastlog(DEBUG,"%s:%s:%s -> %s",short_server_name.data(), scope.data(), name.data(),
              (is_container_cache[short_server_name+scope+name])?"true":"false");
    return found->second;
  }
}

bool rucio_is_file(const std::string& path, uid_t uid, pid_t calling_pid, std::string username){
  auto short_server_name = extract_server_name(path);
  auto conn_params = get_server_params(short_server_name);
  auto scope = extract_scope(path);
  auto name = extract_name(path);
  auto found = is_file_cache.find(short_server_name+scope+name);

  if(found == is_container_cache.end()) {
    auto headers = get_auth_headers(short_server_name, uid, calling_pid, username);

    if (not headers) {
      fastlog(ERROR,"Server %s not found. Aborting!", short_server_name.data());
      return {};
    }

    auto curl_res = safeGET(conn_params->server_url + "/dids/" + scope + "/" + name,
                        conn_params->ca_path,
                        headers);
    if(curl_res.res != CURLE_OK){
      fastlog(ERROR, "IsFile: Curl error. Abort. %s", (conn_params->server_url + "/dids/" + scope + "/" + name).data());
      return false;
    }


    curl_slist_free_all(headers);

    is_file_cache.set_value(path, curl_res.payload.front().find(R"("FILE",)") != std::string::npos);
    return is_file_cache[path];
  } else {
    fastlog(DEBUG,"USING CACHE");
    fastlog(DEBUG,"%s:%s:%s -> %s",short_server_name.data(), scope.data(), name.data(),
            (is_file_cache[short_server_name+scope+name])?"true":"false");
    return found->second;
  }
}

off_t rucio_get_size(const std::string& path, uid_t uid, pid_t calling_pid, std::string username){
  auto short_server_name = extract_server_name(path);
  auto scope = extract_scope(path);
  auto name = extract_name(path);
  auto key = short_server_name+scope+name;

  auto cache_found = file_size_cache.find(key);

  if(cache_found != file_size_cache.end()){
    return cache_found->second;
  }

  auto conn_params = get_server_params(short_server_name);

  auto headers = get_auth_headers(short_server_name, uid, calling_pid, username);

  if (not headers) {
    fastlog(ERROR,"Server %s not found. Aborting!", short_server_name.data());
    return {};
  }

  auto curl_res = safeGET(conn_params->server_url + "/dids/" + scope + "/" + name, // Dont do this. Call attachements instead
                      conn_params->ca_path,
                      headers);
  if(curl_res.res != CURLE_OK){
    fastlog(ERROR, "Size: Curl error. Abort.");
    return 0;
  }


  for(auto const& payload : curl_res.payload){
    auto found = payload.find(rucio_bytes_metadata);
    if (found != std::string::npos) {
      auto pos = payload.find(rucio_bytes_metadata);
      auto pos2 = payload.find(',', pos + rucio_bytes_metadata_length - 1);
      auto size_bytes = payload.substr(pos + rucio_bytes_metadata_length, pos2 - pos - rucio_bytes_metadata_length);
      fastlog(INFO, "File size is %s", size_bytes.data());
      int size_i = 0;
      try {
        size_i = std::stoll(size_bytes);
      } catch (...){

      }
      file_size_cache.emplace(key, size_i);
      return size_i;
    }
  }
  return -1;
}

int authenticate_user(const std::string &path, uid_t uid, pid_t calling_pid, std::string username)
{
  auto short_server_name = extract_server_name(path);
  auto is_token_valid = rucio_is_token_valid(short_server_name, uid);
  if (is_token_valid) {
    return TOKEN_OK;
  }
  auto conn_params = get_server_params(short_server_name);

  switch (conn_params->rucio_auth_mode){
    case auth_mode::userpass: return rucio_get_auth_token_userpass(short_server_name, uid, calling_pid, username);
    case auth_mode::x509: return rucio_get_auth_token_x509(short_server_name, uid, calling_pid, username);
    case auth_mode::oidc: return rucio_get_auth_token_oidc(short_server_name, uid, calling_pid, username);
    default: return TOKEN_ERROR;
  }
}

std::vector<std::string> rucio_get_replicas_metalinks(const std::string& path, uid_t uid, pid_t calling_pid, std::string username){
  auto short_server_name = extract_server_name(path);
  auto conn_params = get_server_params(short_server_name);
  auto scope = extract_scope(path);
  auto name = extract_name(path);

  auto headers = get_auth_headers(short_server_name, uid, calling_pid, username);

  if (not headers) {
    fastlog(ERROR,"Server %s not found. Aborting!", short_server_name.data());
    return {};
  }
  headers= curl_slist_append(headers, "HTTP_ACCEPT: metalink4+xml");

  auto curl_res = safeGET(conn_params->server_url + "/replicas/" + scope + "/" + name,
                      conn_params->ca_path,
                      headers);
  if(curl_res.res != CURLE_OK){
    fastlog(ERROR, "Meta: Curl error. Abort.");
    return {};
  }


  std::string merged_response;

  for(const auto& line : curl_res.payload){
    fastlog(DEBUG, "%s", line.data());
    merged_response.append(line);
  }

  fastlog(DEBUG, "\n\nMerged:\n%s", merged_response.data());

  std::string identifier = R"("rses": {)";
  auto rses_position = merged_response.find(identifier);
  auto rses_end = merged_response.find('}', rses_position+1);
  auto rses = std::string(merged_response.begin() + rses_position + identifier.length(), merged_response.begin() + rses_end);

  fastlog(DEBUG, "\n\nRSES:\n%s\n\n", rses.data());

  auto beg_pfn = rses.find('[', 0);
  auto end_pfn = rses.find(']', beg_pfn + 1);

  std::cout << beg_pfn << std::endl;
  std::cout << end_pfn << std::endl;

  std::vector<std::string> pfns;

  while(beg_pfn != std::string::npos && end_pfn != std::string::npos){
    pfns.emplace_back(std::string(rses.begin() + beg_pfn + 2, rses.begin() + end_pfn - 1));

    beg_pfn = rses.find('[', end_pfn + 1);
    end_pfn = rses.find(']', beg_pfn + 1);

    fastlog(DEBUG, "---> %s", pfns.back().data());
  }

  return std::move(pfns);
}


bool rucio_validate_server(const std::string& short_server_name, uid_t uid, pid_t calling_pid, std::string username){
  auto conn_params = get_server_params(short_server_name);

  if(not rucio_ping(short_server_name)){
    fastlog(ERROR, "Server %s unreachable via network.", conn_params->server_url.data());
    return false;
  }

  if(rucio_get_auth_token_root(short_server_name, uid, calling_pid, username) != TOKEN_OK){
    fastlog(ERROR, "Cannot validate server %s auth settings.", conn_params->server_url.data());
    return false;
  }

  return true;
}
