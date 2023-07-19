#!/usr/bin/env pytest

#---------------------------------------------------------------
# Author: Joe Reuss
#
# Notes:
# test shadow hook implementation. Below is a list of the
# tests run:
# 1. Test shadow hook status messages as well as changes to the classad
# 2. Test the shadow hook is run on the shadow side
# 3. Test that the changes a shadow hook makes are reflected in the starter
#       but not the schedd
# 4. Test that errors in shadow hooks are found in the shadow log
# 5. TODO: Test that shadow hooks work when the hook keyword is in the
#       config file instead of the submit file
#---------------------------------------------------------------

import os
import stat
from ornithology import *

#
# Write out some simple shadow job hook scripts.
#
@standup
def write_job_hook_scripts(test_dir):
    # Below tests functionality of status codes with shadow hooks:
    script_file = test_dir / "hook_shadow_hold_prepare.sh"
    script_contents = f"""#!/bin/bash
        echo 'HookStatusMessage = "Really bad, going on hold"'
        echo 'HookStatusCode = 190'
        exit 0 """
    script = open(script_file, "w")
    script.write(script_contents)
    script.close()
    st = os.stat(script_file)
    os.chmod(script_file, st.st_mode | stat.S_IEXEC)

    script_file = test_dir / "hook_shadow_idle_prepare.sh"
    script_contents = f"""#!/bin/bash
        echo 'HookStatusMessage = "Kinda bad, will try elsewhere"'
        echo 'HookStatusCode = 390'
        exit 0 """
    script = open(script_file, "w")
    script.write(script_contents)
    script.close()
    st = os.stat(script_file)
    os.chmod(script_file, st.st_mode | stat.S_IEXEC)

    # Test hook is on the shadow side
    # Have 2 hooks, 1 shadow hook that runs before the files are
    # transferred and run by the shadow and 1 hook_prepare_job_before_transfer
    # that is also before files are transferred but ran by the starter.
    # Should show that shadow will always be before the starter.

    # In addition, we run a ps --forest command to show that it was the condor_shadow
    # that invoked the shadow job hook

    # shadow
    filepath = test_dir / "test.out"
    script_file = test_dir / "hook_shadow_test_prepare.sh"
    # will replace starter text if starter ran first, this should not happen
    script_contents = f"""#!/bin/bash
        echo 'From the shadows!' > {filepath}
        ps --forest -o cmd --no-headers | sed 's/condor_shadow.*/condor_shadow/' >> {filepath}
        exit 0
        """
        # Note: the sed command after ps basically is removing any additional info after 'condor_shadow'
        # this makes it easier to test
    script = open(script_file, "w")
    script.write(script_contents)
    script.close()
    st = os.stat(script_file)
    os.chmod(script_file, st.st_mode | stat.S_IEXEC)

    # before_transfer starter
    script_file = test_dir / "hook_starter_test_prepare.sh"
    # in append mode so should add after shadow, otherwise text will be replaced if ran before shadow
    script_contents = f"""#!/bin/bash
        echo 'From the starter!' >> {filepath}
        echo 'HookStatusCode = 190'
        exit 0
        """
    script = open(script_file, "w")
    script.write(script_contents)
    script.close()
    st = os.stat(script_file)
    os.chmod(script_file, st.st_mode | stat.S_IEXEC)


    # Test that the jobad changed at the starter side but not the schedd
    # Do this by using a shadow hook to change a job's classad and by printing
    # contents of .job.ad file into job_ad.out with starter hook

    #shadow hook, used to change a job classad
    job_ad_path = test_dir / "job_ad.out"
    script_file = test_dir / "hook_shadow_check.sh"
    script_contents = f"""#!/bin/bash
        echo 'This is the shadow being the shadow' > {job_ad_path}
        echo 'HookStatusMessage = "Really bad, going on hold"'
        """
    script = open(script_file, "w")
    script.write(script_contents)
    script.close()
    st = os.stat(script_file)
    os.chmod(script_file, st.st_mode | stat.S_IEXEC)

    #starter hook --> used to check change in starter
    script_file = test_dir / "hook_starter_check.sh"
    script_contents = f"""#!/bin/bash
        cat "$_CONDOR_JOB_AD" >> {job_ad_path}
        """
    script = open(script_file, "w")
    script.write(script_contents)
    script.close()
    st = os.stat(script_file)
    os.chmod(script_file, st.st_mode | stat.S_IEXEC)


