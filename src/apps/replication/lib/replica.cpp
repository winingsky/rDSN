/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Microsoft Corporation
 * 
 * -=- Robust Distributed System Nucleus (rDSN) -=- 
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "replica.h"
#include "mutation.h"
#include "mutation_log.h"
#include "replica_stub.h"

# ifdef __TITLE__
# undef __TITLE__
# endif
# define __TITLE__ "replica"

namespace dsn { namespace replication {

// for replica::load(..) only
replica::replica(replica_stub* stub, const char* path)
: serverlet<replica>("replica")
{
    dassert (stub, "");
    _stub = stub;
    _app = nullptr;
    _check_timer = nullptr;
    _dir = path;
    _options = &stub->options();

    init_state();
}

// for replica::newr(...) only
replica::replica(replica_stub* stub, global_partition_id gpid, const char* app_type)
: serverlet<replica>("replica")
{
    dassert (stub, "");
    _stub = stub;
    _app = nullptr;
    _check_timer = nullptr;

    char buffer[256];
    sprintf(buffer, "%u.%u.%s", gpid.app_id, gpid.pidx, app_type);
    _dir = _stub->dir() + "/" + buffer;
    _options = &stub->options();

    init_state();
    _config.gpid = gpid;
}

void replica::init_state()
{
    _inactive_is_transient = false;
    _prepare_list = new prepare_list(
        0, 
        _options->max_mutation_count_in_prepare_list,
        std::bind(
            &replica::execute_mutation,
            this,
            std::placeholders::_1
            )
    );

    _config.ballot = 0;
    _config.gpid.pidx = 0;
    _config.gpid.app_id = 0;
    _config.status = PS_INACTIVE;
    _primary_states.membership.ballot = 0;
    _last_config_change_time_ms = now_ms();
    _commit_log = nullptr;
}

replica::~replica(void)
{
    close();

    if (nullptr != _prepare_list)
    {
        delete _prepare_list;
        _prepare_list = nullptr;
    }
}

void replica::on_client_read(const read_request_header& meta, dsn_message_t request)
{
    if (status() == PS_INACTIVE || status() == PS_POTENTIAL_SECONDARY)
    {
        response_client_message(request, ERR_INVALID_STATE);
        return;
    }

    if (meta.semantic == read_semantic_t::ReadLastUpdate)
    {
        if (status() != PS_PRIMARY || 
            last_committed_decree() < _primary_states.last_prepare_decree_on_new_primary)
        {
            response_client_message(request, ERR_INVALID_STATE);
            return;
        }
    }

    dassert (_app != nullptr, "");

    rpc_read_stream reader(request);
    _app->dispatch_rpc_call(dsn_task_code_from_string(meta.code.c_str(), TASK_CODE_INVALID),
                            reader, dsn_msg_create_response(request));
}

void replica::response_client_message(dsn_message_t request, error_code error, decree d/* = invalid_decree*/)
{
    if (nullptr == request)
    {
        error.end_tracking();
        return;
    }   

    reply(request, error);
}

error_code replica::check_and_fix_commit_log_completeness()
{
    error_code err = ERR_OK;

    auto mind = _commit_log->min_decree(get_gpid());
    if (!(mind <= last_durable_decree()))
    {
        err = ERR_INCOMPLETE_DATA;
        derror("%s: commit log is incomplete (min/durable): %lld vs %lld",
            name(),
            mind,
            last_durable_decree()
            );

        _commit_log->reset_as_commit_log(get_gpid(), _app->last_durable_decree());
    }

    mind = _commit_log->max_decree(get_gpid());
    if (!(mind >= _app->last_committed_decree()))
    {
        err = ERR_INCOMPLETE_DATA;

        derror("%s: commit log is incomplete (max/commit): %lld vs %lld",
            name(),
            mind,
            _app->last_committed_decree()
            );

        _commit_log->reset_as_commit_log(get_gpid(), _app->last_durable_decree());
    }
    return err;
}

void replica::check_state_completeness()
{
    /* prepare commit durable */
    dassert(max_prepared_decree() >= last_committed_decree(), "");
    dassert(last_committed_decree() >= last_durable_decree(), "");

    if (nullptr != _stub->_log)
    {
        auto mind = _stub->_log->min_decree(get_gpid());
        dassert(mind - _options->staleness_for_commit + 1 <= last_durable_decree(), "");
    }

    if (_commit_log == nullptr)
    {   
        auto mind = _commit_log->min_decree(get_gpid());
        dassert(mind <= last_durable_decree(), "");
    }
}

void replica::execute_mutation(mutation_ptr& mu)
{
    dassert (nullptr != _app, "");

    error_code err = ERR_OK;
    bool write = true;
    decree d = mu->data.header.decree;

    switch (status())
    {
    case PS_INACTIVE:
        if (_app->last_committed_decree() + 1 == d)
            err = _app->write_internal(mu);
        else
        {
            //
            // commit logs may be lost due to failure
            // in this case, we fix commit logs using prepare log
            // so that _app->last_committed_decree() == _commit_log->max_decree(gpid)
            //
            if (_commit_log && d == _commit_log->max_decree(get_gpid()) + 1)
            {
                dinfo("%s: commit log is incomplete (no %s), fix it by rewrite ...",
                    name(),
                    mu->name()
                    );
                write = true;
            }   
            else
                write = false;
            dassert(d <= _app->last_committed_decree(), "");
        }   
        break;
    case PS_PRIMARY:
        {
            check_state_completeness();
            dassert(_app->last_committed_decree() + 1 == d, "");
            err = _app->write_internal(mu);
        }
        break;

    case PS_SECONDARY:
        if (_secondary_states.checkpoint_task == nullptr)
        {
            check_state_completeness();
            dassert (_app->last_committed_decree() + 1 == d, "");
            err = _app->write_internal(mu);
        }
        else
        {
            // make sure commit log saves the state
            // catch-up will be done later after checkpoint task is fininished
            dassert(_commit_log != nullptr, "");
        }
        break;
    case PS_POTENTIAL_SECONDARY:
        if (d == _app->last_committed_decree() + 1)
        {
            dassert(_potential_secondary_states.learning_status >= LearningWithPrepare, "");
            err = _app->write_internal(mu);
        }
        else
        {
            write = false;
            dassert(d <= _app->last_committed_decree(), "");
        }
        break;
    case PS_ERROR:
        write = false;
        break;
    }
    
    ddebug("TwoPhaseCommit, %s: mutation %s committed, err = %s", name(), mu->name(), err.to_string());

    if (err != ERR_OK)
    {
        handle_local_failure(err);
    }

    // write local commit log if necessary
    else if (write && _commit_log)
    {
        _commit_log->append(mu,
            LPC_WRITE_REPLICATION_LOG,
            this,
            [this](error_code err, size_t size)
            {
                if (err != ERR_OK)
                {
                    handle_local_failure(err);
                }
            },
            gpid_to_hash(get_gpid())
            );
    }
}

mutation_ptr replica::new_mutation(decree decree)
{
    mutation_ptr mu(new mutation());
    mu->data.header.gpid = get_gpid();
    mu->data.header.ballot = get_ballot();
    mu->data.header.decree = decree;
    mu->data.header.log_offset = invalid_offset;
    return mu;
}

bool replica::group_configuration(/*out*/ partition_configuration& config) const
{
    if (PS_PRIMARY != status())
        return false;

    config = _primary_states.membership;
    return true;
}

decree replica::last_durable_decree() const { return _app->last_durable_decree(); }

decree replica::last_prepared_decree() const
{
    ballot lastBallot = 0;
    decree start = last_committed_decree();
    while (true)
    {
        auto mu = _prepare_list->get_mutation_by_decree(start + 1);
        if (mu == nullptr 
            || mu->data.header.ballot < lastBallot 
            || !mu->is_logged()
            )
            break;

        start++;
        lastBallot = mu->data.header.ballot;
    }
    return start;
}

void replica::close()
{
    if (nullptr != _check_timer)
    {
        _check_timer->cancel(true);
        _check_timer = nullptr;
    }

    if (status() != PS_INACTIVE && status() != PS_ERROR)
    {
        update_local_configuration_with_no_ballot_change(PS_INACTIVE);
    }

    cleanup_preparing_mutations(true);
    _primary_states.cleanup();
    _secondary_states.cleanup();
    _potential_secondary_states.cleanup(true);

    if (_commit_log != nullptr)
    {
        _commit_log->close();
        _commit_log = nullptr;
    }

    if (_app != nullptr)
    {
        _app->close(false);
        delete _app;
        _app = nullptr;
    }
}

}} // namespace
