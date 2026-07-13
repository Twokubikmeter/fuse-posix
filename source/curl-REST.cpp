/*//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Copyright European Organization for Nuclear Research (CERN)
Licensed under the Apache License, Version 2.0 (the "License");
You may not use this file except in compliance with the License.
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
Authors:
 - Gabriele Gaetano Fronzé, <gfronze@cern.ch>, 2019-2020
/*//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <curl-REST.h>
#include <utils.h>
#include <iostream>
#include <unordered_map>
#include <fastlog.h>

using namespace fastlog;

size_t curl_append_string_to_vect_callback(void *contents, size_t size, size_t nmemb, std::vector<std::string> &s)
{
    size_t newLength = size*nmemb;
    s.emplace_back(to_string((char*)contents, newLength));
    return newLength;
}

curlRet GET(const std::string& url, const std::string& ca_path, const struct curl_slist* headers, bool include_headers, long timeout, bool insecure){
  curlRet ret;
  {
    auto static_curl = curlWrap();

    fastlog(INFO, "GET %s", url.data());
    fastlog(INFO, "CA path %s", ca_path.data());

    curl_easy_setopt(static_curl(), CURLOPT_URL, url.data());
    curl_easy_setopt(static_curl(), CURLOPT_CUSTOMREQUEST, "GET");

    if(insecure){
      curl_easy_setopt(static_curl(), CURLOPT_SSL_VERIFYPEER, CURLOPT_FALSE); //only for https
      curl_easy_setopt(static_curl(), CURLOPT_SSL_VERIFYHOST, CURLOPT_FALSE); //only for https
    } else {
      curl_easy_setopt(static_curl(), CURLOPT_SSL_VERIFYPEER, CURLOPT_TRUE); //only for https
      curl_easy_setopt(static_curl(), CURLOPT_SSL_VERIFYHOST, CURLOPT_SUPERTRUE); //only for https
    }

    if(not ca_path.empty()) {
      curl_easy_setopt(static_curl(), CURLOPT_CAINFO, ca_path.data());
      curl_easy_setopt(static_curl(), CURLOPT_CAPATH, ca_path.data());
    }

    curl_easy_setopt(static_curl(), CURLOPT_WRITEFUNCTION, curl_append_string_to_vect_callback);
    curl_easy_setopt(static_curl(), CURLOPT_WRITEDATA, &ret.payload);
    curl_easy_setopt(static_curl(), CURLOPT_VERBOSE, CURLOPT_TRUE); //remove this to disable verbose output
    curl_easy_setopt(static_curl(), CURLOPT_TIMEOUT, timeout);

    // Include reply headers in CURLOPT_WRITEFUNCTION
    if (include_headers) {
      curl_easy_setopt(static_curl(), CURLOPT_HEADER, CURLOPT_TRUE);
    }

    // Add headers to request if present
    if (headers) {
      curl_easy_setopt(static_curl(), CURLOPT_HTTPHEADER, headers);
    }

    // Perform CURL request
    ret.res = curl_easy_perform(static_curl());
  }

  // Check return code to detect issues
  if(ret.res != CURLE_OK)
  {
    fastlog(ERROR, "curl_easy_perform() failed: %s url=", curl_easy_strerror(ret.res), url.data());
  }

  return ret;
}


#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

#include <fstream>

#include <sys/ioctl.h>  
#include <pty.h>

#include "terminal-redirect.h"

