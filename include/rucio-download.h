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
//#include <sys/ioctl.h>

//#include <pwd.h>
//#include <grp.h>
//#include <unistd.h>
#include <sys/wait.h>
//#include <string.h>

//#include <fstream>

//#include <pty.h>

//#include "terminal-redirect.h"
#include <globals.h>

using namespace fastlog;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Error return codes definition
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define FILE_NOT_FOUND 1
#define SERVER_NOT_FOUND 2
#define MAX_ATTEMPTS 4
#define TOO_MANY_ATTEMPTS 8
#define SETTINGS_NOT_FOUND 16
#define UNKNOWN_ERROR 32

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility struct to serve as enqueue-able download request descriptor
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct rucio_download_info{
    std::string fserver_name;
    std::string* fserver_config;
    std::string fdid;
    std::string* temp_config_folder;
    std::string::size_type fpos;
    std::string username;
    int freturn_code = 0;
    unsigned int fattempt = 0;
    bool fdownloaded = false;

    explicit rucio_download_info(std::string did, const std::string& path, uid_t uid, pid_t calling_pid, std::string username) :
      fdid(std::move(did)),
      fserver_name(extract_server_name(path)),
      username(std::move(username)){
      fpos = fdid.find_first_of(':');
      fserver_config = get_server_config(fserver_name);
      temp_config_folder = get_temp_config_folder(fserver_name);
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
      auto cache_path = rucio_cache_path + "/" + info.fserver_name + "/" + scopename;
      auto file_path = cache_path + "/" + filename;
      
      std::string did = scopename + ":" + filename;
      std::string rucio_path = "/home/jansson/venv/rucio_venv/bin/rucio";
      std::string new_cfg;
      try{
        new_cfg = copyConfig(*info.fserver_config, *info.temp_config_folder, info.username);
      }
      catch (std::runtime_error error){
        info.freturn_code = SETTINGS_NOT_FOUND;
        info.fdownloaded = false;
      }

      std::string command = rucio_path + " --config " + new_cfg + " download --no-subdir --dir " + cache_path + " " + did;
      
      auto system_return = system(command.data());
      fastlog(DEBUG, "Checking downloaded file...");
      auto file = fopen(file_path.data(), "rb");
      if (not file) {
        fclose(file);
        fastlog(ERROR, "Failed file download! Passing over...");
        info.freturn_code = FILE_NOT_FOUND;
        info.fdownloaded = false;
      }
      else{
        fastlog(DEBUG, "Download OK!");
        info.freturn_code = 0;
        info.fdownloaded = true;
      }
      fastlog(DEBUG, "Adding to cache file downloaded at %s", file_path.data());      
    } else {
      info.freturn_code = TOO_MANY_ATTEMPTS;
      info.fdownloaded = false;
    }
  }
  return &info;
}

#endif //RUCIO_FUSE_POSIX_RUCIO_DOWNLOAD_H
