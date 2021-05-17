/***************************************************************
 *
 * Copyright (C) 2021, Condor Team, Computer Sciences Department,
 * University of Wisconsin-Madison, WI.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You may
 * obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************/

#pragma once

#include <memory>

#include <openssl/x509.h>

namespace htcondor {

bool generate_x509_ca(const std::string &cafile, const std::string &cakeyfile);
bool generate_x509_cert(const std::string &certfile, const std::string &keyfile, const std::string &cafile, const std::string &cakeyfile);

bool get_known_hosts_first_match(const std::string &hostname, bool &permitted, std::string &method, std::string &method_info);
bool add_known_hosts(const std::string &hostname, bool permitted, const std::string &method, const std::string &method_info);

}