std::string GET_OIDC(curlOIDCBundle& bundle, uid_t uid, pid_t calling_pid, std::string username) {
  fastlog(INFO, "Starting OIDC authentication for user %s (uid %d, pid %d)", 
          username.c_str(), uid, calling_pid);
  
  int master_fd, slave_fd;
  pid_t pid;
  std::cout << "in GET_OIDC " << std::endl;

  // Create a pseudo-terminal
  if (openpty(&master_fd, &slave_fd, nullptr, nullptr, nullptr) == -1) {
    fastlog(ERROR, "Failed to openpty: %s", strerror(errno));
    return "";
  }
  
  pid = fork();
  
  if (pid == -1) {
    fastlog(ERROR, "Failed to fork: %s", strerror(errno));
    close(master_fd);
    close(slave_fd);
    return "";
  }
  
  if (pid == 0) {
    // Child process
    close(master_fd);
    
    setsid();
    ioctl(slave_fd, TIOCSCTTY, 0);
    
    dup2(slave_fd, STDIN_FILENO);
    dup2(slave_fd, STDOUT_FILENO);
    dup2(slave_fd, STDERR_FILENO);
    
    if (slave_fd > STDERR_FILENO) {
      close(slave_fd);
    }
    
    struct passwd *pwd = getpwuid(uid);
    if (!pwd) {
      fastlog(ERROR, "User %d not found", uid);
      exit(1);
    }
    
    // Initialize groups BEFORE setuid
    if (initgroups(pwd->pw_name, pwd->pw_gid) == -1) {
      fastlog(ERROR, "initgroups failed: %s", strerror(errno));
    }
    
    // Switch user
    if (setgid(pwd->pw_gid) == -1) {
      fastlog(ERROR, "setgid failed: %s", strerror(errno));
      exit(1);
    }
    
    if (setuid(uid) == -1) {
      fastlog(ERROR, "setuid failed: %s", strerror(errno));
      exit(1);
    }
    
    // Set environment as the user
    setenv("HOME", pwd->pw_dir, 1);
    setenv("USER", pwd->pw_name, 1);
    setenv("LOGNAME", pwd->pw_name, 1);
    setenv("SHELL", pwd->pw_shell, 1);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin", 1);
    
    chdir(pwd->pw_dir);
    
    // TODO: Find rucio - should work now that we're the user
    //std::string rucio_path = find_rucio_executable();
    std::string rucio_path = "/home/jansson/venv/rucio_venv/bin/rucio";
    if (rucio_path.empty()) {
      rucio_path = "/usr/bin/rucio";  // fallback
    }
    
    const char *argv[] = {
      rucio_path.c_str(),
      "--config",
      bundle.config_file.c_str(),
      "whoami",
      nullptr
    };
    
    fastlog(INFO, "Executing %s as uid %d", rucio_path.c_str(), getuid());
    execv(rucio_path.c_str(), (char * const *)argv);
    
    fastlog(ERROR, "Failed to execute rucio: %s", strerror(errno));
    exit(1);
  } else {
    // Parent process - relay PTY output to calling process
    close(slave_fd);
    
    relay_pty_bidirectional(master_fd, calling_pid);
    
    int status;
    waitpid(pid, &status, 0);
    close(master_fd);
    
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      fastlog(ERROR, "OIDC authentication failed for user %d", uid);
      return "";
    }
    
    fastlog(INFO, "OIDC authentication completed for user %d", uid);
  }
  
  // Read token from user's location
  std::string token_path = "/tmp/" + username + "/.rucio_" + username + "/auth_token_for_default_account";
  fastlog(INFO, "Token path: %s", token_path.c_str());


  std::ifstream file(token_path);
  if (!file) {
    fastlog(ERROR, "Failed to read token for user %d", uid);
    return "";
  }
  
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string token = buffer.str();
  
  while (!token.empty() && 
         (token.back() == '\n' || token.back() == '\r')) {
    token.pop_back();
  }
  
  return token;
}
  
