#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2009-2012 Red Hat, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# uses Suds - https://fedorahosted.org/suds/
from suds import *
from suds.client import Client
from sys import exit, argv
from optparse import OptionParser
from aviary.util import *
from time import *
import cmd
import logging

# mainly interactive script for managing a Hadoop cluster under HTCondor
# using Aviary SOAP interface

# change these for other default locations and ports
DEFAULTS = {'wsdl':'file:/var/lib/condor/aviary/services/hadoop/aviary-hadoop.wsdl',
            'host':'localhost',
            'port':'9090',
            'service':'/services/hadoop/',
            'verbose': False
            }
plugins = []
logging.basicConfig(level=logging.CRITICAL)

class AviaryClient:

    client = None
    base_url = None

    def __init__(self,wsdl,base_url):
        self.base_url = base_url
        self.client = Client(wsdl)

    def getClient(self,url_suffix):
        if url_suffix:
            url = self.base_url+url_suffix
            self.client.set_options(location=url)
        return self.client

class HadoopCtrlCmd(cmd.Cmd):
    
    aviary = None
    nodetype = None
    bin_file = None
    owner = None

    def __init__(self,wsdl,url,bin_file,nodetype,owner):
        self.nodetype = nodetype
        if bin_file!='':
            self.bin_file = bin_file
        self.aviary = AviaryClient(wsdl,url)
        self.owner = owner

    def isFloatish(self,val):
        try:
            float(val)
            return True
        except:
            return False

    def create_ref(self,val):
        new_ref = None
        ref_client = self.aviary.getClient(None)
        if "://" in val:
            new_ref = ref_client.factory.create("ns1:HadoopID")
            new_ref.ipc = val
        elif self.isFloatish(val):
            new_ref = ref_client.factory.create("ns1:HadoopID")
            new_ref.id = val
        else:
            print "invalid HadoopID specified:",val," Continuing..."
        return new_ref

    def create_reflist(self,line):
        ref_tokens = line.split()
        ref_list = []
        for s in ref_tokens:
            ref = self.create_ref(s)
            if ref:
                ref_list.append(ref)
        return ref_list

    def do_start(self,line):
        "start a Hadoop Node/Tracker"
        count = 1
        is_nn = self.nodetype == "NameNode"
        if not is_nn:
            count = raw_input('Count (default is 1): ')
        result = None
        target_op = "start"+self.nodetype
        start_client = self.aviary.getClient(target_op)
        func = getattr(start_client.service, target_op, None)
        try:
            if callable(func):
                if is_nn:
                    result = func(self.bin_file,self.owner)
                else:
                    ref = self.create_reflist(line)
                    if ref:
                        result = func(ref[0],self.bin_file,self.owner,count)
                    else:
                        print "you must supply a HadoopID (cluster.proc or ipc uri)"
                        return
        except Exception, e:
            print e
        result and self.print_status(result.status)

    def do_stop(self,line):
        "stop a Hadoop Node/Tracker"
        result = None
        target_op = "stop"+self.nodetype
        stop_client = self.aviary.getClient(target_op)
        func = getattr(stop_client.service, target_op, None)
        try:
            if callable(func):
                refs = self.create_reflist(line)
                result = func(refs)
        except Exception, e:
            print e
        result and self.print_status(result.status)

    def do_list(self,line):
        "list Hadoop Node/Tracker"
        result = None
        target_op = "get"+self.nodetype
        list_client = self.aviary.getClient(target_op)
        func = getattr(list_client.service, target_op, None)
        try:
            if callable(func):
                refs = self.create_reflist(line)
                result = func(refs)
        except Exception, e:
            print e
        self.print_query(result)

    def print_status(self,status):
        text = ""
        try:
            text = ": "+status.text
        except Exception,e:
            pass
        print status.code,text

    def print_header(self):
        print "ID".ljust(7),"SUBMITTED".ljust(27),"STATE".ljust(10),"UPTIME".ljust(10),"OWNER".ljust(16),"IPC"
        print "--".ljust(7),"---------".ljust(27),"-----".ljust(10),"------".ljust(10),"-----".ljust(16),"---"
        return True

    def print_query(self, response):
        if response:
            self.print_header()
            for r in response.results:
                print str(r.ref.id).ljust(7),str(ctime(r.submitted)).ljust(27),str(r.state).ljust(10), \
                    str(strftime('%H:%M:%S',gmtime(r.uptime))).ljust(10), str(r.owner).ljust(16), str(r.ref.ipc)

class AviaryHadoopTool(cmd.Cmd):
    
    prompt = 'aviary> '
    host = DEFAULTS['host']
    port = DEFAULTS['port']
    verbose = DEFAULTS['verbose']
    base_url = 'http://'+host+':'+port
    bin_file = ''
    import pwd
    owner =  pwd.getpwuid(os.getuid())[0]
    
    def do_owner(self,line):
        "view/edit submitting owner name"
        if line:
            self.owner = line
        print 'owner is:', self.owner

    def set_base_url(self):
        self.base_url = 'http://'+self.host+':'+self.port
    
    def do_host(self, line):
        "view/edit HTTP hostname"
        if line:
            self.host = line
        print 'host is:', self.host
        self.set_base_url()

    def do_port(self, line):
        "view/edit HTTP port"
        if line:
            self.port = line
        print 'port is:', self.port
        self.set_base_url()

    def do_url(self, line):
        "view/edit the base url for connect"
        print 'base url is:', self.base_url+DEFAULTS['service']
        print 'use "host" and "port" to modify HTTP target'

    def do_file(self, line):
        "absolute path to a Hadoop binary distribution tar/zip"
        if line:
            self.bin_file = line
        print "current bin file is:",self.bin_file

    def do_verbose(self, line):
        "toggle debug diagnostic messages"
        if self.verbose:
            self.verbose = False
            logging.basicConfig(level=logging.CRITICAL)
            logging.getLogger('suds.client').setLevel(logging.CRITICAL)
        else:
            self.verbose = True
            logging.basicConfig(level=logging.INFO)
            logging.getLogger('suds.client').setLevel(logging.DEBUG)
        print 'verbose:', self.verbose

    _AVAILABLE_CMDS = ('start','stop','list')
    def provide_cmds(self,text):
        return [i for i in self._AVAILABLE_CMDS if i.startswith(text)]

    def complete_namenode(self, text, line, begidx, endidx):
        return self.provide_cmds(text)

    def complete_datanode(self, text, line, begidx, endidx):
        return self.provide_cmds(text)

    def complete_jobtracker(self, text, line, begidx, endidx):
        return self.provide_cmds(text)

    def complete_tasktracker(self, text, line, begidx, endidx):
        return self.provide_cmds(text)

    def execute(self,nodetype,line):
        HadoopCtrlCmd(DEFAULTS['wsdl'],self.base_url+DEFAULTS['service'],self.bin_file,nodetype,self.owner).onecmd(line)

    def do_namenode(self, line):
        "process a NameNode command [start, stop, list]"
        self.execute('NameNode',line)

    def do_datanode(self, line):
        "process a DataNode command [start, stop, list]"
        self.execute('DataNode',line)

    def do_jobtracker(self, line):
        "process a JobTracker command [start, stop, list]"
        self.execute('JobTracker',line)

    def do_tasktracker(self, line):
        "process a TaskTracker command [start, stop, list]"
        self.execute('TaskTracker',line)

    def do_EOF(self, line):
        return True

if __name__ == '__main__':
    if len(argv) > 1:
        AviaryHadoopTool().onecmd(' '.join(argv[1:]))
    else:
        AviaryHadoopTool().cmdloop()
