/*//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Copyright European Organization for Nuclear Research (CERN)
Licensed under the Apache License, Version 2.0 (the "License");
You may not use this file except in compliance with the License.
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
Authors:
 - Gabriele Gaetano Fronzé, <gfronze@cern.ch>, 2019-2020
/*//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <globals.h>
#include <REST-API.h>
#include <fastlog.h>

#include <pwd.h>
#include <unistd.h>

int main(){
  uid_t uid = getuid();
  std::string username = getpwuid(uid)->pw_name;
  pid_t calling_pid = getpid();
  fastlog::logLevel = DEBUG;

  parse_settings_cfg(uid, calling_pid, username);

  rucio_get_auth_token_userpass("test-userpass", uid, calling_pid, username);
  rucio_get_auth_token_x509("test-x509", uid, calling_pid, username);

  return 0;
}