# TODO: Testing shadow hooks when the keyword is defined in the config file rather
# than the submit file (this requires fixing/implementation of this feature within
# shadow job hooks)

#
# Setup a personal condor with some job hooks defined.
# The "HOLD" hook will put the job on hold.
# The "IDLE" hook will do a shadow exception and put the job back to idle.
# The "TEST" hooks will test to ensure the shadow job hook runs from condor_shadow
#
@standup
def condor(test_dir, write_job_hook_scripts):
    with Condor(
        local_dir=test_dir / "condor",
        config={
            "STARTER_DEBUG": "D_FULLDEBUG",
            "HOLD_HOOK_SHADOW_PREPARE_JOB" : test_dir / "hook_shadow_hold_prepare.sh",
            "IDLE_HOOK_SHADOW_PREPARE_JOB" : test_dir / "hook_shadow_idle_prepare.sh",
            "TEST_HOOK_SHADOW_PREPARE_JOB" : test_dir / "hook_shadow_test_prepare.sh",
            "TEST_HOOK_PREPARE_JOB_BEFORE_TRANSFER" : test_dir / "hook_starter_test_prepare.sh",
            "CHECK_HOOK_PREPARE_JOB" : test_dir / "hook_starter_check.sh",
            "CHECK_HOOK_SHADOW_PREPARE_JOB" : test_dir / "hook_shadow_check.sh"
            #"STARTER_JOB_HOOK_KEYWORD" : "CONFIG",
        }
    ) as condor:
        yield condor


#
# Submit a job using the hook check
#
@action
def submit_checkjob(condor, path_to_sleep):
    return condor.submit(
        description={"executable": path_to_sleep,
                "arguments": "0",
                "+HookKeyword" : '"check"',
                "log": "check_hook_job_events.log",
            }
    )

@action
def checkjob_starter(submit_checkjob):
    return submit_checkjob.query()

#
# Submit a job using the hook test.
#
@action
def submit_testjob(condor, path_to_sleep):
    return condor.submit(
            description={"executable": path_to_sleep,
                "arguments": "0",
                "+HookKeyword" : '"test"',
                "log": "test_hook_job_events.log",
            }
    )

@action
def testjob(submit_testjob):
    # Wait for job to go on hold
    assert submit_testjob.wait(condition=ClusterState.any_held,timeout=60)
    # Return the first (and only) job ad in the cluster for testing class to reference
    return submit_testjob.query()[0]

#
# Submit a job using the hook hold.  This job should end up on hold.
#
@action
def submit_heldjob(condor, path_to_sleep):
    return condor.submit(
            description={"executable": path_to_sleep,
                "arguments": "0",
                "+HookKeyword" : '"hold"',
                "log": "hold_hook_shadow_job_events.log"
                }
    )

@action
def heldjob(submit_heldjob):
    # Wait for job to go on hold
    assert submit_heldjob.wait(condition=ClusterState.any_held,timeout=60)
    # Return the first (and only) job ad in the cluster for testing class to reference
    return submit_heldjob.query()[0]

#
# Submit a job using the "idle" hooks.  This job should start running, then go
# back to idle before exectution.  Add a requirement so it only tries to start once.
#
@action
def submit_idlejob(condor, path_to_sleep):
    return condor.submit(
            description={"executable": path_to_sleep,
                "requirements" : "NumShadowStarts =!= 1",
                "arguments": "0",
                "+HookKeyword" : '"idle"',
                "log": "idle_hook_shadow_job_events.log"
                }
    )

