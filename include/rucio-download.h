/*//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Copyright European Organization for Nuclear Research (CERN)
Licensed under the Apache License, Version 2.0 (the "License");
You may not use this file except in compliance with the License.
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
Authors:
 - Gabriele Gaetano Fronzé, <gfronze@cern.ch>, 2019-2020
/*//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef RUCIO_FUSE_POSIX_RUCIO_DOWNLOAD_H
#define RUCIO_FUSE_POSIX_RUCIO_DOWNLOAD_H

#include <fastlog.h>
#include "download-cache.h"
#include "constants.h"
#include "utils.h"
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

using namespace fastlog;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Error return codes definition
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define FILE_NOT_FOUND 42
#define SERVER_NOT_FOUND 17
#define MAX_ATTEMPTS 3
#define TOO_MANY_ATTEMPTS 314
#define SETTINGS_NOT_FOUND 1717

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Wrapper around rucio download called as bash command
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//int rucio_download_wrapper(const std::string& server_name, const std::string* server_cfg, const std::string& scope, const std::string& name){
void* rucio_download_wrapper(void* args){
  void** argarray = reinterpret_cast<void**>(args);
  uid_t* uid = reinterpret_cast<uid_t*>(argarray[0]);
  pid_t* calling_pid = reinterpret_cast<pid_t*>(argarray[1]);
  std::string* username = reinterpret_cast<std::string*>(argarray[2]);
  std::string server_name = *reinterpret_cast<std::string*>(argarray[3]);
  std::string* server_cfg = reinterpret_cast<std::string*>(argarray[4]);
  std::string scope =  *reinterpret_cast<std::string*>(argarray[5]);
  std::string name =  *reinterpret_cast<std::string*>(argarray[6]);
  auto cache_path = rucio_cache_path + "/" + server_name + "/" + scope;
  auto file_path = cache_path + "/" + name;
  FILE* file = fopen(file_path.data(), "rb");
  int* result = (int*)malloc(sizeof(int));
  
  // If file is found populate the cache with the local entry
  if (file){
    fastlog(INFO,"File %s already there!",file_path.data());

  // Otherwise download the file using rucio and populate the cache
  } else {
    fastlog(DEBUG, "Downloading at %s...", cache_path.data());
    FILE *settings = fopen(server_cfg->data(), "r");

    // Abort with correct exit code if the server config is not found
    if (not settings) {
      fastlog(ERROR, "Server config file not found at %s. Aborting!", server_cfg->data());
      fclose(settings);
      *result = SETTINGS_NOT_FOUND;
      pthread_exit((void*)result);
    }

    fclose(settings);

    struct passwd *pwd = getpwuid(*uid);
    if (!pwd) {
      fastlog(ERROR, "User %d not found", *uid);
      *result = SETTINGS_NOT_FOUND;
      pthread_exit((void*)result);
    }
    
    // Initialize groups BEFORE setuid
    if (initgroups(pwd->pw_name, pwd->pw_gid) == -1) {
      fastlog(ERROR, "initgroups failed: %s", strerror(errno));
    }
    
    // Switch user
    if (setgid(pwd->pw_gid) == -1) {
      fastlog(ERROR, "setgid failed: %s", strerror(errno));
      *result = SETTINGS_NOT_FOUND;
      pthread_exit((void*)result);
    }
    
    if (setuid(*uid) == -1) {
      fastlog(ERROR, "setuid failed: %s", strerror(errno));
      *result = SETTINGS_NOT_FOUND;
      pthread_exit((void*)result);
    }
    
    // Set environment as the user
    setenv("HOME", pwd->pw_dir, 1);
    setenv("USER", pwd->pw_name, 1);
    setenv("LOGNAME", pwd->pw_name, 1);
    setenv("SHELL", pwd->pw_shell, 1);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin", 1);
    
    chdir(pwd->pw_dir);

    std::string did = scope + ":" + name;
    std::string command = "rucio --config " + *server_cfg + " download --no-subdir --dir " + cache_path + " " + did;
    fastlog(DEBUG, "Executing: %s", command.data());
    fastlog(DEBUG, "Downloading to: %s", file_path.data());
    auto system_return = system(command.data());

    fastlog(DEBUG, "Checking downloaded file...");
    file = fopen(file_path.data(), "rb");

    // Check downloaded file and use it to populate the cache
    if (not file) {
      fastlog(ERROR, "Failed file download! Passing over...");
      *result = FILE_NOT_FOUND;
      pthread_exit((void*)result);
    } else {
      fastlog(DEBUG, "Download OK!");
    }

    fastlog(DEBUG, "Adding to cache file downloaded at %s", file_path.data());
  }

  rucio_download_cache.add_file(file_path, file);
  *result = 0;
  pthread_exit((void*)result);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility struct to serve as enqueue-able download request descriptor
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct rucio_download_info{
    std::string fserver_name;
    std::string* fserver_config;
    std::string fdid;
    std::string::size_type fpos;
    uid_t uid;
    pid_t calling_pid;
    std::string username;
    int freturn_code = 0;
    unsigned int fattempt = 0;
    bool fdownloaded = false;

    explicit rucio_download_info(std::string did, const std::string& path, uid_t uid, pid_t calling_pid, std::string username) :
      fdid(std::move(did)),
      fserver_name(extract_server_name(path)),
      uid(uid),
      calling_pid(calling_pid),
      username(std::move(username)){
      fpos = fdid.find_first_of(':');
      fserver_config = get_server_config(fserver_name);
      fastlog(DEBUG, "Download info added with server name %s and settings at %s", fserver_name.data(), fserver_config->data());
    }

    std::string print(){
      if(fdownloaded){
        return "Did " + fdid + " downloaded at " + full_cache_path();
      } else {
        return "Did " + fdid + " download FAILED!";
      }
    }

    std::string scopename(){
      return fdid.substr(0, fpos);
    }

    std::string filename(){
      return fdid.substr(fpos+1);
    }

    std::string full_cache_path(){
      return rucio_cache_path + "/" + this->fserver_name + "/" + this->scopename() + "/" + this->filename();
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Alternate rucio download wrapper which ingests a rucio download info object. Useful for download pipeline use.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
rucio_download_info* rucio_download_wrapper(rucio_download_info& info){
  if(not server_exists(info.fserver_name)) {
    fastlog(ERROR, "Server %s not found. Aborting!", info.fserver_name.data());
    info.freturn_code = SERVER_NOT_FOUND;
    return &info;
  } else {
    if (info.fattempt < MAX_ATTEMPTS and info.freturn_code != SETTINGS_NOT_FOUND) {
      info.fattempt++;
      pthread_t child;
      std::string scopename = info.scopename();
      std::string filename = info.filename();
      void* args[7] = {&info.uid, &info.calling_pid, &info.username, &info.fserver_name, info.fserver_config, 
                                                                    &scopename, &filename};
      void* exit_status;
      int result = pthread_create(&child, NULL, rucio_download_wrapper, args);
      pthread_join(child, &exit_status);
      info.freturn_code = *reinterpret_cast<int*>(exit_status);
      free(exit_status);
      info.fdownloaded = (info.freturn_code != FILE_NOT_FOUND and info.freturn_code != SETTINGS_NOT_FOUND);
    } else {
      info.freturn_code = TOO_MANY_ATTEMPTS;
      info.fdownloaded = false;
    }
  }
  return &info;
}

#endif //RUCIO_FUSE_POSIX_RUCIO_DOWNLOAD_H