curlRet GET_x509(const std::string& url, curlx509Bundle& bundle, const struct curl_slist* headers, bool include_headers, long timeout){
  curlRet ret;
  {
    auto static_curl = curlWrap();

    fastlog(DEBUG, "GET x509 %s", url.data());
    fastlog(DEBUG, "CA path %s", bundle.pCACertFile.data());

    curl_easy_setopt(static_curl(), CURLOPT_URL, url.data());
    
    // x509 setup
    curl_easy_setopt(static_curl(), CURLOPT_SSLCERT, bundle.pCertFile.data());
    curl_easy_setopt(static_curl(), CURLOPT_SSLCERTTYPE, "PEM");
    curl_easy_setopt(static_curl(), CURLOPT_CUSTOMREQUEST, "GET");

    if (bundle.pEngine.length() != 0) {
      curl_easy_setopt(static_curl(), CURLOPT_SSLENGINE, bundle.pEngine.data());
    } else {
      curl_easy_setopt(static_curl(), CURLOPT_SSLENGINE_DEFAULT, CURLOPT_TRUE);
    }

    curl_easy_setopt(static_curl(), CURLOPT_SSLENGINE_DEFAULT, CURLOPT_TRUE);
    curl_easy_setopt(static_curl(), CURLOPT_SSLKEYTYPE, bundle.pKeyType.data());
    curl_easy_setopt(static_curl(), CURLOPT_SSLKEY, bundle.pKeyName.data());

    if (!bundle.pPassphrase.empty()) curl_easy_setopt(static_curl(), CURLOPT_KEYPASSWD, bundle.pPassphrase.data());

    curl_easy_setopt(static_curl(), CURLOPT_CAINFO, bundle.pCACertFile.data());
    curl_easy_setopt(static_curl(), CURLOPT_CAPATH, bundle.pCACertFile.data());

    curl_easy_setopt(static_curl(), CURLOPT_SSL_VERIFYPEER, CURLOPT_TRUE); //only for https
    curl_easy_setopt(static_curl(), CURLOPT_SSL_VERIFYHOST, CURLOPT_SUPERTRUE); //only for https

    curl_easy_setopt(static_curl(), CURLOPT_WRITEFUNCTION, curl_append_string_to_vect_callback);
    curl_easy_setopt(static_curl(), CURLOPT_WRITEDATA, &ret.payload);
    curl_easy_setopt(static_curl(), CURLOPT_VERBOSE, CURLOPT_FALSE); //remove this to disable verbose output

    // Include reply headers in CURLOPT_WRITEFUNCTION
    if (include_headers) {
      curl_easy_setopt(static_curl(), CURLOPT_HEADER, CURLOPT_TRUE);
    }

    // Add headers to request if present
    if (headers) {
      curl_easy_setopt(static_curl(), CURLOPT_HTTPHEADER, headers);
    }

    // Perform CURL request
    ret.res = curl_easy_perform(static_curl());
  }

  // Check return code to detect issues
  if(ret.res != CURLE_OK)
  {
    fastlog(ERROR, "curl_easy_perform() failed: %s\n", curl_easy_strerror(ret.res));
  }

  return ret;
}

curlRet safeGET(const std::string& url, const std::string& ca_path, const struct curl_slist * headers, bool include_headers, long timeout){
  curlRet ret = GET(url, ca_path, headers, include_headers, timeout);
  short retry = 0;
  while(ret.res != CURLE_OK && max_retry > ++retry){
    fastlog(ERROR, "safeGET: Curl error on URL %s. Retry.", url.data());
    timeout*=2;
    ret = GET(url, ca_path, headers, include_headers, timeout);
  }

  return ret;
}

curlRet POST(const std::string& url, const std::string& thing_to_post){
  curlRet ret;
  {
    auto static_curl = curlWrap();

    fastlog(DEBUG, "POST %s", url.data());

    curl_easy_setopt(static_curl(), CURLOPT_URL, url.data());
    curl_easy_setopt(static_curl(), CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(static_curl(), CURLOPT_POSTFIELDS, thing_to_post.data());
    curl_easy_setopt(static_curl(), CURLOPT_SSL_VERIFYPEER, CURLOPT_FALSE); //only for https
    curl_easy_setopt(static_curl(), CURLOPT_SSL_VERIFYHOST, CURLOPT_FALSE); //only for https
    curl_easy_setopt(static_curl(), CURLOPT_VERBOSE, CURLOPT_TRUE); //remove this to disable verbose output

    // Perform CURL request
    ret.res = curl_easy_perform(static_curl());
  }

  // Check return code to detect issues
  if(ret.res != CURLE_OK)
    fastlog(ERROR, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(ret.res));

  return ret;
}