@action
def idlejob(condor,submit_idlejob):
    jobid = submit_idlejob.job_ids[0]
    assert condor.job_queue.wait_for_events(
            expected_events={jobid: [
                SetJobStatus(JobStatus.RUNNING),
                SetJobStatus(JobStatus.IDLE),
                ]},
            timeout=60
            )
    # Return the first (and only) job ad in the cluster for testing class to reference
    return submit_idlejob.query()[0]

# get the shadow log to ensure errors are recorded there
@action
def shadow_job_log(condor, heldjob, idlejob):
    shadow_log_filepath = condor.shadow_log
    return shadow_log_filepath

# function to query the schedd
@action
def checkjob_schedd(condor, submit_checkjob):
    # wait for the job to run
    submit_checkjob.wait(condition=ClusterState.any_running,timeout=30)
    # Query the schedd
    schedd = Condor.query(self=condor, projection=['HookStatusMessage'])
    return schedd

class TestShadowHook:
    # Methods that begin with test_* are tests.

    # Test codes with shadow hooks (and updates to job ads)
    def test_holdreasoncode(self, heldjob):
        assert heldjob["HoldReasonCode"] == 48 #48 means HookShadowPrepareJobFailure (we want that)

    def test_holdreasonsubcode(self, heldjob):
        assert heldjob["HoldReasonSubCode"] == 190

    def test_holdreasonsubcode(self, testjob):
        assert testjob["HoldReasonSubCode"] == 190

    # Test to ensure jobad changes on the starter side but not the schedd

    # Check not in schedd by querying the schedd
    def testNotInSchedd(self, checkjob_schedd):
        assert 'Really bad, going on hold' not in checkjob_schedd

    # Use hooks to check the status message changed in starter
    def testInStarter(self, checkjob_starter):
        with open('job_ad.out') as f:
            log = f.read()
            assert 'Really bad, going on hold' in log

    # Check to see that the shadow hook spawns from the shadow and runs before a starter hook
    def test_shadowbeforestarter(self, testjob):
        with open('test.out') as f:
            log = f.read()
            assert 'shadows!' in log
            assert """
     \_ condor_shadow
         \_ /bin/bash /build/src/condor_tests/test_shadow_hook_ctest/test_shadow_hook/TestShadowHook/hook_shadow_test_prepare.sh
            """ in log
            assert 'starter!' in log

    # Testing of status messages, see job classad changed:
    def test_hold_reason(self, heldjob):
        assert "Really bad" in heldjob["HoldReason"]

    def test_hold_status_msg(self, heldjob):
        with open('hold_hook_shadow_job_events.log') as f:
            log = f.read()
            assert 'Really bad' in log

    def test_idle_status(self, idlejob):
        assert idlejob["NumShadowStarts"] == 1
        assert idlejob["NumJobStarts"] == 0
        assert idlejob["JobStatus"] == 1

    def test_idle_status_msg(self, idlejob):
        with open('idle_hook_shadow_job_events.log') as f:
            log = f.read()
            assert 'Kinda bad' in log

    def test_hold_numholdsbyreason_was_policy(self, heldjob):
        assert heldjob["NumHoldsByReason"] == { 'HookShadowPrepareJobFailure' : 1 }

    # find that the error messages are in shadow log
    def test_in_shadow_log(self, shadow_job_log):
        f = shadow_job_log.open()
        log = f.read()
        found_messages = {"idle" : False, "held" : False}
        for line in log:
            if 'Really bad' in line:
                found_messages["held"] = True
            elif 'Kinda bad' in line:
                found_messages["idle"] = True
            if found_messages["held"] == True and found_messages["idle"] == True:
                break
        assert found_messages["held"] == True
        assert found_messages["idle"] == True